/*
 * AxolynOS - Sistema de paginacion por demanda con intercambio (swap).
 *
 * La region de user mode (AX_USER_VIRT_BASE .. AX_USER_REGION_END) esta
 * reservada en el PMM y mapeada en identidad por el VMM.  Este modulo lleva la
 * cuenta de cada pagina de esa region y anade:
 *
 *   - mapeo por demanda a cero (una pagina libre se mapea al tocarla),
 *   - evacuacion al fichero de intercambio (pagefile.axswap en AxFS-D),
 *   - recuperacion de una pagina evacuada cuando se produce un #PF.
 *
 * Como el mapa es de identidad, la pagina fisica de una direccion virtual de
 * user mode es la propia direccion: no hay que mover marcos, solo copiar
 * contenidos entre RAM y disco.
 *
 * Documentacion: manuales/14-paginacion.md
 */

#include "ax_types.h"
#include "ax_paging.h"
#include "ax_memory.h"
#include "ax_fs_disk.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */
static uint8_t  g_state[AX_USER_REGION_PAGES];
static uint32_t g_swap_slot[AX_USER_REGION_PAGES];
static uint8_t  g_swap_used[AX_PAGING_SWAP_MAX];

static char     g_swap_path[AX_PAGING_SWAP_PATH_LEN + 1];
static bool     g_swap_ready  = false;   /* fichero disponible */
static bool     g_swap_enabled = false;  /* se permite evacuar */
static uint64_t g_swap_slots  = 0;       /* ranuras del fichero */
static uint64_t g_swap_used_count = 0;

static uint64_t g_faults   = 0;
static uint64_t g_swap_ins  = 0;
static uint64_t g_swap_outs = 0;

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void pg_memset(void* dst, int value, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = (uint8_t)value;
    }
}

static void pg_strlcpy(char* dst, const char* src, uint64_t max)
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

static uint64_t pg_page_index(uint64_t virt)
{
    return (virt - AX_USER_VIRT_BASE) >> AX_PAGE_SHIFT;
}

static uint64_t pg_page_virt(uint64_t index)
{
    return AX_USER_VIRT_BASE + (index << AX_PAGE_SHIFT);
}

static bool pg_in_user_region(uint64_t page)
{
    return page >= AX_USER_VIRT_BASE && page < AX_USER_REGION_END;
}

/* ------------------------------------------------------------------ */
/* Ranuras de intercambio                                              */
/* ------------------------------------------------------------------ */
static int pg_swap_alloc_slot(void)
{
    for (uint64_t i = 0; i < g_swap_slots; ++i) {
        if (!g_swap_used[i]) {
            g_swap_used[i] = 1;
            g_swap_used_count++;
            return (int)i;
        }
    }
    return -1;
}

static void pg_swap_free_slot(int slot)
{
    if (slot < 0 || (uint64_t)slot >= g_swap_slots) {
        return;
    }
    if (g_swap_used[slot]) {
        g_swap_used[slot] = 0;
        if (g_swap_used_count > 0) {
            g_swap_used_count--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Intercambio de una pagina                                           */
/* ------------------------------------------------------------------ */
/*
 * Escribe la pagina en el fichero de intercambio y la desmapea.
 * Devuelve 0 o -errno.
 */
static int pg_swap_out_page(uint64_t index)
{
    uint64_t page = pg_page_virt(index);

    if (!g_swap_enabled || !g_swap_ready) {
        return AX_EBUSY;
    }

    int slot = pg_swap_alloc_slot();
    if (slot < 0) {
        return AX_ENOSPC;
    }

    uint64_t offset = (uint64_t)slot * AX_PAGE_SIZE;
    uint64_t wrote  = 0;
    int rc = ax_fs_disk_write_at(g_swap_path, offset, (const void*)(uintptr_t)page,
                                 AX_PAGE_SIZE, &wrote);
    if (rc != 0 || wrote != AX_PAGE_SIZE) {
        pg_swap_free_slot(slot);
        return AX_EIO;
    }

    if (ax_vmm_unmap(page) != 0) {
        pg_swap_free_slot(slot);
        return AX_EIO;
    }

    g_state[index]     = AX_PG_SWAPPED;
    g_swap_slot[index] = (uint32_t)slot;
    g_swap_outs++;
    return 0;
}

/*
 * Recupera una pagina del fichero de intercambio y la mapea.
 * Devuelve 0 o -errno.
 */
static int pg_swap_in_page(uint64_t index)
{
    uint64_t page = pg_page_virt(index);
    uint32_t slot = g_swap_slot[index];

    if (!g_swap_ready || slot >= g_swap_slots) {
        return AX_EIO;
    }

    /* Primero se mapea la pagina: el contenido se lee directamente en su
       direccion de identidad.  Si se leyera antes, el kernel provocaria un
       #PF sobre una pagina que esta en estado SWAPPED y el manejador
       recursaria indefinidamente. */
    if (ax_vmm_map(page, page, AX_PAGE_WRITE | AX_PAGE_USER) == nullptr) {
        return AX_ENOMEM;
    }

    uint64_t offset = (uint64_t)slot * AX_PAGE_SIZE;
    uint64_t got    = 0;
    int rc = ax_fs_disk_read_at(g_swap_path, offset, (void*)(uintptr_t)page,
                                AX_PAGE_SIZE, &got);
    if (rc != 0 || got != AX_PAGE_SIZE) {
        ax_vmm_unmap(page);
        return AX_EIO;
    }

    pg_swap_free_slot((int)slot);
    g_swap_slot[index] = 0;
    g_state[index]     = AX_PG_MAPPED;
    g_swap_ins++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_paging_init(void)
{
    for (uint64_t i = 0; i < AX_USER_REGION_PAGES; ++i) {
        g_state[i]     = AX_PG_FREE;
        g_swap_slot[i] = 0;
    }
    for (uint64_t i = 0; i < AX_PAGING_SWAP_MAX; ++i) {
        g_swap_used[i] = 0;
    }

    pg_strlcpy(g_swap_path, AX_PAGING_SWAP_PATH, sizeof(g_swap_path));
    g_swap_ready       = false;
    g_swap_enabled     = false;
    g_swap_slots       = 0;
    g_swap_used_count  = 0;
    g_faults           = 0;
    g_swap_ins         = 0;
    g_swap_outs        = 0;

    ax_printf("[paging] %u paginas de user mode bajo gestion (%u MiB)\n",
              (unsigned)AX_USER_REGION_PAGES,
              (unsigned)((AX_USER_REGION_PAGES * AX_PAGE_SIZE) / (1024ull * 1024ull)));
}

uint64_t ax_paging_user_page_count(void)
{
    return AX_USER_REGION_PAGES;
}

int ax_paging_touch(uint64_t virt, uint64_t size)
{
    if (size == 0) {
        return 0;
    }

    uint64_t start = AX_ALIGN_DOWN(virt, AX_PAGE_SIZE);
    uint64_t end   = AX_ALIGN_UP(virt + size, AX_PAGE_SIZE);

    if (start < AX_USER_VIRT_BASE) {
        start = AX_USER_VIRT_BASE;
    }
    if (end > AX_USER_REGION_END) {
        end = AX_USER_REGION_END;
    }
    if (start >= end) {
        return AX_EINVAL;
    }

    for (uint64_t page = start; page < end; page += AX_PAGE_SIZE) {
        uint64_t index = pg_page_index(page);
        if (index >= AX_USER_REGION_PAGES) {
            return AX_ERANGE;
        }

        if (g_state[index] == AX_PG_MAPPED) {
            continue;
        }

        if (g_state[index] == AX_PG_SWAPPED) {
            int rc = pg_swap_in_page(index);
            if (rc != 0) {
                return rc;
            }
            continue;
        }

        /* Pagina libre: se mapea a cero por demanda.  ax_vmm_map ya vacia
           la TLB, asi que no hace falta otro flush. */
        if (ax_vmm_map(page, page, AX_PAGE_WRITE | AX_PAGE_USER) == nullptr) {
            return AX_ENOMEM;
        }
        pg_memset((void*)(uintptr_t)page, 0, AX_PAGE_SIZE);
        g_state[index] = AX_PG_MAPPED;
    }
    return 0;
}

bool ax_paging_on_fault(uint64_t addr, uint64_t error)
{
    /* Un fallo con P=1 es de proteccion (escritura o usuario), no de
       presencia: no se puede resolver mapeando. */
    if (error & 1u) {
        return false;
    }

    uint64_t page = AX_ALIGN_DOWN(addr, AX_PAGE_SIZE);
    if (!pg_in_user_region(page)) {
        return false;
    }

    uint64_t index = pg_page_index(page);
    if (index >= AX_USER_REGION_PAGES) {
        return false;
    }

    if (g_state[index] == AX_PG_SWAPPED) {
        if (pg_swap_in_page(index) != 0) {
            return false;
        }
        g_faults++;
        return true;
    }

    if (g_state[index] == AX_PG_FREE) {
        if (ax_vmm_map(page, page, AX_PAGE_WRITE | AX_PAGE_USER) == nullptr) {
            return false;
        }
        pg_memset((void*)(uintptr_t)page, 0, AX_PAGE_SIZE);
        g_state[index] = AX_PG_MAPPED;
        g_faults++;
        return true;
    }

    return false;
}

int ax_paging_evict_page(uint64_t virt)
{
    uint64_t page = AX_ALIGN_DOWN(virt, AX_PAGE_SIZE);
    if (!pg_in_user_region(page)) {
        return AX_EINVAL;
    }
    uint64_t index = pg_page_index(page);
    if (index >= AX_USER_REGION_PAGES) {
        return AX_ERANGE;
    }
    if (g_state[index] != AX_PG_MAPPED) {
        return AX_EINVAL;
    }
    return pg_swap_out_page(index);
}

int ax_paging_evict_slot(uint32_t slot)
{
    if (slot >= AX_USER_MAX_SLOTS) {
        return AX_EINVAL;
    }
    uint64_t base = AX_USER_VIRT_BASE + (uint64_t)slot * AX_USER_SLOT_SIZE;
    int done = 0;
    for (uint64_t off = 0; off < AX_USER_SLOT_SIZE; off += AX_PAGE_SIZE) {
        uint64_t index = pg_page_index(base + off);
        if (index < AX_USER_REGION_PAGES && g_state[index] == AX_PG_MAPPED) {
            if (ax_paging_evict_page(base + off) == 0) {
                done++;
            }
        }
    }
    return done;
}

int ax_paging_free_slot(uint32_t slot)
{
    if (slot >= AX_USER_MAX_SLOTS) {
        return AX_EINVAL;
    }
    uint64_t base = AX_USER_VIRT_BASE + (uint64_t)slot * AX_USER_SLOT_SIZE;
    for (uint64_t off = 0; off < AX_USER_SLOT_SIZE; off += AX_PAGE_SIZE) {
        uint64_t page  = base + off;
        uint64_t index = pg_page_index(page);
        if (index >= AX_USER_REGION_PAGES) {
            break;
        }

        if (g_state[index] == AX_PG_MAPPED) {
            ax_vmm_unmap(page);
            g_state[index] = AX_PG_FREE;
        } else if (g_state[index] == AX_PG_SWAPPED) {
            pg_swap_free_slot((int)g_swap_slot[index]);
            g_swap_slot[index] = 0;
            g_state[index]     = AX_PG_FREE;
        } else if (ax_vmm_translate(page) != 0) {
            /* Pagina libre pero todavia mapeada (mapa de identidad inicial):
               se desmapea para que el siguiente proceso la prepare por
               demanda y no vea memoria sucia. */
            ax_vmm_unmap(page);
        }
    }
    return 0;
}

int ax_paging_evict_all(void)
{
    int done = 0;
    for (uint64_t index = 0; index < AX_USER_REGION_PAGES; ++index) {
        if (g_state[index] == AX_PG_MAPPED) {
            if (pg_swap_out_page(index) == 0) {
                done++;
            }
        }
    }
    return done;
}

int ax_paging_swap_enable(const char* path)
{
    if (path && path[0]) {
        pg_strlcpy(g_swap_path, path, sizeof(g_swap_path));
    }

    if (!ax_fs_disk_mounted()) {
        g_swap_ready   = false;
        g_swap_enabled = false;
        g_swap_slots   = 0;
        return AX_ENOENT;
    }

    uint32_t type = 0;
    uint64_t size = 0;
    int rc = ax_fs_disk_stat(g_swap_path, &type, &size);
    if (rc != 0 || type != AXFS_DISK_TYPE_FILE) {
        /* El fichero no existe: se crea con el tamano por defecto. */
        ax_fs_disk_mkdir("/system");
        rc = ax_fs_disk_write(g_swap_path, nullptr, AX_PAGING_SWAP_SIZE);
        if (rc != 0) {
            return rc;
        }
        size = AX_PAGING_SWAP_SIZE;
    }

    if (size < AX_PAGE_SIZE) {
        return AX_ENOSPC;
    }

    g_swap_slots = size / AX_PAGE_SIZE;
    if (g_swap_slots > AX_PAGING_SWAP_MAX) {
        g_swap_slots = AX_PAGING_SWAP_MAX;
    }
    g_swap_ready   = true;
    g_swap_enabled = true;

    ax_printf("[paging] intercambio activo: %s (%llu ranuras de %u bytes)\n",
              g_swap_path, (unsigned long long)g_swap_slots,
              (unsigned)AX_PAGE_SIZE);
    return 0;
}

int ax_paging_swap_disable(void)
{
    g_swap_enabled = false;
    ax_printf("[paging] intercambio desactivado (las paginas evacuadas siguen "
              "estando en %s)\n", g_swap_path);
    return 0;
}

bool ax_paging_swap_is_enabled(void)
{
    return g_swap_enabled;
}

int ax_paging_fill_info(AxPagingInfo* out)
{
    if (!out) {
        return AX_EINVAL;
    }

    uint64_t mapped   = 0;
    uint64_t free     = 0;
    uint64_t swapped  = 0;
    for (uint64_t i = 0; i < AX_USER_REGION_PAGES; ++i) {
        switch (g_state[i]) {
            case AX_PG_MAPPED:  mapped++;  break;
            case AX_PG_SWAPPED: swapped++; break;
            default:            free++;    break;
        }
    }

    out->total_pages   = AX_USER_REGION_PAGES;
    out->mapped_pages  = mapped;
    out->free_pages    = free;
    out->swapped_pages = swapped;
    out->page_faults   = g_faults;
    out->swap_ins      = g_swap_ins;
    out->swap_outs     = g_swap_outs;
    out->swap_enabled  = g_swap_enabled ? 1u : 0u;
    out->swap_slots    = g_swap_slots;
    out->swap_used     = g_swap_used_count;
    out->swap_bytes    = g_swap_slots * AX_PAGE_SIZE;
    return 0;
}
