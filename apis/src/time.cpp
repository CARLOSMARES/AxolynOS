/*
 * AxolynOS - API de tiempo (implementacion).
 */
#include "axos/time.h"

long ax_time_uptime_ms(void)
{
    /* AX_SYS_TIME espera rdi=0 (vease la nota en axos/time.h). */
    return ax_syscall1(AX_SYS_TIME, 0);
}

long ax_sleep_ms(unsigned long ms)
{
    return ax_syscall1(AX_SYS_SLEEP, (long)ms);
}
