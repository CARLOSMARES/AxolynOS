/*
 * AxolynOS - API de la maquina virtual de bytecode (implementacion).
 */
#include "axos/vm.h"

long ax_vm_run(const char* path, char* const argv[])
{
    if (path == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall2(AX_SYS_VMRUN, (long)path, (long)argv);
}

long ax_vm_stop(long pid)
{
    return ax_syscall1(AX_SYS_VMSTOP, pid);
}

long ax_vm_status(AxVmInfo* out)
{
    if (out == nullptr) {
        return AX_EINVAL;
    }
    return ax_syscall1(AX_SYS_VMSTATUS, (long)out);
}
