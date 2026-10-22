/*
 * AxolynOS - Gestor de memoria fisica (bitmap) y paginacion.
 */
#ifndef AX_MEMORY_H
#define AX_MEMORY_H

#include "ax_types.h"

/* ------------------------------------------------------------------ */
/* PMM - Physical Memory Manager                                        */
/* ------------------------------------------------------------------ */
void     ax_pmm_init(uint64_t mem_size, const ax_e820_entry* entries, uint64_t count);
uint64_t ax_pmm_alloc_pages(uint64_t count);      /* frames contiguos 4 KiB */
uint64_t ax_pmm_alloc_page(void);
void     ax_pmm_free_pages(uint64_t phys, uint64_t count);
void     ax_pmm_reserve(uint64_t phys, uint64_t size);
uint64_t ax_pmm_total_bytes(void);
uint64_t ax_pmm_free_bytes(void);
uint64_t ax_pmm_total_pages(void);
uint64_t ax_pmm_free_pages(void);
uint64_t ax_pmm_reserved_pages(void);
int      ax_pmm_fill_info(AxMemInfo* out);

/* ------------------------------------------------------------------ */
/* VMM - Virtual Memory Manager (4 niveles, unico espacio de direcciones) */
/* ------------------------------------------------------------------ */
void     ax_vmm_init(void);
void*    ax_vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void*    ax_vmm_map_2m(uint64_t virt, uint64_t phys, uint64_t flags);
int      ax_vmm_unmap(uint64_t virt);
uint64_t ax_vmm_translate(uint64_t virt);
int      ax_vmm_protect(uint64_t virt, uint64_t flags);
uint64_t ax_vmm_pml4_phys(void);
uint64_t ax_vmm_low_mapped_bytes(void);

/* ------------------------------------------------------------------ */
/* Heap del kernel (kmalloc)                                            */
/* ------------------------------------------------------------------ */
void     ax_heap_init(void);
void*    ax_malloc(uint64_t size);
void*    ax_calloc(uint64_t count, uint64_t size);
void*    ax_realloc(void* ptr, uint64_t size);
void     ax_free(void* ptr);
uint64_t ax_heap_used(void);
uint64_t ax_heap_total(void);
uint64_t ax_heap_blocks(void);
int      ax_heap_fill_info(AxHeapInfo* out);

/* Region de staging para modulos (dentro de la region reservada). */
void*    ax_staging_alloc(uint64_t size);
void     ax_staging_reset(void);

#endif /* AX_MEMORY_H */
