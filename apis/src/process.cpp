/*
 * AxolynOS - API de procesos (implementacion).
 */
#include "axos/process.h"

AX_NORETURN void ax_proc_exit(int code)
{
    ax_syscall1(AX_SYS_EXIT, code);

    /* AX_SYS_EXIT nunca retorna en el kernel; si lo hiciera, el proceso
     * se quedaria en un bucle infinito en lugar de seguir ejecutando. */
    for (;;) {
        ax_syscall0(AX_SYS_YIELD);
    }
}

long ax_proc_getpid(void)
{
    return ax_syscall0(AX_SYS_GETPID);
}

long ax_proc_spawn(const char* path, char* const argv[], long argc)
{
    if (path == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall3(AX_SYS_SPAWN, (long)path, (long)argv, argc);
}

long ax_proc_wait(long pid)
{
    return ax_syscall1(AX_SYS_WAIT, pid);
}

long ax_proc_exit_code(long pid)
{
    return ax_syscall1(AX_SYS_EXITCODE, pid);
}

long ax_proc_kill(long pid)
{
    return ax_syscall1(AX_SYS_KILL, pid);
}

long ax_proc_yield(void)
{
    return ax_syscall0(AX_SYS_YIELD);
}
