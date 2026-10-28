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
 *      AX_SYS_SIGNAL   (33) rdi=senal rsi=accion
 *      AX_SYS_SIGSEND  (34) rdi=pid rsi=senal
 *      AX_SYS_SIGPENDING (35) rdi=senal (0=mascara)
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 *
 * Senales: Ctrl+C en el primer plano envia AX_SIGINT.  Con la accion por
 * defecto (AX_SIG_DFL) el proceso termina; AX_SIG_IGN la ignora y un
 * manejador propio hace que la lectura de teclado devuelva AX_EINTR.  La
 * aplicacion puede consultar la senal con ax_proc_sigpending().
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

/*
 * Instala la accion para una senal en el proceso actual.
 *   handler = AX_SIG_DFL (por defecto), AX_SIG_IGN (ignorar) o la
 *   direccion de una funcion manejadora (no se ejecuta en ring 0; en su
 *   lugar la lectura de consola devuelve AX_EINTR).
 */
long ax_proc_signal(long sig, unsigned long handler);

/* Envia una senal a otro proceso (o al actual con pid=0). */
long ax_proc_sigsend(long pid, long sig);

/*
 * Consulta una senal pendiente/entregada del proceso actual.
 *   sig = 0  -> mascara de todas las senales (bits 0..31 = senales 1..32)
 *   sig > 0  -> 1 si esa senal esta pendiente, 0 si no
 */
long ax_proc_sigpending(long sig);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_PROCESS_H */
