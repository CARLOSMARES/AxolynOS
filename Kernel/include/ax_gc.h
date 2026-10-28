/*
 * AxolynOS - Recolector de basura de procesos.
 */
#ifndef AX_GC_H
#define AX_GC_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t runs;               /* numero de pasadas                     */
    uint64_t total_threads;      /* hilos zombis liberados (acumulado)    */
    uint64_t total_slots;        /* ranuras de user mode liberadas        */
    uint64_t total_fds;          /* descriptores cerrados                 */
    uint64_t total_stack_bytes;  /* pilas de kernel liberadas             */
    uint64_t last_threads;
    uint64_t last_slots;
    uint64_t last_fds;
    uint64_t last_stack_bytes;
    uint64_t pending;            /* zombis pendientes ahora mismo         */
} AxGcStats;

void ax_gc_init(void);
int  ax_gc_collect(void);
void ax_gc_stats(AxGcStats* out);
const char* ax_gc_state_name(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_GC_H */
