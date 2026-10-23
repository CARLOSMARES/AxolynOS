/*
 * AxolynOS - Programa para apagar el sistema
 */
#include <axos/syscall.h>

extern "C" int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    ax_syscall6(AX_SYS_POWEROFF, 0, 0, 0, 0, 0, 0);
    return 0;
}
