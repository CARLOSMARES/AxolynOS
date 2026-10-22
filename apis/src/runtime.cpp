/*
 * AxolynOS - Runtime de user mode.
 *
 * El punto de entrada _start (apis/src/start.S) salta aqui con los
 * argumentos que el kernel dejo en la pila de user mode:
 *
 *      [RSP] = argc
 *      [RSP+8] = argv (array de punteros terminado en NULL)
 *
 * El runtime llama a main(argc, argv) y, cuando vuelve, termina el
 * proceso con la syscall AX_SYS_EXIT usando el codigo devuelto.
 *
 * Si la aplicacion no define main(), se usa la definicion debil de este
 * fichero, que devuelve 1 (error: no hay punto de entrada).
 */
#include "axos/syscall.h"
#include "axos/process.h"

/*
 * Declaracion debil de main: el enlazador usara la definicion fuerte
 * que proporcione la aplicacion. Si no existe ninguna, gana esta.
 */
extern "C" int main(int argc, char** argv) __attribute__((weak));

extern "C" __attribute__((weak)) int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1; /* la aplicacion no define main() */
}

/*
 * Llamada desde _start. No retorna: termina el proceso con el codigo
 * de salida de main().
 */
extern "C" void axos_runtime_start(unsigned long argc, char** argv)
{
    /*
     * Si la aplicacion define main(), el enlazador habra sustituido
     * esta definicion debil por la fuerte. Si no, se usa la de arriba,
     * que devuelve 1.
     */
    int code = main((int)argc, argv);

    ax_proc_exit(code);

    /* ax_proc_exit no retorna; si lo hiciera, no se sale del proceso. */
    for (;;) {
        ax_syscall0(AX_SYS_YIELD);
    }
}
