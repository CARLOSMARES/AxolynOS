/*
 * AxolynOS - API de tiempo.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_TIME  (8) rdi=0 -> milisegundos de uptime
 *      AX_SYS_SLEEP (7) rdi=milisegundos
 *
 * AX_SYS_TIME devuelve el tiempo de actividad del sistema (uptime) en
 * milisegundos; es un valor monotono que nunca baja.
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_TIME_H
#define AXOS_TIME_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AX_SYS_TIME necesita rdi=0: se pasa explicitamente un 0. */

/* Milisegundos de uptime del sistema. */
long ax_time_uptime_ms(void);

/* Duerme el proceso durante `ms` milisegundos. */
long ax_sleep_ms(unsigned long ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_TIME_H */
