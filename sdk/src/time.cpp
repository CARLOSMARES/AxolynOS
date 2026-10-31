/*
 * AxolynOS SDK - funciones de tiempo
 */
#include "../include/axos/sdk.h"

int sdk_time_sleep(ax_u32 milisegundos)
{
    return (int)ax_syscall1(AX_SYS_SLEEP, (long)milisegundos);
}