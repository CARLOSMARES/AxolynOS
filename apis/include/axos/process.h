/*
 * AxolynOS - API de procesos para aplicaciones de user mode.
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_EXIT     (0)  rdi=codigo de salida
 *      AX_SYS_GETPID   (9)  -> pid del proceso actual
 *      AX_SYS_SPAWN    (10) rdi=programa rsi=argv rdx=argc -> pid
 *      AX_SYS_WAIT     (11) rdi=pid -> codigo de salida
 *      AX_SYS_EXITCODE (22) rdi=pid -> codigo de salida
 *      AX_SYS_KILL     (24) rdi=pid
 *      AX_SYS_YIELD    (25) cede el CPU al planificador
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_PROCESS_H
#define AXOS_PROCESS_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codigo de salida del proceso actual (no retorna nunca). */
AX_NORETURN void ax_proc_exit(int code);

/* PID del proceso actual. */
long ax_proc_getpid(void);

/*
 * Lanza otra aplicacion.
 *   path : ruta del ejecutable dentro del sistema de ficheros.
 *   argv : array de punteros a cadenas terminado en NULL (puede ser NULL).
 *   argc : numero de argumentos de argv.
 * Devuelve el PID del proceso nuevo o un valor negativo (-errno).
 */
long ax_proc_spawn(const char* path, char* const argv[], long argc);

/*
 * Espera a que termine un proceso y devuelve su codigo de salida.
 * Devuelve el codigo de salida (>= 0) o un valor negativo (-errno).
 */
long ax_proc_wait(long pid);

/* Consulta el codigo de salida de un proceso sin esperar a el. */
long ax_proc_exit_code(long pid);

/* Envia una senal de terminacion a un proceso. */
long ax_proc_kill(long pid);

/* Cede el CPU al planificador del kernel. */
long ax_proc_yield(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_PROCESS_H */
