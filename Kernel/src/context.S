/*
 * AxolynOS - Cambio de contexto entre hilos y trampolines de entrada.
 *
 * Este fichero es la pieza de mas bajo nivel del planificador: no usa la
 * libc, ni el compilador, ni siquiera una instruccion que no sea un mov de
 * registros.  Todo lo que hace es guardar unos cuantos registros y saltar
 * de una pila a otra.
 *
 * CONVENCION DEL MARCO DE PILA
 * ----------------------------
 * Cuando el planificador cambia de hilo, la pila del hilo nuevo contiene,
 * desde su base (new_rsp) hacia direcciones mas altas:
 *
 *      +0    r15
 *      +8    r14
 *      +16   r13
 *      +24   r12
 *      +32   rbx
 *      +40   rbp
 *      +48   direccion de retorno (destino del "ret")
 *      +56   marco de iretq:  rip
 *      +64                   cs
 *      +72                   rflags
 *      +80                   rsp  (pila del hilo)
 *      +88                   ss
 *
 * El "ret" final de ax_context_switch consume la palabra de +48, de modo que
 * el marco de iretq queda exactamente en +56, que es donde la CPU (o el
 * trampolin de user mode) espera encontrarlo.
 */

    .code64
    .section .text

/* ------------------------------------------------------------------ */
/* ax_context_switch(uint64_t *save_rsp, uint64_t new_rsp)               */
/* ------------------------------------------------------------------ */
/*
 * POR QUE ESTE ORDEN: los seis registros que se guardan son los "callee
 * saved" del ABI de SysV (r15, r14, r13, r12, rbx, rbp).  El resto (rax,
 * rcx, rdx, rsi, rdi, r8-r11) son caller saved: el compilador de cada hilo
 * ya sabe que su valor no sobrevive a una llamada, asi que no hace falta
 * guardarlos.
 *
 * El orden de los push es el inverso de la disposicion del marco: se
 * empuja primero r15 para que quede en la direccion mas baja (+0) y rbp en
 * la mas alta (+40), que es justo lo que declara el contrato.
 */
    .globl ax_context_switch
    .type  ax_context_switch, @function
ax_context_switch:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbx
    pushq %rbp
    movq  %rsp, (%rdi)        /* guardamos el rsp del hilo que sale */
    movq  %rsi, %rsp          /* y saltamos a la pila del hilo nuevo */
    popq  %rbp
    popq  %rbx
    popq  %r12
    popq  %r13
    popq  %r14
    popq  %r15
    ret                       /* -> direccion de retorno (+48) */
    .size ax_context_switch, . - ax_context_switch

/* ------------------------------------------------------------------ */
/* Trampolin de los hilos de kernel                                    */
/* ------------------------------------------------------------------ */
/*
 * El hilo de kernel arranca con la palabra de +48 apuntando aqui.  El
 * trampolin llama a la funcion de entrada en C, que ya sabe cual es el
 * hilo activo y cual es su argumento, y se ocupa de retirar el hilo
 * cuando la funcion de entrada termine.
 *
 * El `subq $8` recupera la alineacion que exige el ABI de SysV: al entrar
 * en una funcion el RSP debe ser congruente con 8 modulo 16 (porque la
 * llamada ha empujado 8 bytes).  El marco de contexto esta alineado a 16
 * bytes, asi que sin esta correccion todas las llamadas del hilo tending
 * la pila desalineada en 8 bytes.
 */
    .globl ax_thread_trampoline
    .type  ax_thread_trampoline, @function
ax_thread_trampoline:
    subq  $8, %rsp
    /*
     * El hilo nuevo arranca dentro de ax_context_switch, que deja IF=0
     * (el cambio de contexto se hace con las interrupciones desactivadas).
     * Hay que reactivarlas: si no, el primer hlt del hilo (por ejemplo el
     * de la shell esperando entrada) se quedaria dormido para siempre
     * porque el temporizador nunca volveria a despertarlo.
     */
    sti
    call  ax_thread_kernel_entry
    /* ax_thread_kernel_entry no retorna (el hilo se retira).  Si algo
       falla, nos quedamos parados aqui en lugar de continuar con un
       marco de pila basura. */
    hlt
    .size ax_thread_trampoline, . - ax_thread_trampoline

/* ------------------------------------------------------------------ */
/* Trampolin de retirada del hilo de arranque                          */
/* ------------------------------------------------------------------ */
/*
 * El arranque (Kernel/src/boot.S) termina con `cli; hlt`: si el hilo de
 * arranque llegase hasta ahi, el sistema se quedaria sin interrupciones
 * (y por tanto sin planificador).  El planificador parchea la direccion de
 * retorno de kmain para que apunte aqui: el hilo se retira del planificador
 * en lugar de volver al arranque.
 */
    .globl ax_boot_park_trampoline
    .type  ax_boot_park_trampoline, @function
ax_boot_park_trampoline:
    subq  $8, %rsp
    call  ax_boot_park
    hlt
    .size ax_boot_park_trampoline, . - ax_boot_park_trampoline

/* ------------------------------------------------------------------ */
/* Trampolin de los hilos de user mode                                 */
/* ------------------------------------------------------------------ */
/*
 * Cuando el hilo entra en user mode, el marco de iretq (rip, cs, rflags,
 * rsp, ss) queda en +56.  Este trampolin solo tiene que ejecutar iretq:
 * la CPU baja a ring 3, restaura la pila del usuario y salta al punto de
 * entrada de la aplicacion.
 */
    .globl ax_user_thread_trampoline
    .type  ax_user_thread_trampoline, @function
ax_user_thread_trampoline:
    iretq
    .size ax_user_thread_trampoline, . - ax_user_thread_trampoline
