/*
 * AxolynOS - Registro del sistema.
 *
 * Formato de /system/registry.dat:
 *   cabecera: "AXREG002" version count reserved  (32 bytes)
 *   registro: path[96] value[256] type len reserved (400 bytes)
 */
#include "ax_registry.h"
#include "ax_fs_disk.h"
#include "ax_console.h"

#define REG_DB_PATH       "/system/registry.dat"
#define REG_RECORD_SIZE   400u
#define REG_HEADER_SIZE   32u
#define REG_DB_MAX        (REG_HEADER_SIZE + AX_REG_MAX_ENTRIES * REG_RECORD_SIZE)

static AxRegEntry g_entries[AX_REG_MAX_ENTRIES];
static uint8_t    g_io[REG_DB_MAX];
static bool       g_loaded = false;

static int reg_strlen(const char* s)
{
    int n = 0;
    if (s) {
        while (s[n]) {
            ++n;
        }
    }
    return n;
}

static bool reg_streq(const char* a, const char* b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static char reg_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool reg_ieq(const char* a, const char* b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (reg_lower(*a) != reg_lower(*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void reg_copy(char* dst, uint64_t max, const char* src)
{
    uint64_t i = 0;
    if (!dst || max == 0) {
        return;
    }
    if (src) {
        for (; i + 1 < max && src[i]; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static void reg_zero(void* dst, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < n; ++i) {
        p[i] = 0;
    }
}

static void reg_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

static void reg_put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t reg_get32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int reg_find(const char* path)
{
    if (!path) {
        return -1;
    }
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (g_entries[i].path[0] && reg_ieq(g_entries[i].path, path)) {
            return i;
        }
    }
    return -1;
}

static int reg_free(void)
{
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (!g_entries[i].path[0]) {
            return i;
        }
    }
    return -1;
}

static void reg_int_to_str(int64_t value, char* out, uint64_t max)
{
    char tmp[24];
    int pos = 0;
    bool neg = value < 0;
    uint64_t mag = neg ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    if (mag == 0) {
        reg_copy(out, max, "0");
        return;
    }
    while (mag && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (mag % 10));
        mag /= 10;
    }
    uint64_t o = 0;
    if (neg && o + 1 < max) {
        out[o++] = '-';
    }
    while (pos > 0 && o + 1 < max) {
        out[o++] = tmp[--pos];
    }
    out[o] = '\0';
}

static bool reg_valid_path(const char* path)
{
    if (!path || !path[0]) {
        return false;
    }
    if ((uint64_t)reg_strlen(path) >= AX_REG_PATH_MAX) {
        return false;
    }
    /* Raices conocidas (se acepta tambien cualquier raiz alfanumerica). */
    return true;
}

static void reg_serialize_record(const AxRegEntry* e, uint8_t* out)
{
    reg_zero(out, REG_RECORD_SIZE);
    reg_memcpy(out, e->path, AX_REG_PATH_MAX);
    reg_memcpy(out + AX_REG_PATH_MAX, e->value, AX_REG_VALUE_MAX);
    reg_put32(out + AX_REG_PATH_MAX + AX_REG_VALUE_MAX, e->type);
    reg_put32(out + AX_REG_PATH_MAX + AX_REG_VALUE_MAX + 4, e->value_len);
}

static void reg_deserialize_record(const uint8_t* in, AxRegEntry* e)
{
    reg_zero(e, sizeof(*e));
    reg_memcpy(e->path, in, AX_REG_PATH_MAX);
    e->path[AX_REG_PATH_MAX - 1] = '\0';
    reg_memcpy(e->value, in + AX_REG_PATH_MAX, AX_REG_VALUE_MAX);
    e->value[AX_REG_VALUE_MAX - 1] = '\0';
    e->type = reg_get32(in + AX_REG_PATH_MAX + AX_REG_VALUE_MAX);
    e->value_len = reg_get32(in + AX_REG_PATH_MAX + AX_REG_VALUE_MAX + 4);
    if (e->value_len >= AX_REG_VALUE_MAX) {
        e->value_len = AX_REG_VALUE_MAX - 1;
    }
}

static int reg_save_defaults(void)
{
    (void)ax_registry_set("HKLM\\System\\Version", "0.2", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\System\\Kernel", "Axolyn Kernel", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\System\\Architecture", "x86_64", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\System\\BootMode", "BIOS/UEFI", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\Software\\Shell\\Prompt", "AxolynOS>", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\Software\\Shell\\History", "64", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\Software\\Axolyn\\DefaultUser", "user", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKLM\\Software\\Axolyn\\Crypto", "PBKDF2-SHA256/ChaCha20", AX_REG_TYPE_STRING);
    (void)ax_registry_set("HKCU\\Environment\\Path", "/bin", AX_REG_TYPE_STRING);
    return 0;
}

int ax_registry_save(void)
{
    if (!ax_fs_disk_mounted()) {
        return 0;
    }
    uint32_t count = 0;
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (g_entries[i].path[0]) {
            ++count;
        }
    }

    reg_zero(g_io, sizeof(g_io));
    reg_memcpy(g_io, "AXREG002", 8);
    reg_put32(g_io + 8, 1);
    reg_put32(g_io + 12, count);
    uint32_t off = REG_HEADER_SIZE;
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (!g_entries[i].path[0]) {
            continue;
        }
        reg_serialize_record(&g_entries[i], g_io + off);
        off += REG_RECORD_SIZE;
    }

    (void)ax_fs_disk_mkdir("/system");
    int rc = ax_fs_disk_write(REG_DB_PATH, g_io, off);
    if (rc != 0) {
        ax_printf("[registry] error guardando %s (%d)\n", REG_DB_PATH, rc);
    }
    return rc;
}

static bool reg_load(void)
{
    if (!ax_fs_disk_mounted()) {
        return false;
    }
    if (!ax_registry_exists(REG_DB_PATH)) {
        return false;
    }
    uint64_t got = 0;
    if (ax_fs_disk_read(REG_DB_PATH, g_io, sizeof(g_io), &got) != 0 ||
        got < REG_HEADER_SIZE || !reg_streq((const char*)g_io, "AXREG002")) {
        return false;
    }
    uint32_t count = reg_get32(g_io + 12);
    if (count > AX_REG_MAX_ENTRIES) {
        count = AX_REG_MAX_ENTRIES;
    }
    uint32_t off = REG_HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + REG_RECORD_SIZE > got) {
            break;
        }
        reg_deserialize_record(g_io + off, &g_entries[i]);
        off += REG_RECORD_SIZE;
    }
    return true;
}

void ax_registry_init(void)
{
    reg_zero(g_entries, sizeof(g_entries));
    g_loaded = false;

    if (reg_load()) {
        g_loaded = true;
        ax_printf("[registry] %d claves cargadas desde %s\n",
                  ax_registry_count(), REG_DB_PATH);
        return;
    }

    reg_save_defaults();
    g_loaded = true;
    if (ax_fs_disk_mounted()) {
        (void)ax_registry_save();
    }
    ax_printf("[registry] registro inicializado (%d claves)\n",
              ax_registry_count());
}

int ax_registry_get(const char* path, char* out, uint64_t max, uint32_t* type)
{
    int idx = reg_find(path);
    if (idx < 0) {
        return -1;
    }
    if (out && max) {
        uint64_t n = g_entries[idx].value_len;
        if (n >= max) {
            n = max - 1;
        }
        reg_memcpy(out, g_entries[idx].value, n);
        out[n] = '\0';
    }
    if (type) {
        *type = g_entries[idx].type;
    }
    return 0;
}

bool ax_registry_exists(const char* path)
{
    return reg_find(path) >= 0;
}

int ax_registry_set(const char* path, const char* value, uint32_t type)
{
    if (!reg_valid_path(path)) {
        return -1;
    }
    if (type != AX_REG_TYPE_STRING && type != AX_REG_TYPE_INT) {
        type = AX_REG_TYPE_STRING;
    }
    int idx = reg_find(path);
    if (idx < 0) {
        idx = reg_free();
        if (idx < 0) {
            return -2;
        }
        reg_zero(&g_entries[idx], sizeof(AxRegEntry));
        reg_copy(g_entries[idx].path, AX_REG_PATH_MAX, path);
    }
    reg_copy(g_entries[idx].value, AX_REG_VALUE_MAX, value ? value : "");
    g_entries[idx].value_len = (uint32_t)reg_strlen(g_entries[idx].value);
    g_entries[idx].type = type;
    if (ax_fs_disk_mounted()) {
        (void)ax_registry_save();
    }
    return 0;
}

int ax_registry_set_int(const char* path, int64_t value)
{
    char buf[32];
    reg_int_to_str(value, buf, sizeof(buf));
    return ax_registry_set(path, buf, AX_REG_TYPE_INT);
}

int ax_registry_del(const char* path)
{
    int idx = reg_find(path);
    if (idx < 0) {
        return -1;
    }
    reg_zero(&g_entries[idx], sizeof(AxRegEntry));
    if (ax_fs_disk_mounted()) {
        (void)ax_registry_save();
    }
    return 0;
}

int ax_registry_enum(uint32_t index, AxRegEntry* out)
{
    if (!out) {
        return -1;
    }
    uint32_t seen = 0;
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (!g_entries[i].path[0]) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        reg_memcpy(out, &g_entries[i], sizeof(AxRegEntry));
        return 0;
    }
    return -1;
}

int ax_registry_count(void)
{
    int c = 0;
    for (int i = 0; i < AX_REG_MAX_ENTRIES; ++i) {
        if (g_entries[i].path[0]) {
            ++c;
        }
    }
    return c;
}

const char* ax_registry_type_name(uint32_t type)
{
    switch (type) {
        case AX_REG_TYPE_INT: return "REG_DWORD";
        case AX_REG_TYPE_STRING: return "REG_SZ";
        default: return "REG_UNKNOWN";
    }
}
