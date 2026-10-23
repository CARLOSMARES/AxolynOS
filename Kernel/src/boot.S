/*
 * AxolynOS - Punto de entrada del kernel (GAS, sintaxis AT&T, 64 bits).
 *
 * El bootloader salta a _start (direccion virtual 0xFFFFFFFF80200000) una vez
 * ha activado la paginacion de 64 bits, y deja en RDI la direccion FISICA del
 * AxBootInfo (0x1000 por defecto).
 *
 * Responsabilidades de este fichero:
 *   1. Poner a cero el BSS (__bss_start .. __bss_end) antes de tocar ninguna
 *      variable estatica (el C++ espera .bss a cero).
 *   2. Construir unas tablas de pagina TEMPORALES con las que se mapea
 *      identidad la memoria baja. Sin esto no se puede leer el AxBootInfo
 *      fisico en 0x1000, porque en long mode todo acceso pasa por las tablas
 *      del bootloader y solo se puede garantizar el mapeo de la ventana del
 *      kernel. Las tablas temporales viven en el propio .bss del kernel
 *      (ver Kernel/linker/kernel.ld) y se descartan cuando ax_vmm_init()
 *      carga la PML4 definitiva.
 *   3. Copiar el AxBootInfo a una variable estatica del kernel.
 *   4. Montar la pila inicial sobre __kernel_stack_top y llamar a kmain().
 *
 * Convenios: todo en AT&T (origen, destino), sufijos de tamaño explicitos y
 * comentarios en español explicando el "por que" de cada decision.
 */

.code64

.section .text.boot, "ax"
.globl _start
.type _start, @function

/* Simbolos-exportados por Kernel/linker/kernel.ld */
.extern __bss_start
.extern __bss_end
.extern __kernel_stack_top
.extern __tmp_ptables
.extern __tmp_pml4_phys

/* Simbolos definidos en el propio kernel (C++) */
.extern kmain                  /* extern "C" en Kernel/src/kernel.cpp        */
.extern ax_boot_info_storage  /* copia estatica del AxBootInfo              */

/* CR3 original del bootloader: se conserva por diagnostico. */
.section .data
.align 8
.globl ax_boot_cr3
ax_boot_cr3:
    .quad 0

/* El codigo vuelve a .text.boot: si no, todo lo que sigue a la etiqueta
   _start se ensambla dentro de .data (seccion de escritura) y el punto de
   entrada termina en el segmento de datos. */
.section .text.boot, "ax"

_start:
    cli                         /* Sin interrupciones hasta que el kernel este listo */
    cld                         /* DF=0: las cadenas se recorren hacia adelante   */

    /* RDI trae la direccion fisica del AxBootInfo.  Hay que salvarla ANTES
       de usar RDI para poner a cero el .bss (rep stosb lo deja apuntando al
       final), o el kernel acabaria copiando basura.  Se usa R12, que no se
       toca en todo el arranque. */
    movq %rdi, %r12

    /* --------------------------------------------------------------- */
    /* 0. Habilitar SSE/SSE2                                           */
    /* --------------------------------------------------------------- */
    /* GCC en x86-64 genera instrucciones SSE de forma rutinaria (por
       ejemplo movd/pshufd para rellenar buffers en ax_console_clear).
       Aunque el hardware las soporta siempre en long mode, la CPU las
       prohibe (#UD) mientras CR4.OSFXSR/OSXMMEXCPT esten a 0.  Tambien
       hay que dejar CR0.EM=0 y CR0.TS=0 para que no se interprete como
       una FPU emulada.  Se hace antes de ejecutar cualquier codigo C. */
    movq %cr0, %rax
    andq $-13, %rax             /* limpia EM (bit 2) y TS (bit 3) */
    orq  $0x02, %rax            /* MP (bit 1) = 1                 */
    movq %rax, %cr0
    movq %cr4, %rax
    orq  $0x600, %rax           /* OSFXSR (9) | OSXMMEXCPT (10)   */
    movq %rax, %cr4

    /* --------------------------------------------------------------- */
    /* 1. Poner a cero el BSS                                          */
    /* --------------------------------------------------------------- */
    /* Se hace antes de leer o escribir cualquier variable estatica: el
       enlazador solo garantiza que .data tiene su valor inicial, .bss
       llega a memoria con contenido indeterminado. */
    leaq __bss_start(%rip), %rdi
    leaq __bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    /* --------------------------------------------------------------- */
    /* 2. Tablas de pagina temporales                                  */
    /* --------------------------------------------------------------- */
    /* Guardamos el CR3 del bootloader por si hay que depurar. */
    movq %cr3, %rax
    movq %rax, ax_boot_cr3(%rip)

    /* RAX = base de las tablas temporales. Las entradas de tabla de pagina
       y el propio CR3 deben contener direcciones FISICAS, no virtuales: la
       MMU no aplica la traduccion al recorrer las tablas. El kernel esta
       enlazado en 0xFFFFFFFF80200000 y cargado en 0x200000, asi que
       restando 0xFFFFFFFF80000000 se obtiene la fisica. La identidad baja
       del bootloader sigue activa, de modo que escribir en esa direccion
       fisica es valido hasta que se carga el nuevo CR3. */
    leaq __tmp_ptables(%rip), %rbx
    movabsq $0xFFFFFFFF80000000, %r11
    subq %r11, %rbx

    /* Estructura de las tablas (todas a 4 KiB):
         base + 0x0000 : PML4
         base + 0x1000 : PDPT low  (cubre 0x00000000 - 0x7FFFFFFF)
         base + 0x2000 : PD   low  (cubre 0x00000000 - 0x3FFFFFFF)
         base + 0x3000 : PDPT high (cubre 0xFFFFFF8000000000+)
         base + 0x4000 : PD   high (cubre la ventana del kernel)
       El .bss ya esta a cero, asi que solo hay que escribir las entradas
       que interesan. */

    /* --- PML4 ------------------------------------------------------- */
    /* PML4[0] -> PDPT low */
    leaq 0x1000(%rbx), %rax
    orq  $0x03, %rax              /* presente | escritura */
    movq %rax, 0(%rbx)
    /* PML4[511] -> PDPT high (0xFFFFFFFF00000000+) */
    leaq 0x3000(%rbx), %rax
    orq  $0x03, %rax
    movq %rax, 4088(%rbx)         /* 511 * 8 = 4088 */

    /* --- PDPT low --------------------------------------------------- */
    /* PDPT[0] -> PD low (0x00000000 - 0x3FFFFFFF) */
    leaq 0x2000(%rbx), %rax
    orq  $0x03, %rax
    movq %rax, 0x1000(%rbx)

    /* --- PDPT high -------------------------------------------------- */
    /* PDPT[510] -> PD high: 0xFFFFFF8000000000 - 0xFFFFFFBFFFFFFFFF.
       El indice 510 es el que cubre la ventana del kernel en 0xFFFFFFFF80200000. */
    leaq 0x4000(%rbx), %rax
    orq  $0x03, %rax
    movq %rax, 0x3FF0(%rbx)       /* 0x3000 + 510*8 */

    /* --- PD low: identidad de los primeros 64 MiB -------------------- */
    /* Paginas de 2 MiB (bit PS = 0x80). Se mapean identidad para poder leer
       el AxBootInfo en 0x1000, la tabla E820 y la imagen del kernel. */
    leaq 0x2000(%rbx), %rcx       /* base del PD low */
    xorl %r8d, %r8d               /* indice */
    xorq %r9, %r9                 /* direccion fisica: i * 2 MiB */
    movl $32, %r10d               /* 32 entradas de 2 MiB = 64 MiB */
0:
    movq %r9, %rax
    orq  $0x83, %rax              /* presente | escritura | PS (2 MiB) */
    movq %rax, (%rcx,%r8,8)
    addq $0x200000, %r9
    incq %r8
    cmpq %r10, %r8
    jb   0b

    /* --- PD high: ventana del kernel --------------------------------- */
    /* 0xFFFFFFFF80200000 -> 0x200000, 32 MiB en paginas de 2 MiB
       (16 entradas, PD[1] .. PD[16]). */
    leaq 0x4000(%rbx), %rcx
    movq $0x200000, %r9           /* fisica inicial */
    movl $1, %r8d                 /* PD[1] */
    movl $17, %r10d
1:
    movq %r9, %rax
    orq  $0x83, %rax
    movq %rax, (%rcx,%r8,8)
    addq $0x200000, %r9
    incq %r8
    cmpq %r10, %r8
    jb   1b

    /* Activamos las tablas nuevas. El codigo que se esta ejecutando vive en
       la ventana del kernel, que tambien esta mapeada en ellas, asi que el
       cambio de CR3 no invalida la ejecucion en curso.

       La direccion fisica de la PML4 temporal es un valor absoluto pequeño
       (dentro de la imagen del kernel), por eso se carga como inmediato de
       32 bits con signo (R_X86_64_32S) en lugar de con un desplazamiento
       relativo: el valor no cabe en un desplazamiento RIP-relativo de 32
       bits porque la direccion virtual del kernel esta en la mitad alta. */
    movq $__tmp_pml4_phys, %rax
    movq %rax, %cr3

    /* --------------------------------------------------------------- */
    /* 3. Copiar el AxBootInfo                                         */
    /* --------------------------------------------------------------- */
    /* RDI es la direccion fisica que entrega el bootloader. Se conservo en
       %r12 al entrar en _start (el bucle del .bss destruye RDI). */
    movq %r12, %rsi
    testq %rsi, %rsi
    jnz  2f
    movq $0x1000, %rsi             /* valor por defecto del contrato */
2:
    leaq ax_boot_info_storage(%rip), %rdi
    movq $9, %rcx                 /* sizeof(AxBootInfo) = 9 * 8 bytes */
    rep movsq

    /* --------------------------------------------------------------- */
    /* 4. Pila inicial y llamada a kmain()                            */
    /* --------------------------------------------------------------- */
    leaq __kernel_stack_top(%rip), %rsp
    xorl %ebp, %ebp               /* Marco de pila a 0: convencion del ABI */
    leaq ax_boot_info_storage(%rip), %rdi

    call kmain

    /* --------------------------------------------------------------- */
    /* 5. kmain() no deberia retornar; si lo hace, el kernel se para.    */
    /* --------------------------------------------------------------- */
3:
    cli
    hlt
    jmp 3b

.size _start, . - _start
