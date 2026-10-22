/*
 * AxolynOS - Trampolines de interrupcion y de llamada al sistema (GAS, AT&T).
 *
 * Convenciones:
 *   - Los vectores 0..31 son excepciones de la CPU, los 32..47 son las IRQ
 *     del PIC 8259 (remapeadas a 0x20..0x2F) y el vector 0x80 es la llamada
 *     al sistema (int 0x80) que ejecutan las aplicaciones de user mode.
 *
 * Diseno del marco (debe coincidir con ax_interrupt_frame de ax_cpu.h):
 *
 *   direccion alta
 *   +----------------------+  <- SS
 *   | RSP                  |     pushes de la CPU
 *   | RFLAGS               |
 *   | CS                   |
 *   | RIP                  |
 *   | codigo de error      |     solo en las excepciones que lo producen
 *   | numero de vector     |     <- lo empujamos nosotros
 *   | RAX RCX RDX RBX RBP  |
 *   | RSI RDI R8..R15      |     <- los empujamos nosotros (orden PUSHA)
 *   +----------------------+  direccion baja
 *
 * El numero de vector se empuja DESPUES del codigo de error, de forma que
 * quede en una direccion mas baja: asi el marco en memoria coincide con la
 * estructura declarada en el contrato.
 */

.code64

.section .text

/* Pila de trabajo de los manejadores: la define Kernel/src/kernel.cpp. */
.extern ax_kernel_stack_top

/* ax_isr_handler(ax_interrupt_frame*) de Kernel/src/isr.cpp.  El prototipo
   esta en ax_cpu.h con enlace C++, por lo que el ensamblador usa el
   envoltorio de enlace C "ax_isr_handler_c". */
.extern ax_isr_handler_c

/* ax_syscall_entry(uint64_t nr, uint64_t* args) de Kernel/include/ax_syscall_entry.h
   (enlace C, declarado en ese header precisamente para poder llamarlo desde
   aqui sin depender del mangling de C++). */
.extern ax_syscall_entry

/* ------------------------------------------------------------------ */
/* Macros de generacion de trampolines                                 */
/* ------------------------------------------------------------------ */
.macro AX_ISR_NOERR vec
    .globl ax_isr_stub_\vec
    .type ax_isr_stub_\vec, @function
ax_isr_stub_\vec:
    pushq $0                    /* codigo de error ficticio: la CPU no lo produce */
    pushq $\vec                 /* numero de vector */
    jmp  ax_isr_common
.endm

.macro AX_ISR_ERR vec
    .globl ax_isr_stub_\vec
    .type ax_isr_stub_\vec, @function
ax_isr_stub_\vec:
    pushq $\vec                 /* la CPU ya ha empujado el codigo de error real */
    jmp  ax_isr_common
.endm

/* ------------------------------------------------------------------ */
/* Excepciones de la CPU (vectores 0..31)                               */
/* ------------------------------------------------------------------ */
AX_ISR_NOERR 0        /* #DE division por cero            */
AX_ISR_NOERR 1        /* #DB debug                        */
AX_ISR_NOERR 2        /* #NM NMI (usa la pila IST)        */
AX_ISR_NOERR 3        /* #BP breakpoint                   */
AX_ISR_NOERR 4        /* #OF overflow                     */
AX_ISR_NOERR 5        /* #BR bound range exceeded          */
AX_ISR_NOERR 6        /* #UD opcode invalido               */
AX_ISR_NOERR 7        /* #NM device not available         */
AX_ISR_ERR   8        /* #DF double fault (pila IST)      */
AX_ISR_NOERR 9        /* reservado (CS obsoleto)          */
AX_ISR_ERR   10       /* #TS TSS invalido                 */
AX_ISR_ERR   11       /* #NP segmento no presente         */
AX_ISR_ERR   12       /* #SS error de pila                */
AX_ISR_ERR   13       /* #GP proteccion general           */
AX_ISR_ERR   14       /* #PF fallo de pagina              */
AX_ISR_NOERR 15       /* reservado                        */
AX_ISR_NOERR 16       /* #MF error de coma flotante       */
AX_ISR_ERR   17       /* #AC error de alineacion          */
AX_ISR_NOERR 18       /* #MC error de maquina              */
AX_ISR_NOERR 19       /* #XM error SIMD                   */
AX_ISR_NOERR 20       /* #VE error de virtualizacion      */
AX_ISR_ERR   21       /* #CP control de flujo             */
AX_ISR_NOERR 22       /* reservado                        */
AX_ISR_NOERR 23       /* reservado                        */
AX_ISR_NOERR 24       /* reservado                        */
AX_ISR_NOERR 25       /* reservado                        */
AX_ISR_NOERR 26       /* reservado                        */
AX_ISR_NOERR 27       /* reservado                        */
AX_ISR_NOERR 28       /* #HV hypervisor                   */
AX_ISR_ERR   29       /* #VC comunicacion con el VMM      */
AX_ISR_ERR   30       /* #SX seguridad                    */
AX_ISR_NOERR 31       /* reservado                        */

/* ------------------------------------------------------------------ */
/* IRQ del PIC 8259 (vectores 32..47, mapped a 0x20..0x2F)            */
/* ------------------------------------------------------------------ */
AX_ISR_NOERR 32       /* IRQ0  timer                      */
AX_ISR_NOERR 33       /* IRQ1  teclado                    */
AX_ISR_NOERR 34       /* IRQ2  cascada (no se genera)     */
AX_ISR_NOERR 35       /* IRQ3  COM2                       */
AX_ISR_NOERR 36       /* IRQ4  COM1                       */
AX_ISR_NOERR 37       /* IRQ5  LPT2                       */
AX_ISR_NOERR 38       /* IRQ6  disquetera                  */
AX_ISR_NOERR 39       /* IRQ7  reservado                   */
AX_ISR_NOERR 40       /* IRQ8  RTC                        */
AX_ISR_NOERR 41       /* IRQ9  reservado                   */
AX_ISR_NOERR 42       /* IRQ10 reservado                   */
AX_ISR_NOERR 43       /* IRQ11 reservado                   */
AX_ISR_NOERR 44       /* IRQ12 reservado                   */
AX_ISR_NOERR 45       /* IRQ13 reservado                   */
AX_ISR_NOERR 46       /* IRQ14 IDE0                       */
AX_ISR_NOERR 47       /* IRQ15 IRQ espurio                */

/* ------------------------------------------------------------------ */
/* Ruta comun de las excepciones y las IRQ                             */
/* ------------------------------------------------------------------ */
.globl ax_isr_common
.type ax_isr_common, @function
ax_isr_common:
    /* Guardamos los 15 registros de proposito general.  El orden es el de
       la instruccion PUSHA (RAX el primero, R15 el ultimo) para que al
       releer la pila de abajo arriba aparezca el mismo orden que declara
       ax_interrupt_frame: r15 en la direccion mas baja y rax en la mas
       alta (desplazamiento 112).  GAS no admite PUSHA en modo 64 bits, de
       ahi la expansion manual. */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* RBX (registro preservado entre llamadas segun el ABI) guarda el
       puntero al marco: sirve para volver a el despues de la llamada a C y
       para devolverlo como primer argumento. */
    movq %rsp, %rbx

    /* Cambio a la pila del kernel.  Asi el manejador de C se ejecuta con una
       pila propia y no sobre la pila que el proceso interrumpido estaba
       usando (que en user mode es la pila del usuario). */
    movq ax_kernel_stack_top(%rip), %r12
    movq %r12, %rsp

    movq %rbx, %rdi                /* ax_interrupt_frame* */
    call ax_isr_handler_c

    /* Volvemos a la pila donde vive el marco y restauramos el contexto. */
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp                 /* numero de vector + codigo de error */
    iretq
.size ax_isr_common, . - ax_isr_common

/* ------------------------------------------------------------------ */
/* Trampolin de llamada al sistema (vector 0x80)                       */
/* ------------------------------------------------------------------ */
/*
 * El vector 0x80 se instala en la IDT con DPL 3 (flags 0xEE): gracias a eso
 * una aplicacion que corre en ring 3 puede ejecutar `int 0x80` sin que la CPU
 * genere un #GP.  Es el unico camino de entrada al kernel desde user mode.
 *
 * El marco se apila igual que en el resto de trampolines, de modo que al
 * final se hace iretq con el marco completo (CS/RFLAGS/RIP/RSP/SS) y la CPU
 * regresa sola al anillo de usuario: no hay que "deshacer" nada del marco de
 * usuario porque el iretq se encarga de restaurar rsp y ss.
 */
.globl ax_syscall_stub
.type ax_syscall_stub, @function
ax_syscall_stub:
    pushq $0                      /* codigo de error (0x80 no produce ninguno) */
    pushq $0x80                   /* numero de vector */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx               /* puntero al marco */

    /* Pila del kernel. */
    movq ax_kernel_stack_top(%rip), %r12
    movq %r12, %rsp

    /* Array de argumentos en la pila del kernel:
         args[0]=RDI  args[1]=RSI  args[2]=RDX  args[3]=R10  args[4]=R8
         args[5]=R9   args[6]=canal de retorno (lo escribe la implementacion)
       Se reservan 64 bytes (multiplo de 16) para que RSP siga alineado a 16
       en el momento de la llamada, como exige el ABI de SysV. */
    subq $64, %rsp
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    movq %rdx, 16(%rsp)
    movq %r10, 24(%rsp)
    movq %r8, 32(%rsp)
    movq %r9, 40(%rsp)
    movq $0, 48(%rsp)

    movq %rax, %rdi               /* nr = RAX (numero de syscall) */
    movq %rsp, %rsi               /* args */
    call ax_syscall_entry

    /* El valor devuelto se guarda en la ranura de RAX del marco (offset
       112) para que sobreviva al popq que restaura los registros. */
    movq %rax, 112(%rbx)

    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    /* Tras el popq, RSP apunta al numero de vector y el marco de RAX esta 8
       bytes por debajo: de ahi recuperamos el valor de retorno. */
    movq -8(%rsp), %rax
    addq $16, %rsp                 /* numero de vector + codigo de error */
    iretq
.size ax_syscall_stub, . - ax_syscall_stub
