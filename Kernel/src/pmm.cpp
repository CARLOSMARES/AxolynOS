/*
 * AxolynOS - Gestor de memoria fisica (bitmap).
 *
 * El bitmap vive en AX_BITMAP_PHYS (0xE00000), dentro de la region reservada
 * del kernel, y se accede a el a traves del mapa de identidad bajo que
 * instala el arranque (boot.S) y mantiene el VMM.  Cada bit representa un
 * frame de 4 KiB: 0 = libre, 1 = usado/reservado.
 *
 * El bitmap de 1 MiB cubre 8 M frames, es decir 32 GiB de memoria fisica,
 * muy por encima de lo que necesita cualquier PC de la epoca.
 */

#include "ax_types.h"
#include "ax_memory.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Estado del PMM                                                      */
/* ------------------------------------------------------------------ */
static uint64_t* const ax_pmm_bitmap = (uint64_t*)AX_BITMAP_PHYS;

static uint64_t ax_pmm_total_frames = 0;    /* frames que cubre el bitmap  */
static uint64_t ax_pmm_used_frames = 0;      /* frames ocupados (reservados + asignados) */
static uint64_t ax_pmm_highest_frame = 0;    /* frame mas alto inicializado */

#define AX_PMM_BITMAP_BYTES AX_BITMAP_MAX
#define AX_PMM_MAX_FRAMES   (AX_PMM_BITMAP_BYTES * 8ull)

/* ------------------------------------------------------------------ */
/* Utilidades de bitmap                                                 */
/* ------------------------------------------------------------------ */
static inline void ax_pmm_bit_set(uint64_t frame)
{
    ax_pmm_bitmap[frame >> 6] |= (1ull << (frame & 63));
}

static inline void ax_pmm_bit_clear(uint64_t frame)
{
    ax_pmm_bitmap[frame >> 6] &= ~(1ull << (frame & 63));
}

static inline bool ax_pmm_bit_test(uint64_t frame)
{
    return (ax_pmm_bitmap[frame >> 6] & (1ull << (frame & 63))) != 0;
}

static void ax_pmm_bitmap_clear_all(void)
{
    uint64_t words = AX_PMM_BITMAP_BYTES / 8;
    for (uint64_t i = 0; i < words; ++i) {
        ax_pmm_bitmap[i] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Inicializacion                                                      */
/* ------------------------------------------------------------------ */
void ax_pmm_init(uint64_t mem_size, const ax_e820_entry* entries, uint64_t count)
{
    /* Todo el bitmap empieza ocupado: solo se libera lo que el firmware
       declara utilizable. */
    ax_pmm_bitmap_clear_all();

    uint64_t frames = mem_size / AX_PAGE_SIZE;
    if (frames > AX_PMM_MAX_FRAMES) {
        frames = AX_PMM_MAX_FRAMES;
    }
    ax_pmm_total_frames = frames;

    /* Marco de la region del kernel: la imagen, las tablas de pagina, el
       bitmap, el heap, el staging de modulos y el initrd.  Es zona
       exclusiva del kernel (nunca se entrega a user mode). */
    ax_pmm_reserve(AX_KERNEL_PHYS_BASE, AX_KERNEL_REGION_SIZE);

    /* Region heredada del BIOS (0..1 MiB): contiene las tablas del firmware
       y la zona de video; no se entrega a nadie. */
    ax_pmm_reserve(0, 0x100000);

    /* Los huecos que declara el firmware (E820) pasan a estar libres. */
    if (entries && count) {
        for (uint64_t i = 0; i < count; ++i) {
            const ax_e820_entry* entry = &entries[i];
            if (entry->type != AX_E820_USABLE || entry->length == 0) {
                continue;
            }
            uint64_t base = entry->base;
            uint64_t end = base + entry->length;
            if (end < base) {
                continue;                       /* desbordamiento: la ignoramos */
            }
            if (base >= mem_size) {
                continue;
            }
            if (end > mem_size) {
                end = mem_size;
            }
            uint64_t first = base / AX_PAGE_SIZE;
            uint64_t last = end / AX_PAGE_SIZE;
            for (uint64_t frame = first; frame < last && frame < frames; ++frame) {
                ax_pmm_bit_clear(frame);
            }
        }
    }

    /* Volvemos a reservar la region del kernel: la memoria declarada por el
       firmware puede solaparse con ella y no debe entregarse a user mode. */
    ax_pmm_reserve(AX_KERNEL_PHYS_BASE, AX_KERNEL_REGION_SIZE);
    ax_pmm_reserve(0, 0x100000);

    /* El frame 0 nunca se entrega (puntero nulo). */
    ax_pmm_reserve(0, AX_PAGE_SIZE);

    /* Recuento final. */
    ax_pmm_used_frames = 0;
    ax_pmm_highest_frame = 0;
    for (uint64_t frame = 0; frame < frames; ++frame) {
        if (ax_pmm_bit_test(frame)) {
            ++ax_pmm_used_frames;
            ax_pmm_highest_frame = frame + 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Asignacion y liberacion                                             */
/* ------------------------------------------------------------------ */
uint64_t ax_pmm_alloc_pages(uint64_t count)
{
    if (count == 0) {
        return 0;
    }

    uint64_t run = 0;
    uint64_t start = 0;

    for (uint64_t frame = 0; frame < ax_pmm_total_frames; ++frame) {
        if (!ax_pmm_bit_test(frame)) {
            if (run == 0) {
                start = frame;
            }
            if (++run == count) {
                for (uint64_t i = 0; i < count; ++i) {
                    ax_pmm_bit_set(start + i);
                }
                ax_pmm_used_frames += count;
                return start * AX_PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;   /* no hay hueco contiguo suficiente */
}

uint64_t ax_pmm_alloc_page(void)
{
    return ax_pmm_alloc_pages(1);
}

void ax_pmm_free_pages(uint64_t phys, uint64_t count)
{
    if (count == 0) {
        return;
    }
    uint64_t first = phys / AX_PAGE_SIZE;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t frame = first + i;
        if (frame >= ax_pmm_total_frames) {
            break;
        }
        if (ax_pmm_bit_test(frame)) {
            ax_pmm_bit_clear(frame);
            if (ax_pmm_used_frames) {
                --ax_pmm_used_frames;
            }
        }
    }
}

void ax_pmm_reserve(uint64_t phys, uint64_t size)
{
    if (size == 0) {
        return;
    }
    uint64_t first = phys / AX_PAGE_SIZE;
    uint64_t last = (phys + size + AX_PAGE_SIZE - 1) / AX_PAGE_SIZE;
    for (uint64_t frame = first; frame < last; ++frame) {
        if (frame >= AX_PMM_MAX_FRAMES) {
            break;
        }
        ax_pmm_bit_set(frame);
    }
}

/* ------------------------------------------------------------------ */
/* Informacion                                                         */
/* ------------------------------------------------------------------ */
uint64_t ax_pmm_total_pages(void)
{
    return ax_pmm_total_frames;
}

uint64_t ax_pmm_free_pages(void)
{
    return ax_pmm_total_frames - ax_pmm_used_frames;
}

uint64_t ax_pmm_reserved_pages(void)
{
    return ax_pmm_used_frames;
}

uint64_t ax_pmm_total_bytes(void)
{
    return ax_pmm_total_frames * AX_PAGE_SIZE;
}

uint64_t ax_pmm_free_bytes(void)
{
    return ax_pmm_free_pages() * AX_PAGE_SIZE;
}

int ax_pmm_fill_info(AxMemInfo* out)
{
    if (!out) {
        return -1;
    }
    out->total_phys = ax_pmm_total_bytes();
    out->free_phys  = ax_pmm_free_bytes();
    out->total_pages = ax_pmm_total_pages();
    out->free_pages  = ax_pmm_free_pages();
    out->reserved_pages = ax_pmm_reserved_pages();
    out->kernel_virt = AX_KERNEL_VIRT_BASE;
    out->heap_used   = ax_heap_used();
    out->heap_total  = ax_heap_total();
    out->low_mapped  = 0;   /* lo rellena el VMM */
    return 0;
}
