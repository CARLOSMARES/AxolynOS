/*
 * AxolynOS - Sistema de ficheros en memoria (AXFS).
 *
 * El initramfs lo genera la herramienta de host scripts/tools/mkinitramfs.cpp
 * y el bootloader lo carga en AX_INITRD_PHYS.  Aqui solo se lee: el AXFS es
 * de solo lectura, asi que ax_vfs_read() devuelve un puntero directo a los
 * bytes dentro de la imagen.
 *
 * Formato (ver Kernel/include/ax_axfs.h):
 *
 *   [AxfsHeader][tabla de AxfsEntry][blob de datos]
 *
 * Los nombres de las entradas son rutas relativas a la raiz, sin '/' inicial
 * ("apps/hello.elf").  Los directorios llevan una '/' final ("apps/").
 *
 * Documentacion: manuales/06-filesystem.md
 */

#include "ax_types.h"
#include "ax_axfs.h"
#include "ax_console.h"
#include "ax_kernel.h"

/* ------------------------------------------------------------------ */
/* Estado del VFS                                                      */
/* ------------------------------------------------------------------ */
static const AxfsHeader* g_header  = nullptr;
static const AxfsEntry*  g_entries  = nullptr;
static const uint8_t*    g_data     = nullptr;
static uint32_t          g_count    = 0;
static bool              g_ready    = false;

/* ------------------------------------------------------------------ */
/* Utilidades internas                                                 */
/* ------------------------------------------------------------------ */
static uint64_t ax_strlen(const char* s)
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

static int ax_streq(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

static void ax_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

/*
 * Normaliza una ruta: quita la '/' inicial, colapsa las barras repetidas y
 * resuelve "." y "..".  El resultado se escribe en "out" (debe caber en
 * AXFS_NAME_MAX bytes) y devuelve true si la ruta es valida.
 */
static bool ax_path_normalize(const char* path, char* out, uint64_t max)
{
    if (!path || !out || max < 2) {
        return false;
    }

    /* Pila de componentes: cada uno es un intervalo [inicio, fin). */
    const char* comp[64];
    uint64_t    len[64];
    uint32_t    depth = 0;
    uint64_t    i = 0;

    while (path[i] && i < 4096) {
        uint64_t start;

        while (path[i] == '/') {
            i++;
        }
        if (!path[i]) {
            break;
        }
        start = i;
        while (path[i] && path[i] != '/') {
            i++;
        }

        uint64_t clen = i - start;

        if (clen == 1 && path[start] == '.') {
            continue;
        }
        if (clen == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (depth > 0) {
                depth--;
            }
            continue;
        }
        if (depth >= AX_ARRAY_SIZE(comp)) {
            return false;
        }
        comp[depth] = path + start;
        len[depth]  = clen;
        depth++;
    }

    uint64_t pos = 0;
    for (uint32_t k = 0; k < depth; ++k) {
        if (k > 0) {
            if (pos + 1 >= max) {
                return false;
            }
            out[pos++] = '/';
        }
        if (pos + len[k] >= max) {
            return false;
        }
        ax_memcpy(out + pos, comp[k], len[k]);
        pos += len[k];
    }
    out[pos] = '\0';
    return true;
}

/* Busca una entrada por su ruta normalizada.  Devuelve el indice o -1. */
static int ax_vfs_find(const char* path)
{
    char norm[AXFS_NAME_MAX + 1];

    if (!g_ready || !path) {
        return -1;
    }
    if (!ax_path_normalize(path, norm, sizeof(norm))) {
        return -1;
    }
    if (norm[0] == '\0') {
        return 0;   /* la raiz */
    }

    uint64_t nlen = ax_strlen(norm);

    for (uint32_t i = 0; i < g_count; ++i) {
        const char* name = g_entries[i].name;
        if (ax_streq(name, norm)) {
            return (int)i;
        }

        /* Los directorios del initrd llevan '/' final ("apps/"), mientras
           que la ruta normalizada no ("apps").  Se aceptan ambas formas. */
        if (g_entries[i].type == AXFS_TYPE_DIR &&
            ax_strlen(name) == nlen + 1 && name[nlen] == '/') {
            uint64_t k = 0;
            while (k < nlen && name[k] == norm[k]) {
                k++;
            }
            if (k == nlen) {
                return (int)i;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_vfs_init(void)
{
    g_ready   = false;
    g_header  = nullptr;
    g_entries = nullptr;
    g_data    = nullptr;
    g_count   = 0;

    /* El initrd vive en una direccion fisica conocida; el mapa de identidad
       del kernel permite leerlo directamente. */
    uint64_t phys = AX_INITRD_PHYS;

    AxBootInfo* info = ax_boot_info();
    if (info && info->initrd_phys) {
        phys = info->initrd_phys;
    }

    const AxfsHeader* hdr = (const AxfsHeader*)(uintptr_t)phys;
    if (hdr->magic[0] != 'A' || hdr->magic[1] != 'X' || hdr->magic[2] != 'F' ||
        hdr->magic[3] != 'S') {
        ax_printf("[vfs] el initrd en %p no tiene una cabecera AXFS valida\n",
                  (void*)(uintptr_t)phys);
        return;
    }
    if (hdr->version != AXFS_VERSION) {
        ax_printf("[vfs] version de AXFS no soportada: %u\n", hdr->version);
        return;
    }
    if (hdr->count == 0 || hdr->count > 4096) {
        ax_printf("[vfs] numero de entradas invalido: %u\n", hdr->count);
        return;
    }

    g_header  = hdr;
    g_entries = (const AxfsEntry*)((const uint8_t*)hdr + hdr->index_off);
    g_data    = (const uint8_t*)hdr + hdr->data_off;
    g_count   = hdr->count;
    g_ready   = true;

    ax_printf("[vfs] initrd montado: %u entradas, %llu bytes\n",
              g_count, (unsigned long long)hdr->total_size);
}

const void* ax_vfs_read(const char* path, uint64_t* size_out)
{
    if (size_out) {
        *size_out = 0;
    }
    if (!g_ready) {
        return nullptr;
    }

    int idx = ax_vfs_find(path);
    if (idx < 0) {
        return nullptr;
    }
    const AxfsEntry* e = &g_entries[idx];
    if (e->type != AXFS_TYPE_FILE) {
        return nullptr;
    }
    if (size_out) {
        *size_out = e->size;
    }
    return g_data + e->offset;
}

int ax_vfs_exists(const char* path)
{
    return ax_vfs_find(path) >= 0 ? 1 : 0;
}

int ax_vfs_list(char* out, uint64_t max)
{
    if (!g_ready || !out || max < 2) {
        return 0;
    }

    uint64_t pos = 0;
    int      n   = 0;

    for (uint32_t i = 0; i < g_count; ++i) {
        const char* name = g_entries[i].name;
        uint64_t    len  = ax_strlen(name);
        if (!len) {
            continue;   /* la raiz */
        }

        if (pos + len + 2 > max) {
            break;
        }
        ax_memcpy(out + pos, name, len);
        pos += len;
        out[pos++] = '\n';
        n++;
    }
    out[pos] = '\0';
    return n;
}

uint32_t ax_vfs_count(void)
{
    return g_count;
}

const char* ax_vfs_name(uint32_t index)
{
    if (!g_ready || index >= g_count) {
        return nullptr;
    }
    return g_entries[index].name;
}

const AxfsEntry* ax_vfs_entry(uint32_t index)
{
    if (!g_ready || index >= g_count) {
        return nullptr;
    }
    return &g_entries[index];
}
