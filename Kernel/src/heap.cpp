/*
 * AxolynOS - Heap del kernel (kmalloc) y area de staging para modulos.
 *
 * El heap vive en AX_HEAP_PHYS (0xF00000) con 8 MiB, dentro de la region
 * reservada del kernel, y se accede a el por el mapa de identidad.  La
 * gestion es de bloques contiguos con cabecera, lista doblemente enlazada en
 * orden de direccion y fusion (coalescing) de bloques libres contiguos.
 *
 * Cabecera de bloque (32 bytes, alineada a 16):
 *   size  : bytes utiles del bloque (sin la cabecera)
 *   flags : AX_HEAP_FREE | AX_HEAP_USED
 *   next  : direccion de la siguiente cabecera (0 si es el ultimo)
 *   prev  : direccion de la cabecera anterior (0 si es el primero)
 */

#include "ax_types.h"
#include "ax_memory.h"

#define AX_HEAP_FREE 0x1ull
#define AX_HEAP_USED 0x2ull

struct ax_heap_hdr {
    uint64_t size;
    uint64_t flags;
    uint64_t next;
    uint64_t prev;
};

static uint64_t const ax_heap_base = AX_HEAP_PHYS;
static uint64_t const ax_heap_limit = AX_HEAP_PHYS + AX_HEAP_SIZE;

static struct ax_heap_hdr* ax_heap_head = nullptr;
static uint64_t ax_heap_block_count = 0;
static uint64_t ax_heap_used_bytes = 0;

/* Area de staging para modulos (AX_MODSTAGE_PHYS). */
static uint64_t ax_staging_cursor = AX_MODSTAGE_PHYS;

/* ------------------------------------------------------------------ */
/* Utilidades propias                                                   */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

static void ax_memcpy(void* dst, const void* src, uint64_t count)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

/* ------------------------------------------------------------------ */
/* Inicializacion                                                      */
/* ------------------------------------------------------------------ */
void ax_heap_init(void)
{
    /* El heap arranca como un unico bloque libre que ocupa todo el area.
       El area se limpia por si el arranque la ha dejado con basura. */
    ax_memset((void*)ax_heap_base, 0, AX_HEAP_SIZE);

    struct ax_heap_hdr* head = (struct ax_heap_hdr*)ax_heap_base;
    head->size = AX_HEAP_SIZE - sizeof(struct ax_heap_hdr);
    head->flags = AX_HEAP_FREE;
    head->next = 0;
    head->prev = 0;

    ax_heap_head = head;
    ax_heap_block_count = 1;
    ax_heap_used_bytes = 0;
    ax_staging_cursor = AX_MODSTAGE_PHYS;
}

/* ------------------------------------------------------------------ */
/* Asignacion y liberacion                                             */
/* ------------------------------------------------------------------ */
void* ax_malloc(uint64_t size)
{
    if (size == 0) {
        size = 1;
    }
    size = (size + 15ull) & ~15ull;   /*.payload alineado a 16 bytes */

    if (!ax_heap_head) {
        return nullptr;
    }

    for (struct ax_heap_hdr* block = ax_heap_head; block; block = (struct ax_heap_hdr*)block->next) {
        if ((block->flags & AX_HEAP_FREE) && block->size >= size) {
            /* Si sobra mucho espacio, dividimos el bloque. */
            uint64_t overhead = sizeof(struct ax_heap_hdr);
            if (block->size >= size + overhead + 16) {
                struct ax_heap_hdr* split = (struct ax_heap_hdr*)((uint8_t*)block + overhead + size);
                split->size = block->size - size - overhead;
                split->flags = AX_HEAP_FREE;
                split->next = block->next;
                split->prev = (uint64_t)(uintptr_t)block;
                if (split->next) {
                    struct ax_heap_hdr* next = (struct ax_heap_hdr*)split->next;
                    next->prev = (uint64_t)(uintptr_t)split;
                }
                block->next = (uint64_t)(uintptr_t)split;
                block->size = size;
                ++ax_heap_block_count;
            }
            block->flags = AX_HEAP_USED;
            ax_heap_used_bytes += block->size;
            return (void*)((uint8_t*)block + sizeof(struct ax_heap_hdr));
        }
    }
    return nullptr;    /* memoria agotada */
}

void* ax_calloc(uint64_t count, uint64_t size)
{
    if (count && size > (~0ull) / count) {
        return nullptr;   /* desbordamiento */
    }
    uint64_t total = count * size;
    void* memory = ax_malloc(total);
    if (memory) {
        ax_memset(memory, 0, total);
    }
    return memory;
}

void* ax_realloc(void* ptr, uint64_t size)
{
    if (!ptr) {
        return ax_malloc(size);
    }
    if (size == 0) {
        ax_free(ptr);
        return nullptr;
    }

    struct ax_heap_hdr* block = (struct ax_heap_hdr*)((uint8_t*)ptr - sizeof(struct ax_heap_hdr));
    if (block->flags & AX_HEAP_USED) {
        if (block->size >= size) {
            return ptr;
        }
        /* El bloque es mas pequeno que lo pedido: reservamos uno nuevo,
           copiamos el contenido antiguo y liberamos el viejo. */
        void* memory = ax_malloc(size);
        if (memory) {
            ax_memcpy(memory, ptr, block->size);
            ax_free(ptr);
        }
        return memory;
    }
    return nullptr;
}

void ax_free(void* ptr)
{
    if (!ptr) {
        return;
    }

    struct ax_heap_hdr* block = (struct ax_heap_hdr*)((uint8_t*)ptr - sizeof(struct ax_heap_hdr));
    if (!(block->flags & AX_HEAP_USED)) {
        return;     /* doble liberacion: lo ignoramos */
    }

    ax_heap_used_bytes -= block->size;
    block->flags = AX_HEAP_FREE;

    /* Fusion con el bloque siguiente si tambien esta libre. */
    if (block->next) {
        struct ax_heap_hdr* next = (struct ax_heap_hdr*)block->next;
        if (next->flags & AX_HEAP_FREE) {
            block->size += sizeof(struct ax_heap_hdr) + next->size;
            block->next = next->next;
            if (next->next) {
                struct ax_heap_hdr* after = (struct ax_heap_hdr*)next->next;
                after->prev = (uint64_t)(uintptr_t)block;
            }
            --ax_heap_block_count;
        }
    }

    /* Fusion con el bloque anterior si tambien esta libre. */
    if (block->prev) {
        struct ax_heap_hdr* prev = (struct ax_heap_hdr*)block->prev;
        if (prev->flags & AX_HEAP_FREE) {
            prev->size += sizeof(struct ax_heap_hdr) + block->size;
            prev->next = block->next;
            if (block->next) {
                struct ax_heap_hdr* next = (struct ax_heap_hdr*)block->next;
                next->prev = (uint64_t)(uintptr_t)prev;
            }
            --ax_heap_block_count;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Informacion del heap                                                */
/* ------------------------------------------------------------------ */
uint64_t ax_heap_used(void)
{
    return ax_heap_used_bytes;
}

uint64_t ax_heap_total(void)
{
    return AX_HEAP_SIZE;
}

uint64_t ax_heap_blocks(void)
{
    return ax_heap_block_count;
}

int ax_heap_fill_info(AxHeapInfo* out)
{
    if (!out) {
        return -1;
    }
    uint64_t free_blocks = 0;
    for (struct ax_heap_hdr* block = ax_heap_head; block; block = (struct ax_heap_hdr*)block->next) {
        if (block->flags & AX_HEAP_FREE) {
            ++free_blocks;
        }
    }
    out->used = ax_heap_used_bytes;
    out->total = AX_HEAP_SIZE;
    out->blocks = ax_heap_block_count;
    out->free_blocks = free_blocks;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Staging de modulos                                                  */
/* ------------------------------------------------------------------ */
/*
 * El area de staging es un simple bump allocator: los modulos se cargan
 * ahi, se comprueban y despues se copian a su ranura definitiva.  Al
 * reiniciar el staging (ax_staging_reset) la memoria queda disponible otra
 * vez.
 */
void* ax_staging_alloc(uint64_t size)
{
    uint64_t aligned = (ax_staging_cursor + 15ull) & ~15ull;
    if (size == 0) {
        return nullptr;
    }
    if (aligned + size > AX_MODSTAGE_PHYS + AX_MODSTAGE_SIZE) {
        return nullptr;   /* staging lleno */
    }
    ax_staging_cursor = aligned + size;
    return (void*)(uintptr_t)aligned;
}

void ax_staging_reset(void)
{
    ax_staging_cursor = AX_MODSTAGE_PHYS;
}
