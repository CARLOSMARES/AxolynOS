/*
 * AxolynOS - Punto de entrada de las llamadas al sistema (int 0x80).
 *
 * Este header existe para que el trampolin de syscalls (Kernel/src/isr_stubs.S)
 * pueda invocar la funcion C sin depender del mangling de C++ y sin colisionar
 * con la declaracion de ax_syscall.h (que usa enlace de C++ y devuelve void).
 *
 * Contrato con el trampolin:
 *   nr   -> numero de syscall, tomado de RAX en el momento del int 0x80.
 *   args -> array de 6 elementos en la pila del kernel con los argumentos
 *           de la llamada en el orden del ABI de SysV:
 *               args[0] = RDI
 *               args[1] = RSI
 *               args[2] = RDX
 *               args[3] = R10   (4º argumento; RAX se usa para el numero)
 *               args[4] = R8
 *               args[5] = R9
 *           El trampolin reserva ademas una septima palabra (args[6]) como
 *           canal de retorno: la implementacion escribe ahi el valor que
 *           el usuario recibira en RAX al volver a user mode.
 *
 * La puerta de la IDT para el vector AX_SYSCALL_VECTOR (0x80) se instala con
 * DPL 3, de modo que el `int 0x80` ejecutado en ring 3 no dispara #GP: es el
 * unico mecanismo por el que una aplicacion en user mode puede entrar en el
 * kernel.
 */
#ifndef AX_SYSCALL_ENTRY_H
#define AX_SYSCALL_ENTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Numero de argumentos de la llamada al sistema. */
#define AX_SYSCALL_ARG_COUNT 6

/* Posicion del canal de retorno dentro del array de argumentos. */
#define AX_SYSCALL_RET_INDEX 6

/* Numero total de palabras que reserva el trampolin para los argumentos. */
#define AX_SYSCALL_FRAME_WORDS (AX_SYSCALL_ARG_COUNT + 1)

/*
 * Ejecuta la llamada al sistema numero "nr" con los argumentos "args" y
 * devuelve el valor que el usuario debe recibir en RAX.
 */
uint64_t ax_syscall_entry(uint64_t nr, uint64_t* args);

#ifdef __cplusplus
}
#endif

#endif /* AX_SYSCALL_ENTRY_H */
