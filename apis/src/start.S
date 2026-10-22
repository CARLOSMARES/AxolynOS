/*
 * AxolynOS - Punto de entrada de las aplicaciones en user mode.
 *
 * Contrato con el kernel (ver apis/include/axos/abi.h):
 *
 *      Al saltar a e_entry, el kernel deja en la pila de user mode:
 *
 *          [RSP+0] = argc
 *          [RSP+8] = argv  (array de punteros a cadenas, terminado en NULL)
 *
 * _start lee esos dos valores, alinea la pila a 16 bytes (requisito del
 * ABI de SysV: justo antes de `call` el RSP debe estar alineado a 16) y
 * llama a axos_runtime_start(), que a su vez invoca main(argc, argv) y
 * termina el proceso con AX_SYS_EXIT.
 *
 * Se ensambla con:
 *      gcc -c -o start.o start.S
 */

    .section .text.entry, "ax"
    .globl  _start
    .type   _start, @function
    .align  16
_start:
    /* --- recupera argc y argv de la pila ------------------------- */
    movq   (%rsp), %rdi          /* rdi = argc                      */
    leaq   8(%rsp), %rsi         /* rsi = argv (array terminado en 0) */

    /*
     * Alinea la pila a 16 bytes HACIA ABAJO (and $-16). Alinear hacia
     * arriba dejaria el RSP dentro del bloque [argc][argv] y el `call`
     * pisaria argv[0]. Como los valores ya estan en rdi/rsi, no hace
     * falta volver a leerlos de la pila.
     */
    andq   $-16, %rsp

    /* --- llama al runtime de libaxos ----------------------------- */
    call   axos_runtime_start

    /*
     * axos_runtime_start no deberia retornar (termina el proceso con
     * AX_SYS_EXIT). Si lo hiciera, se sale con el codigo 1.
     */
    movl   $1, %edi
    movl   $0, %eax
    int    $0x80
    hlt
    .size  _start, . - _start

    .section .note.GNU-stack, "", @progbits
