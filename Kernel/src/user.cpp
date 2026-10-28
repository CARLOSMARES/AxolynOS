/*
 * AxolynOS - Sistema de usuarios y permisos
 */
#include "ax_user.h"
#include "ax_console.h"
#include "ax_memory.h"
#include "ax_fs_disk.h"

static AxUser g_users[AX_USER_MAX_USERS];
static int g_current_user = -1; /* -1 = sin login */
static bool g_db_loaded = false; /* hay /etc/passwd en el volumen */

/* Base de datos de usuarios en AxFS-D: una linea por usuario.
 *
 *   nombre:uid:gid:admin:HHHHHHHH\n
 *
 * El campo final es el hash FNV-1a de la contrasena (8 digitos hex). */
#define USER_DB_PATH "/etc/passwd"

static int user_strlen(const char* s)
{
    int n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static bool user_streq(const char* a, const char* b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint32_t user_hash_pwd(const char* password);

static void user_set_password(AxUser* u, const char* password)
{
    uint32_t h = user_hash_pwd(password);
    for (int i = 0; i < 4; ++i) {
        u->password[i] = (char)((h >> (i * 8)) & 0xff);
    }
    for (int i = 4; i < AX_USER_MAX_PASS; ++i) {
        u->password[i] = 0;
    }
}

static void user_copy(AxUser* dst, const AxUser* src)
{
    for (size_t i = 0; i < sizeof(AxUser); ++i) {
        ((uint8_t*)dst)[i] = ((const uint8_t*)src)[i];
    }
}

static void user_clear(AxUser* u)
{
    for (size_t i = 0; i < sizeof(AxUser); ++i) {
        ((uint8_t*)u)[i] = 0;
    }
    u->active = false;
    u->is_admin = false;
}

static uint32_t user_hash_pwd(const char* pwd)
{
    uint32_t h = 0x811c9dc5;
    if (!pwd) return h;
    for (const char* p = pwd; *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 0x1000193;
    }
    return h;
}

void ax_user_init(void)
{
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        user_clear(&g_users[i]);
    }
    /* Crear usuario root */
    user_clear(&g_users[0]);
    g_users[0].uid = AX_USER_ROOT_UID;
    g_users[0].gid = 0;
    const char* rname = "root";
    for (size_t i = 0; i < AX_USER_MAX_NAME - 1 && rname[i]; ++i) g_users[0].username[i] = rname[i];
    g_users[0].username[AX_USER_MAX_NAME - 1] = '\0';
    /* password por defecto vacia/root */
    const char* rpwd = "root";
    uint32_t h = user_hash_pwd(rpwd);
    for (int i = 0; i < 4; ++i) g_users[0].password[i] = (char)((h >> (i*8)) & 0xff);
    g_users[0].active = true;
    g_users[0].is_admin = true;
    g_users[0].perms = AX_PERM_ALL;
    g_current_user = -1;
    g_db_loaded = false;
    ax_printf("[user] sistema de usuarios inicializado (root)\n");
}

int ax_user_login(const char* username, const char* password)
{
    if (!username) return -1;
    uint32_t h = user_hash_pwd(password);
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) continue;
        if (!user_streq(g_users[i].username, username)) continue;
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            if (g_users[i].password[k] != (char)((h >> (k*8)) & 0xff)) { ok = false; break; }
        }
        if (!ok) continue;
        g_current_user = i;
        ax_printf("[user] sesion iniciada como %s (uid=%u)\n",
                  g_users[i].username, g_users[i].uid);
        return 0;
    }
    return -1;
}

int ax_user_logout(void)
{
    g_current_user = -1;
    ax_printf("[user] sesion cerrada\n");
    return 0;
}

int ax_user_add(const char* username, const char* password, bool is_admin)
{
    if (g_current_user < 0 || !g_users[g_current_user].is_admin) return -1;
    if (!username || !username[0]) return -1;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (g_users[i].active && user_streq(username, g_users[i].username)) {
            return -1;      /* el nombre ya existe */
        }
    }
    for (int i = 1; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            user_clear(&g_users[i]);
            g_users[i].uid = 1000 + i;
            g_users[i].gid = 1000 + i;
            for (size_t j = 0; j < AX_USER_MAX_NAME - 1 && username[j]; ++j) g_users[i].username[j] = username[j];
            g_users[i].username[AX_USER_MAX_NAME - 1] = '\0';
            uint32_t h = user_hash_pwd(password);
            for (int k = 0; k < 4; ++k) g_users[i].password[k] = (char)((h >> (k*8)) & 0xff);
            g_users[i].active = true;
            g_users[i].is_admin = is_admin;
            g_users[i].perms = is_admin ? AX_PERM_ALL : (AX_PERM_READ | AX_PERM_EXEC);
            ax_printf("[user] creado usuario %s\n", g_users[i].username);
            (void)ax_user_save();
            return 0;
        }
    }
    return -1;
}

int ax_user_del(const char* username)
{
    if (g_current_user < 0 || !g_users[g_current_user].is_admin) return -1;
    if (!username) return -1;
    for (int i = 1; i < AX_USER_MAX_USERS; ++i) {
        if (g_users[i].active && user_streq(g_users[i].username, username)) {
            user_clear(&g_users[i]);
            (void)ax_user_save();
            return 0;
        }
    }
    return -1;
}

int ax_user_list(char* out, uint64_t max)
{
    if (!out || max == 0) return -1;
    size_t pos = 0;
    const char* hdr = "USERS:\n";
    for (const char* p = hdr; *p && pos < max - 1; ++p) out[pos++] = *p;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) continue;
        if (pos < max - 1) out[pos++] = ' ';
        for (const char* p = g_users[i].username; *p && pos < max - 1; ++p) out[pos++] = *p;
        if (g_users[i].is_admin && pos < max - 6) { out[pos++] = '('; out[pos++] = 'a'; out[pos++] = 'd'; out[pos++] = 'm'; out[pos++] = 'i'; out[pos++] = 'n'; out[pos++] = ')'; }
        if (pos < max - 1) out[pos++] = '\n';
    }
    out[pos] = '\0';
    return (int)pos;
}

int ax_user_count(void)
{
    int c = 0;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) if (g_users[i].active) ++c;
    return c;
}

AxUser* ax_user_current(void)
{
    if (g_current_user < 0 || g_current_user >= AX_USER_MAX_USERS) return 0;
    if (!g_users[g_current_user].active) return 0;
    return &g_users[g_current_user];
}

uint32_t ax_user_getuid(void)
{
    AxUser* u = ax_user_current();
    return u ? u->uid : AX_USER_GUEST_UID;
}

bool ax_user_is_root(void)
{
    AxUser* u = ax_user_current();
    return u ? (u->is_admin || u->uid == 0) : false;
}

bool ax_user_check_perm(const char* path, uint32_t need_perm)
{
    (void)path;
    AxUser* u = ax_user_current();
    if (!u) return false;
    if (u->is_admin) return true;
    return (u->perms & need_perm) == need_perm;
}

/* ------------------------------------------------------------------ */
/* Persistencia en /etc/passwd (AxFS-D)                                */
/* ------------------------------------------------------------------ */

static int user_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint32_t user_parse_u32(const char* s, const char* end)
{
    uint32_t v = 0;
    while (s < end && *s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static uint32_t user_parse_hash(const char* s, const char* end)
{
    /* El hash se guarda byte a byte en orden little-endian. */
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        if (s + 1 >= end) {
            break;
        }
        int hi = user_hexval(s[0]);
        int lo = user_hexval(s[1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        v |= ((uint32_t)((hi << 4) | lo)) << (k * 8);
        s += 2;
    }
    return v;
}

static size_t user_append(char* out, size_t pos, size_t max, const char* s)
{
    if (!s) {
        return pos;
    }
    while (*s && pos + 1 < max) {
        out[pos++] = *s++;
    }
    return pos;
}

static size_t user_append_u32(char* out, size_t pos, size_t max, uint32_t v)
{
    char digits[16];
    int n = 0;
    if (v == 0) {
        digits[n++] = '0';
    }
    while (v && n < (int)sizeof(digits)) {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0 && pos + 1 < max) {
        out[pos++] = digits[--n];
    }
    return pos;
}

static size_t user_append_hash(char* out, size_t pos, size_t max,
                               const char* pwd_bytes)
{
    static const char hex[] = "0123456789abcdef";
    for (int k = 0; k < 4 && pos + 1 < max; ++k) {
        uint8_t b = (uint8_t)pwd_bytes[k];
        if (pos + 2 < max) {
            out[pos++] = hex[(b >> 4) & 0x0f];
        }
        if (pos + 1 < max) {
            out[pos++] = hex[b & 0x0f];
        }
    }
    return pos;
}

int ax_user_save(void)
{
    if (!ax_fs_disk_mounted()) {
        return -1;
    }

    char buf[2048];
    size_t pos = 0;

    for (int i = 1; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            continue;
        }
        pos = user_append(buf, pos, sizeof(buf), g_users[i].username);
        if (pos + 1 < sizeof(buf)) buf[pos++] = ':';
        pos = user_append_u32(buf, pos, sizeof(buf), g_users[i].uid);
        if (pos + 1 < sizeof(buf)) buf[pos++] = ':';
        pos = user_append_u32(buf, pos, sizeof(buf), g_users[i].gid);
        if (pos + 1 < sizeof(buf)) buf[pos++] = ':';
        if (pos + 1 < sizeof(buf)) buf[pos++] = g_users[i].is_admin ? '1' : '0';
        if (pos + 1 < sizeof(buf)) buf[pos++] = ':';
        pos = user_append_hash(buf, pos, sizeof(buf), g_users[i].password);
        if (pos + 1 < sizeof(buf)) buf[pos++] = '\n';
    }
    buf[pos] = '\0';

    (void)ax_fs_disk_mkdir("/etc");
    int rc = ax_fs_disk_write(USER_DB_PATH, buf, pos);
    if (rc == 0) {
        ax_printf("[user] base de usuarios guardada (%u bytes)\n",
                  (unsigned)pos);
    }
    return rc == 0 ? 0 : -1;
}

static void user_load_line(const char* line, size_t len)
{
    /* nombre:uid:gid:admin:hash */
    size_t c1 = len, c2 = len, c3 = len, c4 = len;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] != ':') {
            continue;
        }
        if (c1 == len) c1 = i;
        else if (c2 == len) c2 = i;
        else if (c3 == len) c3 = i;
        else if (c4 == len) c4 = i;
    }
    if (c1 == len || c2 == len || c3 == len || c4 == len || c1 == 0) {
        return;
    }

    char name[AX_USER_MAX_NAME];
    size_t nlen = c1;
    if (nlen >= sizeof(name)) {
        nlen = sizeof(name) - 1;
    }
    for (size_t i = 0; i < nlen; ++i) {
        name[i] = line[i];
    }
    name[nlen] = '\0';
    if (user_streq(name, "root")) {
        return;
    }

    int slot = -1;
    for (int i = 1; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    AxUser* u = &g_users[slot];
    user_clear(u);
    u->uid = user_parse_u32(line + c1 + 1, line + c2);
    u->gid = user_parse_u32(line + c2 + 1, line + c3);
    u->is_admin = (c3 + 1 < len && line[c3 + 1] == '1');
    for (size_t i = 0; i < nlen; ++i) {
        u->username[i] = name[i];
    }
    u->username[nlen] = '\0';
    uint32_t h = user_parse_hash(line + c4 + 1, line + len);
    for (int k = 0; k < 4; ++k) {
        u->password[k] = (char)((h >> (k * 8)) & 0xff);
    }
    u->active = true;
    u->perms = u->is_admin ? AX_PERM_ALL : (AX_PERM_READ | AX_PERM_EXEC);
}

void ax_user_boot(void)
{
    g_db_loaded = false;
    if (!ax_fs_disk_mounted()) {
        return;
    }

    uint32_t type = 0;
    uint64_t size = 0;
    if (ax_fs_disk_stat(USER_DB_PATH, &type, &size) != 0 ||
        type != AXFS_DISK_TYPE_FILE) {
        return;
    }

    g_db_loaded = true;
    if (size == 0 || size >= 2048) {
        return;
    }

    char buf[2048];
    uint64_t got = 0;
    if (ax_fs_disk_read(USER_DB_PATH, buf, size, &got) != 0) {
        return;
    }
    buf[got] = '\0';

    size_t pos = 0;
    while (pos < got) {
        size_t end = pos;
        while (end < got && buf[end] != '\n') {
            end++;
        }
        if (end > pos) {
            user_load_line(buf + pos, end - pos);
        }
        pos = end + 1;
    }

    ax_printf("[user] %d usuario(s) cargados de %s\n",
              ax_user_count() - 1, USER_DB_PATH);
}

bool ax_user_needs_setup(void)
{
    return !g_db_loaded;
}

int ax_user_setup(const char* username, const char* password)
{
    if (!username || !username[0] || !password) {
        return -1;
    }
    if (user_strlen(username) >= AX_USER_MAX_NAME) {
        return -1;
    }

    int slot = -1;
    for (int i = 1; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }

    AxUser* u = &g_users[slot];
    user_clear(u);
    u->uid = 1000 + (uint32_t)slot;
    u->gid = 1000 + (uint32_t)slot;
    for (size_t i = 0; i < AX_USER_MAX_NAME - 1 && username[i]; ++i) {
        u->username[i] = username[i];
    }
    u->username[AX_USER_MAX_NAME - 1] = '\0';
    user_set_password(u, password);
    u->active = true;
    u->is_admin = true;
    u->perms = AX_PERM_ALL;

    g_current_user = slot;
    g_db_loaded = true;

    if (ax_fs_disk_mounted()) {
        (void)ax_user_save();
    }
    ax_printf("[user] cuenta '%s' creada\n", u->username);
    return 0;
}
