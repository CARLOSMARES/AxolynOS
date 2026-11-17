/*
 * AxolynOS - Analizador de ficheros INI y soporte de desktop.ini.
 *
 * Documentacion: Kernel/include/ax_ini.h y manuales/16-ini.md.
 */

#include "ax_types.h"
#include "ax_ini.h"
#include "ax_axfs.h"
#include "ax_fs_disk.h"
#include "ax_memory.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Tabla en memoria                                                    */
/* ------------------------------------------------------------------ */
static AxIniEntry g_entries[AX_INI_MAX_ENTRIES];
static int        g_count = 0;
static char       g_ini_buf[AX_INI_BUFFER_SIZE];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ini_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void ini_memset(void* dst, int value, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = (uint8_t)value;
    }
}

static uint64_t ini_strlen(const char* s)
{
    uint64_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

static void ini_strlcpy(char* dst, const char* src, uint64_t max)
{
    uint64_t i = 0;
    if (!dst || !max) {
        return;
    }
    if (src) {
        while (i + 1 < max && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static char ini_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool ini_eq_ci(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return false;
    }
    while (a[i] && b[i]) {
        if (ini_lower(a[i]) != ini_lower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == b[i];
}

static bool ini_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Recorta los espacios de los extremos de una cadena en su sitio. */
static void ini_trim(char* s)
{
    if (!s) {
        return;
    }
    uint64_t start = 0;
    while (s[start] && ini_is_space(s[start])) {
        start++;
    }
    uint64_t end = start;
    while (s[end]) {
        end++;
    }
    while (end > start && ini_is_space(s[end - 1])) {
        end--;
    }
    if (start > 0) {
        uint64_t j = 0;
        for (uint64_t i = start; i < end; ++i) {
            s[j++] = s[i];
        }
        end -= start;
        start = 0;
    }
    s[end] = '\0';
}

/* Elimina las comillas que envuelven un valor. */
static void ini_unquote(char* s)
{
    uint64_t n = ini_strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\''))) {
        for (uint64_t i = 0; i + 1 < n; ++i) {
            s[i] = s[i + 1];
        }
        s[n - 2] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Busqueda y modificacion de la tabla                                 */
/* ------------------------------------------------------------------ */
static int ini_find(const char* section, const char* key)
{
    for (int i = 0; i < g_count; ++i) {
        if (ini_eq_ci(g_entries[i].section, section) &&
            ini_eq_ci(g_entries[i].key, key)) {
            return i;
        }
    }
    return -1;
}

int ax_ini_clear(void)
{
    g_count = 0;
    ini_memset(g_entries, 0, sizeof(g_entries));
    return 0;
}

int ax_ini_count(void)
{
    return g_count;
}

int ax_ini_enum(uint32_t index, AxIniEntry* out)
{
    if (!out || index >= (uint32_t)g_count) {
        return AX_ENOENT;
    }
    ini_memcpy(out, &g_entries[index], sizeof(*out));
    return 0;
}

int ax_ini_get(const char* section, const char* key, char* out, uint64_t max)
{
    if (!out || max == 0 || !section || !key) {
        return AX_EINVAL;
    }
    int i = ini_find(section, key);
    if (i < 0) {
        return AX_ENOENT;
    }
    ini_strlcpy(out, g_entries[i].value, max);
    return 0;
}

int ax_ini_set(const char* section, const char* key, const char* value)
{
    if (!section || !key) {
        return AX_EINVAL;
    }
    if (ini_strlen(key) >= AX_INI_NAME_MAX) {
        return AX_ERANGE;
    }
    if (ini_strlen(value) >= AX_INI_VALUE_MAX) {
        return AX_ERANGE;
    }

    int i = ini_find(section, key);
    if (i >= 0) {
        ini_strlcpy(g_entries[i].value, value, AX_INI_VALUE_MAX);
        return 0;
    }
    if (g_count >= AX_INI_MAX_ENTRIES) {
        return AX_ENOSPC;
    }

    AxIniEntry* e = &g_entries[g_count++];
    ini_memset(e, 0, sizeof(*e));
    ini_strlcpy(e->section, section, AX_INI_NAME_MAX);
    ini_strlcpy(e->key, key, AX_INI_NAME_MAX);
    ini_strlcpy(e->value, value ? value : "", AX_INI_VALUE_MAX);
    return 0;
}

int ax_ini_get_int(const char* section, const char* key, int def)
{
    char value[AX_INI_VALUE_MAX];
    if (ax_ini_get(section, key, value, sizeof(value)) != 0) {
        return def;
    }
    int sign = 1;
    uint64_t i = 0;
    if (value[0] == '-') {
        sign = -1;
        i = 1;
    } else if (value[0] == '+') {
        i = 1;
    }
    if (!value[i]) {
        return def;
    }
    int64_t n = 0;
    for (; value[i]; ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return def;
        }
        n = n * 10 + (value[i] - '0');
    }
    return (int)(sign * n);
}

bool ax_ini_get_bool(const char* section, const char* key, bool def)
{
    char value[AX_INI_VALUE_MAX];
    if (ax_ini_get(section, key, value, sizeof(value)) != 0) {
        return def;
    }
    if (ini_eq_ci(value, "1") || ini_eq_ci(value, "true") ||
        ini_eq_ci(value, "yes") || ini_eq_ci(value, "on")) {
        return true;
    }
    if (ini_eq_ci(value, "0") || ini_eq_ci(value, "false") ||
        ini_eq_ci(value, "no") || ini_eq_ci(value, "off")) {
        return false;
    }
    return def;
}

/* ------------------------------------------------------------------ */
/* Analizador                                                          */
/* ------------------------------------------------------------------ */
int ax_ini_parse(const char* text, uint64_t size)
{
    if (!text || size == 0) {
        return AX_EINVAL;
    }

    ax_ini_clear();

    char section[AX_INI_NAME_MAX] = "";
    char line[AX_INI_NAME_MAX + AX_INI_VALUE_MAX + 4];

    uint64_t pos = 0;
    while (pos < size) {
        /* Copia la linea actual. */
        uint64_t n = 0;
        while (pos + n < size && text[pos + n] != '\n' && n + 1 < sizeof(line)) {
            line[n] = text[pos + n];
            n++;
        }
        line[n] = '\0';
        pos += n;
        if (pos < size && text[pos] == '\n') {
            pos++;
        }

        ini_trim(line);

        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[') {
            uint64_t end = 1;
            while (line[end] && line[end] != ']') {
                end++;
            }
            if (line[end] != ']') {
                continue;   /* seccion mal formada */
            }
            line[end] = '\0';
            ini_strlcpy(section, line + 1, sizeof(section));
            ini_trim(section);
            continue;
        }

        char* eq = nullptr;
        for (char* p = line; *p; ++p) {
            if (*p == '=') {
                eq = p;
                break;
            }
        }
        if (!eq) {
            continue;       /* linea sin clave=valor */
        }
        *eq = '\0';
        char* key   = line;
        char* value = eq + 1;
        ini_trim(key);
        ini_trim(value);
        ini_unquote(value);

        if (key[0] == '\0') {
            continue;
        }
        ax_ini_set(section, key, value);
    }

    return g_count;
}

/* ------------------------------------------------------------------ */
/* Lectura y escritura de ficheros                                     */
/* ------------------------------------------------------------------ */
int ax_ini_load(const char* path)
{
    if (!path || !path[0]) {
        return AX_EINVAL;
    }

    uint64_t vsize = 0;
    const void* vdata = ax_vfs_read(path, &vsize);
    if (vdata && vsize) {
        return ax_ini_parse((const char*)vdata, vsize);
    }

    if (!ax_fs_disk_mounted()) {
        return AX_ENOENT;
    }

    uint32_t type = 0;
    uint64_t dsize = 0;
    if (ax_fs_disk_stat(path, &type, &dsize) != 0 ||
        type != AXFS_DISK_TYPE_FILE || dsize == 0) {
        return AX_ENOENT;
    }

    char* buf = (char*)ax_malloc(dsize + 1);
    if (!buf) {
        return AX_ENOMEM;
    }
    uint64_t got = 0;
    if (ax_fs_disk_read(path, buf, dsize, &got) != 0 || got != dsize) {
        ax_free(buf);
        return AX_EIO;
    }
    buf[dsize] = '\0';
    int rc = ax_ini_parse(buf, dsize);
    ax_free(buf);
    return rc;
}

int ax_ini_save(const char* path)
{
    if (!path || !path[0]) {
        return AX_EINVAL;
    }
    if (!ax_fs_disk_mounted()) {
        return AX_ENOENT;
    }

    uint64_t used = 0;
    bool section_done[AX_INI_MAX_ENTRIES];
    ini_memset(section_done, 0, sizeof(section_done));

    for (int i = 0; i < g_count; ++i) {
        AxIniEntry* e = &g_entries[i];
        if (section_done[i]) {
            continue;
        }

        /* Cabecera de la seccion (omitida si es la seccion vacia). */
        if (e->section[0]) {
            if (used + 1 + ini_strlen(e->section) + 2 + 1 > AX_INI_BUFFER_SIZE) {
                return AX_ENOSPC;
            }
            g_ini_buf[used++] = '[';
            for (uint64_t k = 0; e->section[k]; ++k) {
                g_ini_buf[used++] = e->section[k];
            }
            g_ini_buf[used++] = ']';
            g_ini_buf[used++] = '\n';
        }

        /* Todas las claves de esta seccion. */
        for (int j = i; j < g_count; ++j) {
            if (!ini_eq_ci(g_entries[j].section, e->section)) {
                continue;
            }
            section_done[j] = true;
            uint64_t need = ini_strlen(g_entries[j].key) +
                            ini_strlen(g_entries[j].value) + 2;
            if (used + need + 1 > AX_INI_BUFFER_SIZE) {
                return AX_ENOSPC;
            }
            for (uint64_t k = 0; g_entries[j].key[k]; ++k) {
                g_ini_buf[used++] = g_entries[j].key[k];
            }
            g_ini_buf[used++] = '=';
            for (uint64_t k = 0; g_entries[j].value[k]; ++k) {
                g_ini_buf[used++] = g_entries[j].value[k];
            }
            g_ini_buf[used++] = '\n';
        }
    }

    return ax_fs_disk_write(path, g_ini_buf, used);
}

/* ------------------------------------------------------------------ */
/* desktop.ini                                                         */
/* ------------------------------------------------------------------ */
int ax_ini_load_desktop(const char* dir, AxIniDesktop* out)
{
    if (!out) {
        return AX_EINVAL;
    }
    ini_memset(out, 0, sizeof(*out));
    out->icon_index = 0;

    char path[192];
    if (!dir || dir[0] == '\0' || (dir[0] == '/' && dir[1] == '\0')) {
        ini_strlcpy(path, "/desktop.ini", sizeof(path));
    } else {
        ini_strlcpy(path, dir, sizeof(path));
        uint64_t n = ini_strlen(path);
        if (n + 12 < sizeof(path)) {
            path[n++] = '/';
            const char* leaf = "desktop.ini";
            for (uint64_t i = 0; leaf[i]; ++i) {
                path[n++] = leaf[i];
            }
            path[n] = '\0';
        }
    }

    if (ax_ini_load(path) < 0) {
        return AX_ENOENT;
    }

    const char* sec = ".ShellClassInfo";
    char value[AX_INI_VALUE_MAX];

    if (ax_ini_get(sec, "DisplayName", value, sizeof(value)) == 0) {
        ini_strlcpy(out->display_name, value, sizeof(out->display_name));
        out->valid = true;
    }
    if (ax_ini_get(sec, "IconFile", value, sizeof(value)) == 0) {
        ini_strlcpy(out->icon_file, value, sizeof(out->icon_file));
        out->valid = true;
    } else if (ax_ini_get(sec, "IconResource", value, sizeof(value)) == 0) {
        /* IconResource puede traer "fichero,indice". */
        char* comma = nullptr;
        for (char* p = value; *p; ++p) {
            if (*p == ',') {
                comma = p;
            }
        }
        if (comma) {
            *comma = '\0';
            out->icon_index = ax_ini_get_int(sec, "IconIndex", 0);
        }
        ini_strlcpy(out->icon_file, value, sizeof(out->icon_file));
        out->valid = true;
    }
    if (ax_ini_get(sec, "InfoTip", value, sizeof(value)) == 0) {
        ini_strlcpy(out->info_tip, value, sizeof(out->info_tip));
        out->valid = true;
    }
    if (ax_ini_get(sec, "IconIndex", value, sizeof(value)) == 0) {
        out->icon_index = ax_ini_get_int(sec, "IconIndex", out->icon_index);
        out->valid = true;
    }

    return out->valid ? 0 : AX_ENOENT;
}
