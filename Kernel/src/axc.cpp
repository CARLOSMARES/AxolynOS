/*
 * AxolynOS - Configuracion de aplicaciones (.axc).
 *
 * El formato y la API estan documentados en Kernel/include/ax_axc.h y en
 * manuales/17-axc.md.  El analizador reutiliza el parser INI del kernel: un
 * .axc es un INI con las secciones [app] y [connections].
 */

#include "ax_types.h"
#include "ax_axc.h"
#include "ax_ini.h"
#include "ax_axfs.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Registro                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    bool           used;
    AxcInfo        info;
    AxcConnection  conns[AX_AXC_MAX_CONNS];
} ax_axc_entry;

static ax_axc_entry g_axc[AX_AXC_MAX_APPS];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void axc_memset(void* dst, int value, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = (uint8_t)value;
    }
}

static void axc_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static uint64_t axc_strlen(const char* s)
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

static void axc_strlcpy(char* dst, const char* src, uint64_t max)
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

static char axc_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool axc_eq_ci(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return false;
    }
    while (a[i] && b[i]) {
        if (axc_lower(a[i]) != axc_lower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == b[i];
}

static bool axc_suffix(const char* s, const char* suffix)
{
    uint64_t ls = axc_strlen(s);
    uint64_t lf = axc_strlen(suffix);
    if (lf > ls) {
        return false;
    }
    return axc_eq_ci(s + ls - lf, suffix);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_axc_init(void)
{
    for (int i = 0; i < AX_AXC_MAX_APPS; ++i) {
        g_axc[i].used = false;
        axc_memset(&g_axc[i].info, 0, sizeof(g_axc[i].info));
        axc_memset(g_axc[i].conns, 0, sizeof(g_axc[i].conns));
    }
}

int ax_axc_find(const char* name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < AX_AXC_MAX_APPS; ++i) {
        if (g_axc[i].used && axc_eq_ci(g_axc[i].info.name, name)) {
            return i;
        }
    }
    return -1;
}

int ax_axc_load(const char* path)
{
    if (!path || !path[0]) {
        return AX_EINVAL;
    }

    int entries = ax_ini_load(path);
    if (entries < 0) {
        return entries;
    }

    char name[AX_AXC_NAME_LEN];
    char value[AX_INI_VALUE_MAX];
    name[0] = '\0';

    if (ax_ini_get("app", "name", name, sizeof(name)) != 0) {
        ax_printf("[axc] %s: falta [app] name\n", path);
        return AX_EINVAL;
    }

    /* Si la aplicacion ya esta registrada, se actualiza en su hueco. */
    int slot = ax_axc_find(name);
    if (slot < 0) {
        for (int i = 0; i < AX_AXC_MAX_APPS; ++i) {
            if (!g_axc[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return AX_ENOSPC;
    }

    ax_axc_entry* e = &g_axc[slot];
    axc_memset(e, 0, sizeof(*e));
    e->used = true;
    axc_strlcpy(e->info.name, name, sizeof(e->info.name));

    if (ax_ini_get("app", "version", value, sizeof(value)) == 0) {
        axc_strlcpy(e->info.version, value, sizeof(e->info.version));
    }
    if (ax_ini_get("app", "publisher", value, sizeof(value)) == 0) {
        axc_strlcpy(e->info.publisher, value, sizeof(e->info.publisher));
    }
    if (ax_ini_get("app", "exec", value, sizeof(value)) == 0) {
        axc_strlcpy(e->info.exec, value, sizeof(e->info.exec));
    }
    if (ax_ini_get("app", "icon", value, sizeof(value)) == 0) {
        axc_strlcpy(e->info.icon, value, sizeof(e->info.icon));
    }
    if (ax_ini_get("app", "description", value, sizeof(value)) == 0) {
        axc_strlcpy(e->info.description, value, sizeof(e->info.description));
    }

    /* Conexiones de la seccion [connections]. */
    uint32_t conns = 0;
    for (int i = 0; i < entries && conns < AX_AXC_MAX_CONNS; ++i) {
        AxIniEntry ie;
        if (ax_ini_enum((uint32_t)i, &ie) != 0) {
            continue;
        }
        if (!axc_eq_ci(ie.section, "connections")) {
            continue;
        }
        axc_strlcpy(e->conns[conns].key, ie.key, sizeof(e->conns[conns].key));
        axc_strlcpy(e->conns[conns].value, ie.value,
                    sizeof(e->conns[conns].value));
        conns++;
    }
    e->info.conn_count = conns;

    ax_printf("[axc] '%s' v%s (%s) cargada\n",
              e->info.name, e->info.version, e->info.publisher);
    return slot;
}

int ax_axc_unload(uint32_t index)
{
    if (index >= AX_AXC_MAX_APPS || !g_axc[index].used) {
        return AX_EINVAL;
    }
    g_axc[index].used = false;
    axc_memset(&g_axc[index].info, 0, sizeof(g_axc[index].info));
    axc_memset(g_axc[index].conns, 0, sizeof(g_axc[index].conns));
    return 0;
}

int ax_axc_count(void)
{
    int n = 0;
    for (int i = 0; i < AX_AXC_MAX_APPS; ++i) {
        if (g_axc[i].used) {
            n++;
        }
    }
    return n;
}

int ax_axc_info(uint32_t index, AxcInfo* out)
{
    if (!out || index >= AX_AXC_MAX_APPS || !g_axc[index].used) {
        return AX_ENOENT;
    }
    axc_memcpy(out, &g_axc[index].info, sizeof(*out));
    return 0;
}

int ax_axc_conn(uint32_t index, uint32_t conn_index, AxcConnection* out)
{
    if (!out || index >= AX_AXC_MAX_APPS || !g_axc[index].used) {
        return AX_ENOENT;
    }
    if (conn_index >= g_axc[index].info.conn_count) {
        return AX_ENOENT;
    }
    axc_memcpy(out, &g_axc[index].conns[conn_index], sizeof(*out));
    return 0;
}

int ax_axc_connection(const char* name, const char* key,
                      char* out, uint64_t max)
{
    if (!out || max == 0 || !key) {
        return AX_EINVAL;
    }
    int index = ax_axc_find(name);
    if (index < 0) {
        return AX_ENOENT;
    }
    for (uint32_t i = 0; i < g_axc[index].info.conn_count; ++i) {
        if (axc_eq_ci(g_axc[index].conns[i].key, key)) {
            axc_strlcpy(out, g_axc[index].conns[i].value, max);
            return 0;
        }
    }
    return AX_ENOENT;
}

int ax_axc_list(char* out, uint64_t max)
{
    if (!out || max == 0) {
        return AX_EINVAL;
    }
    uint64_t used = 0;
    for (int i = 0; i < AX_AXC_MAX_APPS; ++i) {
        if (!g_axc[i].used) {
            continue;
        }
        char line[160];
        uint64_t n = 0;
        const char* fields[3] = {
            g_axc[i].info.name,
            g_axc[i].info.version,
            g_axc[i].info.publisher
        };
        for (int f = 0; f < 3; ++f) {
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
        axc_memcpy(out + used, line, n);
        used += n;
    }
    out[used] = '\0';
    return (int)used;
}

void ax_axc_autoload(void)
{
    uint32_t total = ax_vfs_count();
    int loaded = 0;

    for (uint32_t i = 0; i < total; ++i) {
        const char* name = ax_vfs_name(i);
        if (!name) {
            continue;
        }
        if (!axc_suffix(name, ".axc")) {
            continue;
        }
        bool in_config = false;
        const char* p = name;
        while (*p) {
            if (p[0] == 'c' && p[1] == 'o' && p[2] == 'n' && p[3] == 'f' &&
                p[4] == 'i' && p[5] == 'g' && p[6] == '/') {
                in_config = true;
                break;
            }
            p++;
        }
        if (!in_config) {
            continue;
        }

        char path[160];
        path[0] = '/';
        axc_strlcpy(path + 1, name, sizeof(path) - 1);

        int rc = ax_axc_load(path);
        if (rc >= 0) {
            loaded++;
        } else {
            ax_printf("[axc] no se pudo cargar '%s' (%d)\n", path, rc);
        }
    }

    if (loaded > 0) {
        ax_printf("[axc] %d configuracion(es) de aplicaciones cargada(s)\n",
                  loaded);
    }
}
