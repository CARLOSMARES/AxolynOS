/*
 * AxolynOS - Informacion de memoria y heap del kernel.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_MEMINFO  (19) rdi=AxMemInfo*
 *      AX_SYS_HEAPINFO (20) rdi=AxHeapInfo*
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_MEM_H
#define AXOS_MEM_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Informacion general del gestor de memoria fisico. */
long ax_mem_info(AxMemInfo* out);

/* Informacion del heap del kernel. */
long ax_heap_info(AxHeapInfo* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_MEM_H */
