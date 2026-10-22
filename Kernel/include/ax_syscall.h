/*
 * AxolynOS - Capa de llamadas al sistema (syscall).
 *
 * Mecanismo: la aplicacion ejecuta `int 0x80` (vector AX_SYSCALL_VECTOR con
 * DPL 3). El trampolin Kernel/src/isr_stubs.S guarda el contexto, cambia a la
 * pila del kernel y llama a ax_syscall_entry() (ver ax_syscall_entry.h).
 *
 * Los handlers de syscall devuelven 0 en exito o un valor negativo (-errno)
 * en error, y escriben el resultado de la operacion en *ret.
 */
#ifndef AX_SYSCALL_H
#define AX_SYSCALL_H

#include "ax_types.h"
#include "ax_syscall_entry.h"

typedef int (*ax_syscall_handler)(uint64_t* args, uint64_t* ret);

/* Registra un handler; devuelve 0 o -AX_EBUSY si el numero ya existe. */
int  ax_syscall_register(uint64_t nr, ax_syscall_handler handler);

/*
 * Reparte la llamada: busca el handler registrado para "nr" y lo invoca.
 * Si no hay handler, se consulta al anfitrion de la maquina virtual y, si
 * tampoco lo sabe, devuelve -AX_ENOSYS.
 *
 *   args = { RDI, RSI, RDX, R10, R8, R9, <canal de retorno> }
 */
int  ax_syscall_dispatch(uint64_t nr, uint64_t* args, uint64_t* ret);

void ax_syscall_init(void);
uint64_t ax_syscall_count(void);

/* Registro de contexto de una syscall (lo rellena el trampolin). */
typedef struct {
    uint64_t nr;
    uint64_t args[6];
    uint64_t ret;
    uint64_t user_rsp;
    int      from_user;
} ax_syscall_context;

/* Puente con el gestor de procesos de la VM (apps de bytecode). */
void ax_syscall_set_vm_dispatch(int (*fn)(uint64_t nr, uint64_t* args, uint64_t* ret));

#endif /* AX_SYSCALL_H */
