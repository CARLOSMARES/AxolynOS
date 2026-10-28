/*
 * AxolynOS - Senales de proceso (Ctrl+C y compania).
 *
 * Modelo sencillo, adaptado al kernel actual (hilos de user mode sobre un
 * unico espacio de direcciones):
 *
 *   - Cada hilo tiene una mascara de senales pendientes (bit 0 = senal 1).
 *   - Ctrl+C en la consola se traduce en SIGINT para el proceso en primer
 *     plano.
 *   - SIGINT/SIGTERM/SIGQUIT con la accion por defecto terminan el proceso.
 *   - SIG_IGN descarta la senal.
 *   - Un manejador propio (handler != 0/1) no se ejecuta en ring 0: la
 *     lectura de consola se interrumpe con AX_EINTR y la aplicacion puede
 *     consultar la senal con AX_SYS_SIGPENDING.
 *
 * Las syscalls asociadas son AX_SYS_SIGNAL y AX_SYS_SIGSEND.
 */
#ifndef AX_SIGNAL_H
#define AX_SIGNAL_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_SIGHUP   1
#define AX_SIGINT   2
#define AX_SIGQUIT  3
#define AX_SIGILL   4
#define AX_SIGABRT  6
#define AX_SIGFPE   8
#define AX_SIGKILL  9
#define AX_SIGUSR1  10
#define AX_SIGSEGV  11
#define AX_SIGUSR2  12
#define AX_SIGALRM  14
#define AX_SIGTERM  15
#define AX_SIGCHLD  17
#define AX_SIGCONT  18
#define AX_SIGSTOP  19
#define AX_SIG_MAX  32

/* Acciones especiales. */
#define AX_SIG_DFL  0ull
#define AX_SIG_IGN  1ull

void ax_signal_init(void);

/*
 * Envia "sig" al hilo "pid".  SIGKILL termina el proceso de inmediato; el
 * resto queda pendiente hasta que el proceso vuelva a ejecutarse.
 */
int ax_signal_send(uint64_t pid, int sig);

/* Accion para la senal "sig" en el hilo actual (AX_SIG_DFL, AX_SIG_IGN o
   direccion de un manejador de user mode). */
int ax_signal_set(int sig, uint64_t handler);

/* Mascara de senales pendientes / entregadas del hilo actual. */
uint32_t ax_signal_pending_mask(void);
uint32_t ax_signal_taken_mask(void);
bool     ax_signal_is_pending(int sig);
int      ax_signal_take(int sig);

/*
 * Procesa las senales pendientes del hilo actual.  Si una senal tiene la
 * accion por defecto, el hilo termina (no retorna).  Se llama al volver de
 * una syscall y en las esperas de consola.
 */
void ax_signal_check(void);

/* Proceso en primer plano (el que recibe Ctrl+C). */
void     ax_signal_set_foreground(uint64_t pid);
uint64_t ax_signal_foreground(void);
void     ax_signal_foreground_interrupt(int sig);

/* Numero de senal que corresponde a un caracter de control (Ctrl+C = 2). */
int ax_signal_from_char(char c);

#ifdef __cplusplus
}
#endif

#endif /* AX_SIGNAL_H */
