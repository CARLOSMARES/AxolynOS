/*
 * AxolynOS SDK - funciones de proceso
 */
#include "../include/axos/sdk.h"

/* Obtener el PID del proceso actual */
int sdk_proceso_obtener_pid(void)
{
    return (int)ax_syscall0(AX_SYS_GETPID);
}

/* Terminar un proceso por PID */
int sdk_proceso_terminar(int pid)
{
    return (int)ax_syscall1(AX_SYS_KILL, (long)pid);
}