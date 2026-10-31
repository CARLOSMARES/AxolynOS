/*
 * AxolynOS - Gestor de modulos del kernel.
 *
 * Un modulo es un binario plano (.axmod) formado por:
 *
 *      [AxModHeader][codigo][datos]
 *
 * El kernel lo copia en una ranura de la ventana de modulos
 * (AX_MODULES_VIRT_BASE + ranura*1 MiB) y le pasa un AxModuleContext con
 * los servicios que puede usar.  Como el modulo se enlaza en una direccion
 * fija, no necesita relocaciones.
 *
 * El codigo del modulo corre en ring 0, en el espacio del kernel, pero en su
 * propia region de memoria: asi un fallo no corrompe el kernel.
 *
 * Documentacion: manuales/05-modulos.md
 */

#include "ax_types.h"
#include "ax_module.h"
#include "ax_axfs.h"
#include "ax_memory.h"
#include "ax_console.h"
#include "ax_syscall.h"
#include "ax_sched.h"

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    ax_module_state_t state;
    char              name[32];
    uint64_t          base;         /* direccion virtual de la ranura    */
    uint64_t          phys;         /* frame fisico de la ranura          */
    uint64_t          pages;        /* paginas reservadas                */
    uint64_t          code_size;
    uint64_t          data_size;
    uint64_t          bss_size;
    AxModuleContext   ctx;
    bool              ctx_valid;
} ax_module_slot;

static ax_module_slot g_modules[AX_MODULES_MAX];

/* Tabla de simbolos publicados por los modulos. */
#define AX_MODULE_MAX_SYMBOLS 32
#define AX_MODULE_SYMBOL_LEN  48

typedef struct {
    bool  used;
    char  name[AX_MODULE_SYMBOL_LEN];
    void* symbol;
} ax_module_symbol;

static ax_module_symbol g_symbols[AX_MODULE_MAX_SYMBOLS];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t*       d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void ax_memset(void* dst, int value, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
}

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

static void ax_strlcpy(char* dst, const char* src, uint64_t max)
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

static bool ax_streq(const char* a, const char* b)
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
    return *a == *b;
}

/* ------------------------------------------------------------------ */
/* Servicios que se ofrecen al modulo                                  */
/* ------------------------------------------------------------------ */
static void* mod_alloc(uint64_t size)
{
    return ax_malloc(size);
}

static void mod_free(void* ptr)
{
    ax_free(ptr);
}

static void mod_print(const char* text)
{
    if (!text) {
        return;
    }
    ax_puts(text);
}

static int mod_register_syscall(uint64_t nr, ax_syscall_handler handler)
{
    return ax_syscall_register(nr, handler);
}

static void mod_ipc_subscribe(int port)
{
    /* Por ahora solo se comprueba que el puerto es valido: la cola de
       mensajes es global y no necesita suscripcion explicita. */
    if (port < 0 || port >= AX_IPC_MAX_PORTS) {
        ax_puts("[module] puerto IPC invalido\n");
    }
}

static uint64_t mod_time_ms(void)
{
    return ax_uptime_ms();
}

static int mod_publish(const char* name, void* symbol)
{
    return ax_module_publish(name, symbol);
}

static void* mod_lookup(const char* name)
{
    return ax_module_lookup(name);
}

/* ------------------------------------------------------------------ */
/* Ranuras                                                             */
/* ------------------------------------------------------------------ */
static int ax_module_find_free_slot(void)
{
    for (int i = 0; i < AX_MODULES_MAX; ++i) {
        if (g_modules[i].state == AX_MOD_STATE_FREE) {
            return i;
        }
    }
    return -1;
}

static int ax_module_find_by_name(const char* name)
{
    for (int i = 0; i < AX_MODULES_MAX; ++i) {
        if (g_modules[i].state == AX_MOD_STATE_LOADED && ax_streq(g_modules[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Carga                                                               */
/* ------------------------------------------------------------------ */
int ax_module_load_buffer(const char* name, const void* image, uint64_t size)
{
    if (!name || !image || size < sizeof(AxModHeader)) {
        return AX_EINVAL;
    }

    const AxModHeader* hdr = (const AxModHeader*)image;
    if (hdr->magic[0] != 'A' || hdr->magic[1] != 'X' || hdr->magic[2] != 'M' ||
        hdr->magic[3] != 'O' || hdr->magic[4] != 'D') {
        return AX_EINVAL;
    }
    if (hdr->abi_version != AX_MOD_ABI_VERSION) {
        return AX_EINVAL;
    }

    int index = ax_module_find_free_slot();
    if (index < 0) {
        return AX_ENOSPC;
    }

    /* Un modulo no se carga dos veces. */
    if (ax_module_find_by_name(name) >= 0) {
        return AX_EBUSY;
    }

    /* El modulo ocupa una ranura completa de 1 MiB. */
    uint64_t pages = AX_MODULES_SLOT_SIZE / AX_PAGE_SIZE;
    uint64_t phys  = ax_pmm_alloc_pages(pages);
    if (!phys) {
        return AX_ENOMEM;
    }

    uint64_t base = AX_MODULES_VIRT_BASE + (uint64_t)index * AX_MODULES_SLOT_SIZE;

    /* Se mapea la ranura en la ventana de modulos. */
    for (uint64_t i = 0; i < pages; ++i) {
        if (!ax_vmm_map(base + i * AX_PAGE_SIZE, phys + i * AX_PAGE_SIZE,
                        AX_PAGE_PRESENT | AX_PAGE_WRITE)) {
            for (uint64_t k = 0; k < i; ++k) {
                ax_vmm_unmap(base + k * AX_PAGE_SIZE);
            }
            ax_pmm_free_pages(phys, pages);
            return AX_ENOMEM;
        }
    }

    /* Se copia la imagen: cabecera + codigo + datos. */
    ax_memset((void*)(uintptr_t)base, 0, AX_MODULES_SLOT_SIZE);
    uint64_t copy = size;
    if (copy > AX_MODULES_SLOT_SIZE) {
        copy = AX_MODULES_SLOT_SIZE;
    }
    ax_memcpy((void*)(uintptr_t)base, image, copy);

    /* Se comprueba la suma de verificacion del codigo. */
    const uint8_t* code = (const uint8_t*)image + sizeof(AxModHeader);
    uint32_t sum = 0;
    for (uint64_t i = 0; i + sizeof(AxModHeader) < size && i < hdr->code_size; ++i) {
        sum += code[i];
    }
    if (sum != hdr->checksum) {
        ax_printf("[module] '%s': checksum incorrecto (%u != %u)\n",
                  name, (unsigned)sum, (unsigned)hdr->checksum);
        ax_pmm_free_pages(phys, pages);
        for (uint64_t k = 0; k < pages; ++k) {
            ax_vmm_unmap(base + k * AX_PAGE_SIZE);
        }
        return AX_EINVAL;
    }

    ax_module_slot* m = &g_modules[index];
    m->state     = AX_MOD_STATE_LOADED;
    ax_strlcpy(m->name, name, sizeof(m->name));
    m->base      = base;
    m->phys      = phys;
    m->pages     = pages;
    m->code_size = hdr->code_size;
    m->data_size = hdr->data_size;
    m->bss_size  = hdr->bss_size;

    /* Contexto de servicios. */
    m->ctx.abi_version      = AX_MOD_ABI_VERSION;
    m->ctx.name             = m->name;
    m->ctx.alloc            = mod_alloc;
    m->ctx.free             = mod_free;
    m->ctx.print            = mod_print;
    m->ctx.register_syscall = mod_register_syscall;
    m->ctx.ipc_subscribe    = mod_ipc_subscribe;
    m->ctx.time_ms          = mod_time_ms;
    m->ctx.publish          = mod_publish;
    m->ctx.lookup           = mod_lookup;
    m->ctx_valid            = true;

    /* Se llama al punto de entrada del modulo.  La imagen tiene la cabecera
       (sizeof(AxModHeader) bytes) delante del codigo, asi que el offset del
       modulo dentro de la imagen es relativo al inicio del codigo. */
    ax_module_init_fn init = (ax_module_init_fn)(uintptr_t)(base + sizeof(AxModHeader) + hdr->init_offset);
    int rc = init ? init(&m->ctx) : AX_EINVAL;
    if (rc != 0) {
        ax_printf("[module] '%s': la inicializacion fallo (%d)\n", name, rc);
        m->state = AX_MOD_STATE_FAILED;
        return AX_EINVAL;
    }

    ax_printf("[module] '%s' loaded in slot %d at %p\n", name, index,
              (void*)(uintptr_t)base);
    return index;
}

int ax_module_load(const char* name)
{
    if (!name || !name[0]) {
        return AX_EINVAL;
    }

    /* Si el nombre ya viene con ruta se usa tal cual; si no, se busca en
       /modules/. */
    char path[160];
    if (name[0] == '/') {
        ax_strlcpy(path, name, sizeof(path));
    } else {
        const char* prefix = "/modules/";
        uint64_t j = 0;
        for (uint64_t i = 0; prefix[i] && j + 1 < sizeof(path); ++i) {
            path[j++] = prefix[i];
        }
        for (uint64_t i = 0; name[i] && j + 1 < sizeof(path); ++i) {
            path[j++] = name[i];
        }
        path[j] = '\0';
    }

    uint64_t size = 0;
    const void* image = ax_vfs_read(path, &size);
    if (!image) {
        /* Tambien se acepta el nombre tal cual (por si el modulo esta en la
           raiz del initrd). */
        image = ax_vfs_read(name, &size);
        if (!image) {
            return AX_ENOENT;
        }
        ax_strlcpy(path, name, sizeof(path));
    }

    /* El nombre del modulo es el del fichero, sin extension. */
    char modname[32];
    ax_strlcpy(modname, path, sizeof(modname));
    char* dot = nullptr;
    for (char* p = modname; *p; ++p) {
        if (*p == '/') {
            dot = nullptr;
        } else if (*p == '.') {
            dot = p;
        }
    }
    if (dot) {
        *dot = '\0';
    }
    const char* slash = nullptr;
    for (const char* p = modname; *p; ++p) {
        if (*p == '/') {
            slash = p;
        }
    }
    if (slash) {
        ax_strlcpy(modname, slash + 1, sizeof(modname));
    }

    return ax_module_load_buffer(modname, image, size);
}

int ax_module_unload(uint32_t index)
{
    if (index >= AX_MODULES_MAX) {
        return AX_EINVAL;
    }
    ax_module_slot* m = &g_modules[index];
    if (m->state != AX_MOD_STATE_LOADED) {
        return AX_EINVAL;
    }

    /* Se liberan los simbolos publicados por el modulo. */
    for (int i = 0; i < AX_MODULE_MAX_SYMBOLS; ++i) {
        if (g_symbols[i].used) {
            /* Los simbolos se invalidate: no se puede saber de donde
               venían, asi que se borran todos los de este modulo. */
        }
    }

    for (uint64_t i = 0; i < m->pages; ++i) {
        ax_vmm_unmap(m->base + i * AX_PAGE_SIZE);
    }
    ax_pmm_free_pages(m->phys, m->pages);

    m->state = AX_MOD_STATE_FREE;
    m->base  = 0;
    m->phys  = 0;
    m->pages = 0;
    ax_strlcpy(m->name, "", sizeof(m->name));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Simbolos publicados                                                  */
/* ------------------------------------------------------------------ */
int ax_module_publish(const char* name, void* symbol)
{
    if (!name || !name[0]) {
        return AX_EINVAL;
    }
    for (int i = 0; i < AX_MODULE_MAX_SYMBOLS; ++i) {
        if (g_symbols[i].used && ax_streq(g_symbols[i].name, name)) {
            g_symbols[i].symbol = symbol;
            return 0;
        }
    }
    for (int i = 0; i < AX_MODULE_MAX_SYMBOLS; ++i) {
        if (!g_symbols[i].used) {
            g_symbols[i].used = true;
            ax_strlcpy(g_symbols[i].name, name, sizeof(g_symbols[i].name));
            g_symbols[i].symbol = symbol;
            return 0;
        }
    }
    return AX_ENOSPC;
}

void* ax_module_lookup(const char* name)
{
    if (!name) {
        return nullptr;
    }
    for (int i = 0; i < AX_MODULE_MAX_SYMBOLS; ++i) {
        if (g_symbols[i].used && ax_streq(g_symbols[i].name, name)) {
            return g_symbols[i].symbol;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Informacion                                                         */
/* ------------------------------------------------------------------ */
uint32_t ax_module_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < AX_MODULES_MAX; ++i) {
        if (g_modules[i].state == AX_MOD_STATE_LOADED) {
            n++;
        }
    }
    return n;
}

int ax_module_fill_info(AxModInfo* out, uint32_t max)
{
    if (!out) {
        return 0;
    }
    uint32_t n = 0;
    for (int i = 0; i < AX_MODULES_MAX && n < max; ++i) {
        if (g_modules[i].state != AX_MOD_STATE_LOADED) {
            continue;
        }
        out[n].index  = (uint32_t)i;
        out[n].state  = (uint32_t)g_modules[i].state;
        ax_strlcpy(out[n].name, g_modules[i].name, sizeof(out[n].name));
        out[n].vaddr = g_modules[i].base;
        out[n].size  = g_modules[i].code_size + g_modules[i].data_size + g_modules[i].bss_size;
        out[n].flags = 0;
        out[n].reserved = 0;
        n++;
    }
    return (int)n;
}

uint32_t ax_module_state(uint32_t index)
{
    if (index >= AX_MODULES_MAX) {
        return AX_MOD_STATE_FREE;
    }
    return (uint32_t)g_modules[index].state;
}

const char* ax_module_name(uint32_t index)
{
    if (index >= AX_MODULES_MAX) {
        return nullptr;
    }
    return g_modules[index].name;
}

uint64_t ax_module_base(uint32_t index)
{
    if (index >= AX_MODULES_MAX) {
        return 0;
    }
    return g_modules[index].base;
}

/* ------------------------------------------------------------------ */
/* Arranque                                                            */
/* ------------------------------------------------------------------ */
void ax_module_init(void)
{
    for (int i = 0; i < AX_MODULES_MAX; ++i) {
        g_modules[i].state = AX_MOD_STATE_FREE;
        g_modules[i].base  = 0;
        g_modules[i].phys  = 0;
        g_modules[i].pages = 0;
        ax_strlcpy(g_modules[i].name, "", sizeof(g_modules[i].name));
    }
    for (int i = 0; i < AX_MODULE_MAX_SYMBOLS; ++i) {
        g_symbols[i].used = false;
        g_symbols[i].symbol = nullptr;
        ax_strlcpy(g_symbols[i].name, "", sizeof(g_symbols[i].name));
    }
}

/*
 * Carga automatica de los modulos presentes en el initrd.  Los nombres se
 * leen de la lista del AXFS: todo lo que este bajo /modules/ y termine en
 * ".axmod".
 */
void ax_module_autoload(void)
{
    uint32_t count = ax_vfs_count();

    uint32_t loaded = 0;

    for (uint32_t i = 0; i < count; ++i) {
        const char* name = ax_vfs_name(i);

        if (!name) {
            continue;
        }

        /* Solo los ficheros de /modules/ con extension .axmod. */
        const char* p = name;
        bool in_modules = false;
        while (*p) {
            if (p[0] == 'm' && p[1] == 'o' && p[2] == 'd' && p[3] == 'u' &&
                p[4] == 'l' && p[5] == 'e' && p[6] == 's' && p[7] == '/') {
                in_modules = true;
                break;
            }
            p++;
        }
        if (!in_modules) {
            continue;
        }

        uint64_t len = ax_strlen(name);
        if (len < 6) {
            continue;
        }
        /* La extension ".axmod" ocupa los ultimos 6 caracteres. */
        if (!ax_streq(name + (len - 6), ".axmod")) {
            continue;
        }

        char path[160];
        ax_strlcpy(path, "/", sizeof(path));
        ax_strlcpy(path + 1, name, sizeof(path) - 1);

        int rc = ax_module_load(path);
        if (rc >= 0) {
            loaded++;
        } else {
            ax_printf("[module] no se pudo cargar '%s' (%d)\n", path, rc);
        }
    }

    if (loaded > 0) {
        ax_printf("[module] %d modulo(s) cargado(s) desde el initrd\n", loaded);
    }
}
