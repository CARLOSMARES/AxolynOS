/*
 * AxolynOS - Gestor propio de usuarios cifrados.
 *
 * Formato de /etc/users.db:
 *
 *   cabecera (48 bytes):
 *     "AXUDB002"  version  count  iterations  reserved  salt_db[16]
 *
 *   registro (192 bytes):
 *     name[32] uid gid flags iterations
 *     salt[16] nonce[12] ct_len
 *     ciphertext[64] hmac[32] reserved[16]
 *
 * El texto cifrado contiene name[32] + verify[32].  La clave se obtiene
 * con PBKDF2-HMAC-SHA256(password, salt, iterations).  El HMAC cubre la
 * cabecera del registro y el texto cifrado, de modo que cualquier cambio
 * en disco invalida la cuenta.
 */
#include "ax_user.h"
#include "ax_console.h"
#include "ax_memory.h"
#include "ax_fs_disk.h"
#include "ax_crypto.h"

#define USER_DB_PATH      "/etc/users.db"
#define USER_OLD_DB_PATH  "/etc/passwd"
#define USER_HEADER_SIZE  48u
#define USER_RECORD_SIZE  192u
#define USER_DB_MAX       (USER_HEADER_SIZE + AX_USER_MAX_USERS * USER_RECORD_SIZE)

#define USER_FLAG_ACTIVE  0x01u
#define USER_FLAG_ADMIN   0x02u
#define USER_FLAG_LEGACY  0x04u

static AxUser g_users[AX_USER_MAX_USERS];
static int    g_current_user = -1;
static bool   g_db_loaded = false;

/* ------------------------------------------------------------------ */
/* Utilidades basicas                                                  */
/* ------------------------------------------------------------------ */
static int user_strlen(const char* s)
{
    int n = 0;
    if (s) {
        while (s[n]) {
            ++n;
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
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void user_copy_name(char* dst, const char* src)
{
    int i = 0;
    if (!dst) {
        return;
    }
    if (src) {
        for (; i + 1 < AX_USER_MAX_NAME && src[i]; ++i) {
            dst[i] = src[i];
        }
    }
    for (; i < AX_USER_MAX_NAME; ++i) {
        dst[i] = '\0';
    }
}

static void user_clear(AxUser* u)
{
    uint8_t* p = (uint8_t*)u;
    for (size_t i = 0; i < sizeof(AxUser); ++i) {
        p[i] = 0;
    }
    u->active = false;
    u->is_admin = false;
    u->legacy = false;
}

static void user_copy(AxUser* dst, const AxUser* src)
{
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < sizeof(AxUser); ++i) {
        d[i] = s[i];
    }
}

static uint32_t user_fnv(const char* s)
{
    uint32_t h = 0x811c9dc5u;
    if (!s) {
        return h;
    }
    for (const char* p = s; *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 0x01000193u;
    }
    return h;
}

static void user_put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t user_get32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void user_bytes_zero(void* dst, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < n; ++i) {
        p[i] = 0;
    }
}

static void user_bytes_copy(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

/* ------------------------------------------------------------------ */
/* Registro cifrado                                                    */
/* ------------------------------------------------------------------ */
struct UserMeta {
    uint8_t name[AX_USER_MAX_NAME];
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t iterations;
    uint8_t salt[AX_USER_SALT_LEN];
    uint8_t nonce[AX_USER_NONCE_LEN];
    uint8_t ct[AX_USER_SECRET_LEN];
};

static void user_meta_serialize(const UserMeta& m, uint8_t* out)
{
    user_bytes_zero(out, USER_RECORD_SIZE);
    user_bytes_copy(out, m.name, AX_USER_MAX_NAME);
    user_put32(out + 32, m.uid);
    user_put32(out + 36, m.gid);
    user_put32(out + 40, m.flags);
    user_put32(out + 44, m.iterations);
    user_bytes_copy(out + 48, m.salt, AX_USER_SALT_LEN);
    user_bytes_copy(out + 64, m.nonce, AX_USER_NONCE_LEN);
    user_put32(out + 76, AX_USER_SECRET_LEN);
    user_bytes_copy(out + 80, m.ct, AX_USER_SECRET_LEN);
}

static void user_meta_from_record(const uint8_t* rec, UserMeta& m)
{
    user_bytes_copy(m.name, rec, AX_USER_MAX_NAME);
    m.uid = user_get32(rec + 32);
    m.gid = user_get32(rec + 36);
    m.flags = user_get32(rec + 40);
    m.iterations = user_get32(rec + 44);
    user_bytes_copy(m.salt, rec + 48, AX_USER_SALT_LEN);
    user_bytes_copy(m.nonce, rec + 64, AX_USER_NONCE_LEN);
    user_bytes_copy(m.ct, rec + 80, AX_USER_SECRET_LEN);
}

static void user_derive_key(const char* password, const uint8_t* salt,
                            uint32_t iterations, uint8_t key[32])
{
    ax_pbkdf2_sha256(password ? password : "", (uint64_t)user_strlen(password),
                     salt, AX_USER_SALT_LEN,
                     iterations ? iterations : AX_USER_ITERATIONS,
                     key, 32);
}

static void user_hmac_record(const uint8_t key[32], const UserMeta& m,
                             uint8_t tag[AX_USER_TAG_LEN])
{
    uint8_t blob[USER_RECORD_SIZE];
    user_meta_serialize(m, blob);
    ax_hmac_sha256(key, 32, blob, 144, tag);
    user_bytes_zero(blob, sizeof(blob));
}

static void user_fill_record(AxUser* u, const char* username,
                             const char* password, uint32_t uid, uint32_t gid,
                             bool admin, bool legacy)
{
    user_clear(u);
    user_copy_name(u->username, username);
    u->uid = uid;
    u->gid = gid;
    u->active = true;
    u->is_admin = admin;
    u->perms = admin ? AX_PERM_ALL : (AX_PERM_READ | AX_PERM_EXEC);
    u->legacy = legacy;
    u->iterations = AX_USER_ITERATIONS;

    if (legacy) {
        u->legacy_hash = user_fnv(password);
        for (int i = 0; i < 4; ++i) {
            u->password[i] = (uint8_t)(u->legacy_hash >> (i * 8));
        }
        return;
    }

    uint8_t key[32];
    uint8_t secret[AX_USER_SECRET_LEN];
    UserMeta m;
    user_bytes_zero(&m, sizeof(m));
    user_bytes_copy(m.name, u->username, AX_USER_MAX_NAME);
    m.uid = uid;
    m.gid = gid;
    m.flags = USER_FLAG_ACTIVE | (admin ? USER_FLAG_ADMIN : 0u);
    m.iterations = AX_USER_ITERATIONS;
    ax_random_bytes(u->salt, AX_USER_SALT_LEN);
    ax_random_bytes(u->nonce, AX_USER_NONCE_LEN);
    user_derive_key(password, u->salt, u->iterations, key);

    user_bytes_zero(secret, sizeof(secret));
    user_bytes_copy(secret, u->username, AX_USER_MAX_NAME);
    ax_hmac_sha256(key, 32, u->username, AX_USER_MAX_NAME, secret + 32);

    user_bytes_copy(m.salt, u->salt, AX_USER_SALT_LEN);
    user_bytes_copy(m.nonce, u->nonce, AX_USER_NONCE_LEN);
    ax_chacha20_xor(key, u->nonce, 1, secret, m.ct, AX_USER_SECRET_LEN);
    user_hmac_record(key, m, u->tag);
    user_bytes_copy(u->password, m.ct, AX_USER_SECRET_LEN);

    user_bytes_zero(key, sizeof(key));
    user_bytes_zero(secret, sizeof(secret));
    user_bytes_zero(&m, sizeof(m));
}

static int user_check_password(const AxUser* u, const char* password)
{
    if (!u || !u->active || !password) {
        return -1;
    }
    if (u->legacy) {
        return (user_fnv(password) == u->legacy_hash) ? 0 : -1;
    }

    uint8_t key[32];
    uint8_t secret[AX_USER_SECRET_LEN];
    uint8_t tag[AX_USER_TAG_LEN];
    UserMeta m;
    user_bytes_zero(&m, sizeof(m));
    user_bytes_copy(m.name, u->username, AX_USER_MAX_NAME);
    m.uid = u->uid;
    m.gid = u->gid;
    m.flags = USER_FLAG_ACTIVE | (u->is_admin ? USER_FLAG_ADMIN : 0u);
    m.iterations = u->iterations;
    user_bytes_copy(m.salt, u->salt, AX_USER_SALT_LEN);
    user_bytes_copy(m.nonce, u->nonce, AX_USER_NONCE_LEN);
    user_bytes_copy(m.ct, u->password, AX_USER_SECRET_LEN);
    m.flags = USER_FLAG_ACTIVE | (u->is_admin ? USER_FLAG_ADMIN : 0u);

    user_derive_key(password, u->salt, u->iterations, key);
    user_hmac_record(key, m, tag);
    if (!ax_crypto_equal(tag, u->tag, AX_USER_TAG_LEN)) {
        user_bytes_zero(key, sizeof(key));
        return -1;
    }
    ax_chacha20_xor(key, u->nonce, 1, m.ct, secret, AX_USER_SECRET_LEN);
    bool ok = ax_crypto_equal(secret, u->username, AX_USER_MAX_NAME);
    user_bytes_zero(key, sizeof(key));
    user_bytes_zero(secret, sizeof(secret));
    user_bytes_zero(tag, sizeof(tag));
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Base de datos                                                       */
/* ------------------------------------------------------------------ */
static int user_find(const char* username)
{
    if (!username) {
        return -1;
    }
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (g_users[i].active && user_streq(g_users[i].username, username)) {
            return i;
        }
    }
    return -1;
}

static int user_free_slot(bool include_root)
{
    int start = include_root ? 0 : 1;
    for (int i = start; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            return i;
        }
    }
    return -1;
}

static void user_serialize_header(uint8_t* out, uint32_t count)
{
    user_bytes_zero(out, USER_HEADER_SIZE);
    user_bytes_copy(out, AX_USER_DB_MAGIC, 8);
    user_put32(out + 8, AX_USER_DB_VERSION);
    user_put32(out + 12, count);
    user_put32(out + 16, AX_USER_ITERATIONS);
    user_put32(out + 20, 0);
    ax_random_bytes(out + 24, AX_USER_SALT_LEN);
}

static void user_serialize_record(const AxUser* u, uint8_t* out)
{
    UserMeta m;
    user_bytes_zero(&m, sizeof(m));
    user_bytes_copy(m.name, u->username, AX_USER_MAX_NAME);
    m.uid = u->uid;
    m.gid = u->gid;
    m.flags = USER_FLAG_ACTIVE | (u->is_admin ? USER_FLAG_ADMIN : 0u) |
              (u->legacy ? USER_FLAG_LEGACY : 0u);
    m.iterations = u->iterations;
    user_bytes_copy(m.salt, u->salt, AX_USER_SALT_LEN);
    user_bytes_copy(m.nonce, u->nonce, AX_USER_NONCE_LEN);
    if (u->legacy) {
        user_bytes_zero(m.ct, AX_USER_SECRET_LEN);
        user_put32(m.ct, u->legacy_hash);
    } else {
        user_bytes_copy(m.ct, u->password, AX_USER_SECRET_LEN);
    }
    user_meta_serialize(m, out);

    if (!u->legacy) {
        user_bytes_copy(out + 144, u->tag, AX_USER_TAG_LEN);
    } else {
        /* Etiqueta legacy: los 4 bytes del hash FNV. */
        user_bytes_copy(out + 144, u->password, 4);
    }
}

int ax_user_save(void)
{
    if (!ax_fs_disk_mounted()) {
        return -1;
    }

    uint8_t blob[USER_DB_MAX];
    uint32_t count = 0;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (g_users[i].active) {
            ++count;
        }
    }
    user_serialize_header(blob, count);

    uint32_t off = USER_HEADER_SIZE;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            continue;
        }
        user_serialize_record(&g_users[i], blob + off);
        off += USER_RECORD_SIZE;
    }

    (void)ax_fs_disk_mkdir("/etc");
    int rc = ax_fs_disk_write(USER_DB_PATH, blob, off);
    if (rc == 0) {
        ax_printf("[userdb] %u cuenta(s) cifradas guardadas (%u bytes)\n",
                  (unsigned)count, (unsigned)off);
    } else {
        ax_printf("[userdb] error guardando %s (%d)\n", USER_DB_PATH, rc);
    }
    user_bytes_zero(blob, sizeof(blob));
    return rc == 0 ? 0 : -1;
}

static bool user_parse_record(const uint8_t* rec)
{
    UserMeta m;
    user_meta_from_record(rec, m);

    /* El nombre visible debe ser una cadena valida. */
    char name[AX_USER_MAX_NAME];
    user_bytes_copy(name, m.name, AX_USER_MAX_NAME);
    name[AX_USER_MAX_NAME - 1] = '\0';
    if (!name[0]) {
        return false;
    }
    if (user_find(name) >= 0) {
        return false;
    }

    int slot = user_streq(name, "root") ? 0 : user_free_slot(false);
    if (slot < 0) {
        return false;
    }

    AxUser* u = &g_users[slot];
    user_clear(u);
    user_copy_name(u->username, name);
    u->uid = m.uid;
    u->gid = m.gid;
    u->is_admin = (m.flags & USER_FLAG_ADMIN) != 0;
    u->legacy = (m.flags & USER_FLAG_LEGACY) != 0;
    u->iterations = m.iterations ? m.iterations : AX_USER_ITERATIONS;
    user_bytes_copy(u->salt, m.salt, AX_USER_SALT_LEN);
    user_bytes_copy(u->nonce, m.nonce, AX_USER_NONCE_LEN);
    if (u->legacy) {
        u->legacy_hash = user_get32(m.ct);
        user_bytes_copy(u->password, m.ct, 4);
    } else {
        user_bytes_copy(u->password, m.ct, AX_USER_SECRET_LEN);
        user_bytes_copy(u->tag, rec + 144, AX_USER_TAG_LEN);
    }
    u->active = true;
    u->perms = u->is_admin ? AX_PERM_ALL : (AX_PERM_READ | AX_PERM_EXEC);
    return true;
}

static void user_load_encrypted(const uint8_t* blob, uint64_t size)
{
    if (size < USER_HEADER_SIZE ||
        !ax_crypto_equal(blob, AX_USER_DB_MAGIC, 8)) {
        return;
    }
    uint32_t version = user_get32(blob + 8);
    uint32_t count = user_get32(blob + 12);
    if (version != AX_USER_DB_VERSION || count > AX_USER_MAX_USERS) {
        return;
    }
    uint64_t off = USER_HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + USER_RECORD_SIZE > size) {
            break;
        }
        (void)user_parse_record(blob + off);
        off += USER_RECORD_SIZE;
    }
    g_db_loaded = true;
}

static void user_load_legacy(const uint8_t* blob, uint64_t size)
{
    /* Formato antiguo: nombre:uid:gid:admin:HHHHHHHH\n (FNV-1a). */
    uint64_t pos = 0;
    while (pos < size) {
        uint64_t end = pos;
        while (end < size && blob[end] != '\n') {
            ++end;
        }
        if (end > pos) {
            char line[256];
            uint64_t len = end - pos;
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            for (uint64_t i = 0; i < len; ++i) {
                line[i] = (char)blob[pos + i];
            }
            line[len] = '\0';

            char* c1 = nullptr;
            char* c2 = nullptr;
            char* c3 = nullptr;
            char* c4 = nullptr;
            for (char* p = line; *p; ++p) {
                if (*p != ':') {
                    continue;
                }
                if (!c1) c1 = p;
                else if (!c2) c2 = p;
                else if (!c3) c3 = p;
                else if (!c4) c4 = p;
            }
            if (c1 && c2 && c3 && c4 && c1 != line) {
                *c1 = *c2 = *c3 = *c4 = '\0';
                const char* name = line;
                uint32_t uid = 1000;
                uint32_t gid = 1000;
                char* p = c1 + 1;
                uid = 0;
                while (*p >= '0' && *p <= '9') uid = uid * 10 + (uint32_t)(*p++ - '0');
                p = c2 + 1;
                gid = 0;
                while (*p >= '0' && *p <= '9') gid = gid * 10 + (uint32_t)(*p++ - '0');
                bool admin = (*(c3 + 1) == '1');

                if (!user_streq(name, "root") && user_find(name) < 0) {
                    int slot = user_free_slot(false);
                    if (slot >= 0) {
                        AxUser* u = &g_users[slot];
                        user_fill_record(u, name, "", uid, gid, admin, true);
                        /* Lee el hash FNV little-endian del campo antiguo. */
                        uint32_t hash = 0;
                        const char* h = c4 + 1;
                        for (int k = 0; k < 4 && h[0] && h[1]; ++k) {
                            int hi = (h[0] >= '0' && h[0] <= '9') ? h[0] - '0' :
                                     (h[0] >= 'a' && h[0] <= 'f') ? h[0] - 'a' + 10 :
                                     (h[0] >= 'A' && h[0] <= 'F') ? h[0] - 'A' + 10 : 0;
                            int lo = (h[1] >= '0' && h[1] <= '9') ? h[1] - '0' :
                                     (h[1] >= 'a' && h[1] <= 'f') ? h[1] - 'a' + 10 :
                                     (h[1] >= 'A' && h[1] <= 'F') ? h[1] - 'A' + 10 : 0;
                            hash |= (uint32_t)((hi << 4) | lo) << (k * 8);
                            h += 2;
                        }
                        u->legacy_hash = hash;
                        user_bytes_copy(u->password, &hash, 4);
                    }
                }
            }
        }
        pos = end + 1;
    }
    g_db_loaded = true;
}

static int user_read_file(const char* path, uint8_t* buf, uint64_t max,
                          uint64_t* out)
{
    uint64_t got = 0;
    int rc = ax_fs_disk_read(path, buf, max, &got);
    if (out) {
        *out = got;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* API publica                                                        */
/* ------------------------------------------------------------------ */
void ax_user_init(void)
{
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        user_clear(&g_users[i]);
    }
    user_fill_record(&g_users[0], "root", "root", AX_USER_ROOT_UID, 0,
                     true, false);
    g_current_user = -1;
    g_db_loaded = false;
    ax_crypto_init();
    ax_printf("[userdb] gestor cifrado inicializado (PBKDF2-SHA256 + ChaCha20)\n");
}

int ax_user_login(const char* username, const char* password)
{
    int slot = user_find(username);
    if (slot < 0 || user_check_password(&g_users[slot], password) != 0) {
        return -1;
    }

    /* Migracion transparente de una cuenta antigua (FNV-1a). */
    if (g_users[slot].legacy) {
        AxUser upgraded;
        user_fill_record(&upgraded, g_users[slot].username, password,
                         g_users[slot].uid, g_users[slot].gid,
                         g_users[slot].is_admin, false);
        user_copy(&g_users[slot], &upgraded);
        (void)ax_user_save();
        ax_printf("[userdb] cuenta '%s' migrada a cifrado v2\n",
                  g_users[slot].username);
    }

    g_current_user = slot;
    ax_printf("[userdb] sesion iniciada como %s (uid=%u)\n",
              g_users[slot].username, (unsigned)g_users[slot].uid);
    return 0;
}

int ax_user_logout(void)
{
    g_current_user = -1;
    ax_printf("[userdb] sesion cerrada\n");
    return 0;
}

int ax_user_add(const char* username, const char* password, bool is_admin)
{
    if (g_current_user < 0 || !g_users[g_current_user].is_admin) {
        return -1;
    }
    if (!username || !username[0] || user_strlen(username) >= AX_USER_MAX_NAME) {
        return -1;
    }
    if (user_find(username) >= 0) {
        return -1;
    }
    int slot = user_free_slot(false);
    if (slot < 0) {
        return -1;
    }
    user_fill_record(&g_users[slot], username, password,
                     1000u + (uint32_t)slot, 1000u + (uint32_t)slot,
                     is_admin, false);
    ax_printf("[userdb] creada cuenta cifrada %s\n", g_users[slot].username);
    (void)ax_user_save();
    return 0;
}

int ax_user_del(const char* username)
{
    if (g_current_user < 0 || !g_users[g_current_user].is_admin) {
        return -1;
    }
    if (!username || user_streq(username, "root") ||
        (g_current_user >= 0 && user_streq(username, g_users[g_current_user].username))) {
        return -1;
    }
    int slot = user_find(username);
    if (slot < 0) {
        return -1;
    }
    user_clear(&g_users[slot]);
    (void)ax_user_save();
    return 0;
}

int ax_user_list(char* out, uint64_t max)
{
    if (!out || max == 0) {
        return -1;
    }
    uint64_t pos = 0;
    const char* hdr = "USUARIOS (base cifrada):\n";
    for (const char* p = hdr; *p && pos + 1 < max; ++p) {
        out[pos++] = *p;
    }
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            continue;
        }
        if (pos + 1 < max) out[pos++] = ' ';
        for (const char* p = g_users[i].username; *p && pos + 1 < max; ++p) {
            out[pos++] = *p;
        }
        if (g_users[i].is_admin && pos + 7 < max) {
            const char* tag = " (admin)";
            for (const char* p = tag; *p; ++p) out[pos++] = *p;
        }
        if (i == g_current_user && pos + 3 < max) {
            out[pos++] = ' ';
            out[pos++] = '*';
        }
        if (pos + 1 < max) out[pos++] = '\n';
    }
    out[pos] = '\0';
    return (int)pos;
}

int ax_user_count(void)
{
    int c = 0;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (g_users[i].active) ++c;
    }
    return c;
}

AxUser* ax_user_current(void)
{
    if (g_current_user < 0 || g_current_user >= AX_USER_MAX_USERS) {
        return nullptr;
    }
    if (!g_users[g_current_user].active) {
        return nullptr;
    }
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
    return u ? (u->is_admin || u->uid == AX_USER_ROOT_UID) : false;
}

bool ax_user_check_perm(const char* path, uint32_t need_perm)
{
    (void)path;
    AxUser* u = ax_user_current();
    if (!u) return false;
    if (u->is_admin) return true;
    return (u->perms & need_perm) == need_perm;
}

void ax_user_boot(void)
{
    g_db_loaded = false;
    if (!ax_fs_disk_mounted()) {
        return;
    }

    uint8_t blob[USER_DB_MAX];
    uint64_t size = 0;
    uint32_t type = 0;
    uint64_t fsize = 0;

    if (ax_fs_disk_stat(USER_DB_PATH, &type, &fsize) == 0 &&
        type == AXFS_DISK_TYPE_FILE &&
        user_read_file(USER_DB_PATH, blob, sizeof(blob), &size) == 0 &&
        size >= USER_HEADER_SIZE) {
        user_load_encrypted(blob, size);
        if (g_db_loaded) {
            ax_printf("[userdb] %d cuenta(s) cargadas desde %s\n",
                      ax_user_count(), USER_DB_PATH);
            return;
        }
    }

    /* Migracion del formato antiguo /etc/passwd si existe. */
    if (ax_fs_disk_stat(USER_OLD_DB_PATH, &type, &fsize) == 0 &&
        type == AXFS_DISK_TYPE_FILE &&
        user_read_file(USER_OLD_DB_PATH, blob, sizeof(blob), &size) == 0 &&
        size > 0) {
        user_load_legacy(blob, size);
        ax_printf("[userdb] migrando %s al formato cifrado\n", USER_OLD_DB_PATH);
        (void)ax_user_save();
        return;
    }

    ax_printf("[userdb] no hay base de usuarios: se pedira crear la primera cuenta\n");
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
    if (user_find(username) >= 0) {
        return -1;
    }

    int slot = user_free_slot(false);
    if (slot < 0) {
        return -1;
    }
    user_fill_record(&g_users[slot], username, password,
                     1000u + (uint32_t)slot, 1000u + (uint32_t)slot,
                     true, false);
    g_current_user = slot;
    g_db_loaded = true;
    if (ax_fs_disk_mounted()) {
        (void)ax_user_save();
    }
    ax_printf("[userdb] cuenta '%s' creada y cifrada\n", g_users[slot].username);
    return 0;
}

int ax_user_change_password(const char* username, const char* old_password,
                            const char* new_password)
{
    int slot = user_find(username ? username : "");
    if (slot < 0 || !new_password || !new_password[0]) {
        return -1;
    }
    if (user_check_password(&g_users[slot], old_password) != 0) {
        return -2;
    }
    AxUser upgraded;
    user_fill_record(&upgraded, g_users[slot].username, new_password,
                     g_users[slot].uid, g_users[slot].gid,
                     g_users[slot].is_admin, false);
    user_copy(&g_users[slot], &upgraded);
    int rc = ax_user_save();
    ax_printf("[userdb] contrasena de '%s' actualizada\n",
              g_users[slot].username);
    return rc;
}

int ax_user_verify(const char* username, const char* password)
{
    int slot = user_find(username);
    if (slot < 0) {
        return -1;
    }
    return user_check_password(&g_users[slot], password);
}

int ax_user_get_info(uint32_t index, AxUserInfoK* out)
{
    if (!out) {
        return -1;
    }
    uint32_t seen = 0;
    for (int i = 0; i < AX_USER_MAX_USERS; ++i) {
        if (!g_users[i].active) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        user_bytes_zero(out, sizeof(*out));
        out->uid = g_users[i].uid;
        out->gid = g_users[i].gid;
        out->flags = (g_users[i].is_admin ? 1u : 0u) |
                     (i == g_current_user ? 2u : 0u);
        user_copy_name(out->name, g_users[i].username);
        return 0;
    }
    return -1;
}

uint32_t ax_user_uid_for_name(const char* username)
{
    int slot = user_find(username);
    return slot >= 0 ? g_users[slot].uid : 0xFFFFFFFFu;
}

bool ax_user_is_admin_name(const char* username)
{
    int slot = user_find(username);
    return slot >= 0 && g_users[slot].is_admin;
}
