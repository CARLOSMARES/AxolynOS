/*
 * AxolynOS - API de modulos del kernel.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_MODLIST   (14) rdi=AxModInfo* rsi=max -> numero de modulos
 *      AX_SYS_MODLOAD   (15) rdi=nombre -> 0 | -errno
 *      AX_SYS_MODUNLOAD (16) rdi=indice -> 0 | -errno
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_MODULE_H
#define AXOS_MODULE_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lista los modulos cargados en el kernel.
 *   out : array de AxModInfo donde escribir la informacion.
 *   max : numero de entradas que caben en `out`.
 * Devuelve el numero de modulos escritos o -errno.
 */
long ax_mod_list(AxModInfo* out, unsigned long max);

/* Carga un modulo por su nombre. Devuelve 0 o -errno. */
long ax_mod_load(const char* name);

/* Descarga el modulo indicado por su indice. Devuelve 0 o -errno. */
long ax_mod_unload(unsigned int index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_MODULE_H */
