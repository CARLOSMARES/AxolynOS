/*
 * AxolynOS - Archivos de sistema protegidos (.axsys).
 *
 * El formato y la API estan documentados en Kernel/include/ax_sysfile.h y
 * en manuales/15-axsys.md.
 *
 * El contenido de un .axsys queda en memoria del kernel: las aplicaciones
 * de user mode solo pueden leer los metadatos y la firma a traves de las
 * syscalls AX_SYS_SYSFILE_*.
 */

#include "ax_types.h"
#include "ax_sysfile.h"
#include "ax_pkg.h"
#include "ax_crypto.h"
#include "ax_axfs.h"
#include "ax_fs_disk.h"
#include "ax_memory.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Tabla de ficheros cargados                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    bool          used;
    AxSysFileInfo info;
    AxSysFileMeta meta[AX_SYSFILE_MAX_META];
    uint8_t       signature[AX_SYSFILE_SIGN_SIZE];
    uint8_t*      content;      /* copia en memoria del kernel (opaca) */
} ax_sysfile_entry;

static ax_sysfile_entry g_files[AX_SYSFILE_MAX];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void sf_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void sf_memset(void* dst, int value, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = (uint8_t)value;
    }
}

static uint64_t sf_strlen(const char* s)
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

static void sf_strlcpy(char* dst, const char* src, uint64_t max)
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

static char sf_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/* Comparacion de claves de metadato: no distingue mayusculas. */
static bool sf_key_eq(const char* a, const char* b)
{
    uint64_t i = 0;
    while (a[i] && b[i]) {
        if (sf_lower(a[i]) != sf_lower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == b[i];
}

static bool sf_suffix(const char* s, const char* suffix)
{
    uint64_t ls = sf_strlen(s);
    uint64_t lf = sf_strlen(suffix);
    if (lf > ls) {
        return false;
    }
    return sf_key_eq(s + ls - lf, suffix);
}

/* ------------------------------------------------------------------ */
/* Lectura de un fichero                                               */
/* ------------------------------------------------------------------ */
/*
 * Lee un fichero completo.  Si "from_disk" es false se intenta primero el
 * VFS (initrd) y luego AxFS-D.  Devuelve un puntero (del VFS o asignado con
 * ax_malloc) y su tamano, o 0 si no existe.  El llamante debe liberar el
 * buffer si "owned" es true.
 */
static const uint8_t* sf_read_file(const char* path, uint64_t* size_out,
                                   bool* owned)
{
    *owned = false;

    uint64_t vsize = 0;
    const void* vdata = ax_vfs_read(path, &vsize);
    if (vdata && vsize) {
        *size_out = vsize;
        return (const uint8_t*)vdata;
    }

    if (!ax_fs_disk_mounted()) {
        return nullptr;
    }

    uint32_t type = 0;
    uint64_t dsize = 0;
    if (ax_fs_disk_stat(path, &type, &dsize) != 0 ||
        type != AXFS_DISK_TYPE_FILE) {
        return nullptr;
    }
    if (dsize == 0) {
        *size_out = 0;
        return (const uint8_t*)"";
    }

    uint8_t* buf = (uint8_t*)ax_malloc(dsize);
    if (!buf) {
        return nullptr;
    }
    uint64_t got = 0;
    if (ax_fs_disk_read(path, buf, dsize, &got) != 0 || got != dsize) {
        ax_free(buf);
        return nullptr;
    }
    *size_out = dsize;
    *owned    = true;
    return buf;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_sysfile_init(void)
{
    for (int i = 0; i < AX_SYSFILE_MAX; ++i) {
        g_files[i].used    = false;
        g_files[i].content = nullptr;
        sf_memset(&g_files[i].info, 0, sizeof(g_files[i].info));
        sf_memset(g_files[i].meta, 0, sizeof(g_files[i].meta));
        sf_memset(g_files[i].signature, 0, sizeof(g_files[i].signature));
    }
}

int ax_sysfile_load(const char* path)
{
    if (!path || !path[0]) {
        return AX_EINVAL;
    }

    uint64_t size  = 0;
    bool     owned = false;
    const uint8_t* data = sf_read_file(path, &size, &owned);
    if (!data) {
        return AX_ENOENT;
    }

    int result = AX_EINVAL;
    AxSysFileHeader hdr;
    const uint8_t* meta_ptr = nullptr;
    const uint8_t* sign_ptr = nullptr;
    const uint8_t* content  = nullptr;
    uint8_t  sig[AX_SYSFILE_SIGN_SIZE];
    bool     sig_ok = false;
    int      slot   = -1;
    uint8_t* copy   = nullptr;

    if (size < sizeof(AxSysFileHeader)) {
        result = AX_EINVAL;
        goto done;
    }

    sf_memcpy(&hdr, data, sizeof(hdr));

    if (hdr.magic[0] != 'A' || hdr.magic[1] != 'X' || hdr.magic[2] != 'S' ||
        hdr.magic[3] != 'Y' || hdr.magic[4] != 'S') {
        result = AX_EINVAL;
        goto done;
    }
    if (hdr.version != AX_SYSFILE_VERSION) {
        ax_printf("[sysfile] %s: version no soportada (%u)\n",
                  path, (unsigned)hdr.version);
        result = AX_EINVAL;
        goto done;
    }

    /* CRC de la cabecera: se calcula con el campo a cero. */
    {
        AxSysFileHeader tmp = hdr;
        tmp.header_crc32 = 0;
        uint32_t hcrc = ax_pkg_crc32(&tmp, sizeof(tmp));
        if ((uint64_t)hcrc != hdr.header_crc32) {
            ax_printf("[sysfile] %s: CRC de cabecera incorrecto\n", path);
            result = AX_EINVAL;
            goto done;
        }
    }

    if (hdr.meta_count > AX_SYSFILE_MAX_META) {
        ax_printf("[sysfile] %s: demasiados metadatos (%u)\n",
                  path, (unsigned)hdr.meta_count);
        result = AX_EINVAL;
        goto done;
    }

    {
        uint64_t need = sizeof(AxSysFileHeader) +
                        (uint64_t)hdr.meta_count * sizeof(AxSysFileMeta) +
                        AX_SYSFILE_SIGN_SIZE + hdr.content_size;
        if (need > size) {
            ax_printf("[sysfile] %s: fichero truncado (%llu < %llu)\n",
                      path, (unsigned long long)size, (unsigned long long)need);
            result = AX_EINVAL;
            goto done;
        }
    }

    meta_ptr = data + sizeof(AxSysFileHeader);
    sign_ptr = meta_ptr + (uint64_t)hdr.meta_count * sizeof(AxSysFileMeta);
    content  = sign_ptr + AX_SYSFILE_SIGN_SIZE;

    /* CRC del contenido. */
    {
        uint32_t crc = ax_pkg_crc32(content, (uint32_t)hdr.content_size);
        if ((uint64_t)crc != hdr.content_crc32) {
            ax_printf("[sysfile] %s: CRC de contenido incorrecto\n", path);
            result = AX_EINVAL;
            goto done;
        }
    }

    /* Firma SHA-256 del contenido. */
    ax_sha256(content, hdr.content_size, sig);
    sig_ok = ax_crypto_equal(sig, sign_ptr, AX_SYSFILE_SIGN_SIZE);
    if ((hdr.flags & AX_SYSFILE_FLAG_SIGNED) && !sig_ok) {
        ax_printf("[sysfile] %s: firma incorrecta\n", path);
        result = AX_EINVAL;
        goto done;
    }

    /* Busca un hueco en la tabla. */
    for (int i = 0; i < AX_SYSFILE_MAX; ++i) {
        if (!g_files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        result = AX_ENOSPC;
        goto done;
    }

    {
        ax_sysfile_entry* e = &g_files[slot];
        sf_memset(e, 0, sizeof(*e));
        e->used = true;
        e->info.index        = (uint32_t)slot;
        e->info.flags        = hdr.flags;
        e->info.content_type = hdr.content_type;
        e->info.content_size = hdr.content_size;
        e->info.content_crc32 = hdr.content_crc32;
        e->info.meta_count   = hdr.meta_count;
        e->info.signature_ok = sig_ok ? 1u : 0u;
        sf_strlcpy(e->info.name, hdr.name, sizeof(e->info.name));
        sf_strlcpy(e->info.product, hdr.product, sizeof(e->info.product));
        sf_strlcpy(e->info.manufacturer, hdr.manufacturer, sizeof(e->info.manufacturer));
        sf_strlcpy(e->info.description, hdr.description, sizeof(e->info.description));

        for (uint32_t i = 0; i < hdr.meta_count; ++i) {
            sf_memcpy(&e->meta[i], meta_ptr + i * sizeof(AxSysFileMeta),
                      sizeof(AxSysFileMeta));
            e->meta[i].key[AX_SYSFILE_META_KEY - 1]     = '\0';
            e->meta[i].value[AX_SYSFILE_META_VALUE - 1] = '\0';
        }
        sf_memcpy(e->signature, sign_ptr, AX_SYSFILE_SIGN_SIZE);

        /* El contenido queda en memoria del kernel (opaco para user mode). */
        if (hdr.content_size > 0) {
            copy = (uint8_t*)ax_malloc(hdr.content_size);
            if (!copy) {
                e->used = false;
                result  = AX_ENOMEM;
                goto done;
            }
            sf_memcpy(copy, content, hdr.content_size);
            e->content = copy;
            copy = nullptr;
        }
    }

    ax_printf("[sysfile] '%s' cargado (%s, %llu bytes, firma %s)\n",
              hdr.name, hdr.manufacturer,
              (unsigned long long)hdr.content_size,
              sig_ok ? "valida" : "sin verificar");
    result = slot;

done:
    if (copy) {
        ax_free(copy);
    }
    if (owned && data) {
        ax_free((void*)data);
    }
    return result;
}

int ax_sysfile_unload(uint32_t index)
{
    if (index >= AX_SYSFILE_MAX || !g_files[index].used) {
        return AX_EINVAL;
    }
    if (g_files[index].content) {
        ax_free(g_files[index].content);
    }
    g_files[index].used    = false;
    g_files[index].content = nullptr;
    return 0;
}

int ax_sysfile_count(void)
{
    int n = 0;
    for (int i = 0; i < AX_SYSFILE_MAX; ++i) {
        if (g_files[i].used) {
            n++;
        }
    }
    return n;
}

int ax_sysfile_info(uint32_t index, AxSysFileInfo* out)
{
    if (!out || index >= AX_SYSFILE_MAX || !g_files[index].used) {
        return AX_ENOENT;
    }
    sf_memcpy(out, &g_files[index].info, sizeof(*out));
    return 0;
}

int ax_sysfile_meta(uint32_t index, const char* key, char* out, uint64_t max)
{
    if (!out || max == 0 || !key) {
        return AX_EINVAL;
    }
    if (index >= AX_SYSFILE_MAX || !g_files[index].used) {
        return AX_ENOENT;
    }
    for (uint32_t i = 0; i < g_files[index].info.meta_count; ++i) {
        if (sf_key_eq(g_files[index].meta[i].key, key)) {
            sf_strlcpy(out, g_files[index].meta[i].value, max);
            return 0;
        }
    }
    return AX_ENOENT;
}

int ax_sysfile_meta_at(uint32_t index, uint32_t meta_index,
                       char* key, uint64_t key_max,
                       char* value, uint64_t value_max)
{
    if (!key || !value || key_max == 0 || value_max == 0) {
        return AX_EINVAL;
    }
    if (index >= AX_SYSFILE_MAX || !g_files[index].used) {
        return AX_ENOENT;
    }
    ax_sysfile_entry* e = &g_files[index];
    if (meta_index >= e->info.meta_count) {
        return AX_ENOENT;
    }
    sf_strlcpy(key, e->meta[meta_index].key, key_max);
    sf_strlcpy(value, e->meta[meta_index].value, value_max);
    return 0;
}

int ax_sysfile_list(char* out, uint64_t max)
{
    if (!out || max == 0) {
        return AX_EINVAL;
    }
    uint64_t used = 0;
    for (int i = 0; i < AX_SYSFILE_MAX; ++i) {
        if (!g_files[i].used) {
            continue;
        }
        char line[160];
        uint64_t n = 0;
        const char* fields[4] = {
            g_files[i].info.name,
            g_files[i].info.product,
            g_files[i].info.manufacturer,
            g_files[i].info.description
        };
        for (int f = 0; f < 4; ++f) {
            if (f) {
                line[n++] = ' ';
            }
            for (uint64_t k = 0; fields[f][k] && n + 1 < sizeof(line); ++k) {
                line[n++] = fields[f][k];
            }
        }
        line[n++] = '\n';
        if (used + n + 1 > max) {
            break;
        }
        sf_memcpy(out + used, line, n);
        used += n;
    }
    out[used] = '\0';
    return (int)used;
}

void ax_sysfile_autoload(void)
{
    uint32_t total = ax_vfs_count();
    int loaded = 0;

    for (uint32_t i = 0; i < total; ++i) {
        const char* name = ax_vfs_name(i);
        if (!name) {
            continue;
        }
        /* Solo los ficheros de /system/ con extension .axsys. */
        if (!sf_suffix(name, ".axsys")) {
            continue;
        }
        bool in_system = false;
        const char* p = name;
        while (*p) {
            if (p[0] == 's' && p[1] == 'y' && p[2] == 's' && p[3] == 't' &&
                p[4] == 'e' && p[5] == 'm' && p[6] == '/') {
                in_system = true;
                break;
            }
            p++;
        }
        if (!in_system) {
            continue;
        }

        char path[160];
        path[0] = '/';
        sf_strlcpy(path + 1, name, sizeof(path) - 1);

        int rc = ax_sysfile_load(path);
        if (rc >= 0) {
            loaded++;
        } else {
            ax_printf("[sysfile] no se pudo cargar '%s' (%d)\n", path, rc);
        }
    }

    if (loaded > 0) {
        ax_printf("[sysfile] %d archivo(s) de sistema cargado(s) desde el initrd\n",
                  loaded);
    }
}
