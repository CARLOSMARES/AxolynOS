/*
 * AxolynOS - Manejadores de excepciones e IRQ.
 *
 * Reparto de trabajo:
 *   - ax_isr_handler(): punto de entrada comun (lo llama el trampolin de
 *     ensamblador).  Decide si el vector es una excepcion de la CPU, una IRQ
 *     del PIC o la llamada al sistema (0x80).
 *   - ax_irq_handler(): confirma la interrupcion en el PIC y despacha el
 *     temporizador.
 *   - ax_syscall_entry(): ejecuta la llamada al sistema solicitada por un
 *     proceso en user mode.
 */

#include "ax_types.h"
#include "ax_cpu.h"
#include "ax_console.h"
#include "ax_syscall_entry.h"

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Referencias a los subsistemas del kernel                             */
/* ------------------------------------------------------------------ */
/*
 * El punto de entrada de las syscalls (ax_syscall_entry) lo implementa
 * Kernel/src/syscall.cpp con enlace C: el trampolin de isr_stubs.S lo llama
 * por su nombre, sin mangling.
 */

/* Tick del temporizador (Kernel/src/timer.cpp). */
void ax_timer_on_tick(void);

/* ------------------------------------------------------------------ */
/* Ultimo marco de excepcion: lo usa ax_panic_regs() para el volcado.  */
/* ------------------------------------------------------------------ */
ax_interrupt_frame ax_last_exception_frame;
volatile int      ax_last_exception_valid = 0;

/* ------------------------------------------------------------------ */
/* Nombres de las excepciones de la CPU                                 */
/* ------------------------------------------------------------------ */
static const char* const ax_exception_names[32] = {
    "division por cero",
    "debug (punto de parada)",
    "NMI",
    "breakpoint",
    "overflow aritmetico",
    "bound range exceeded",
    "opcode invalido",
    "device not available",
    "doble fallo",
    "coprocessor overrun",
    "TSS invalido",
    "segmento no presente",
    "error de pila",
    "proteccion general (GPF)",
    "fallo de pagina",
    "reservado",
    "error de coma flotante",
    "error de alineacion",
    "error de comprobacion de maquina",
    "error SIMD",
    "error de virtualizacion",
    "error de control de flujo",
    "reservado",
    "reservado",
    "reservado",
    "reservado",
    "reservado",
    "reservado",
    "hypervisor",
    "error de comunicacion con el VMM",
    "error de seguridad",
    "reservado"
};

/* ------------------------------------------------------------------ */
/* Entrada desde el ensamblador                                         */
/* ------------------------------------------------------------------ */
/*
 * El trampolin de Kernel/src/isr_stubs.S necesita un simbolo con enlace C
 * (el prototipo de ax_cpu.h usa enlace C++ y por tanto esta mangling).
 */
extern "C" void ax_isr_handler_c(ax_interrupt_frame* frame)
{
    ax_isr_handler(frame);
}

/* ------------------------------------------------------------------ */
/* Manejador de excepciones                                             */
/* ------------------------------------------------------------------ */
void ax_isr_handler(ax_interrupt_frame* frame)
{
    uint64_t vector = frame->vector;

    /* --- Llamada al sistema (int 0x80) ----------------------------- */
    if (vector == AX_SYSCALL_VECTOR) {
        /* nr = RAX, argumentos = { RDI, RSI, RDX, R10, R8, R9 }. */
        uint64_t args[AX_SYSCALL_FRAME_WORDS];
        args[0] = frame->rdi;
        args[1] = frame->rsi;
        args[2] = frame->rdx;
        args[3] = frame->r10;
        args[4] = frame->r8;
        args[5] = frame->r9;
        args[AX_SYSCALL_RET_INDEX] = 0;

        uint64_t result = ax_syscall_entry(frame->rax, args);

        /* El valor devuelto viaja en RAX: el trampolin lo recoge del marco. */
        frame->rax = result;
        return;
    }

    /* --- IRQ del PIC (vectores 32..47) ----------------------------- */
    if (vector >= AX_IRQ_BASE && vector < AX_IRQ_BASE + 16) {
        ax_irq_handler(frame);
        return;
    }

    /* --- Excepcion de la CPU --------------------------------------- */
    const char* name = (vector < 32) ? ax_exception_names[vector] : "desconocida";
    bool from_user = (frame->cs & 0x03) != 0;

    /* Guardamos el marco para que el panic pueda volcar los registros. */
    ax_last_exception_frame = *frame;
    ax_last_exception_valid = 1;

    ax_printf("\n[CPU] excepcion %llu: %s (error=%llu)\n",
              (unsigned long long)vector, name, (unsigned long long)frame->error);
    if (from_user) {
        ax_printf("[CPU] la excepcion se ha producido en USER MODE (cs=%llx)\n",
                  (unsigned long long)frame->cs);
    }
    if (vector == AX_EXC_PGFAULT) {
        ax_printf("[CPU] CR2 (direccion que provoca el fallo) = %p\n",
                  (void*)(uintptr_t)ax_read_cr2());
        ax_printf("[CPU] bits del codigo de error: p=%llu w=%llu u=%llu\n",
                  (unsigned long long)((frame->error >> 0) & 1),
                  (unsigned long long)((frame->error >> 1) & 1),
                  (unsigned long long)((frame->error >> 2) & 1));
    }

    /* ax_panic_regs() imprime el volcado de registros y detiene la CPU. */
    ax_panic_regs("excepcion no controlada");
}

/* ------------------------------------------------------------------ */
/* Manejador de IRQ                                                     */
/* ------------------------------------------------------------------ */
void ax_irq_handler(ax_interrupt_frame* frame)
{
    uint8_t irq = (uint8_t)(frame->vector - AX_IRQ_BASE);

    switch (irq) {
        case AX_IRQ_TIMER: {
            /* El tick del PIT actualiza el reloj y avisa al planificador. */
            ax_timer_on_tick();
            break;
        }
        case AX_IRQ_KEYBOARD: {
            /* Leemos el codigo de escaneo para acknowledge la IRQ: si no se
               lee el puerto 0x60 la IRQ se volveria a disparar sin parar.
               El modulo de teclado podra sustituir esta lectura. */
            uint8_t status;
            __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x60));
            (void)status;
            break;
        }
        default:
            break;
    }

    /* Confirmamos la interrupcion al PIC (el EOI va al maestro y, si la IRQ
       venia del esclavo, tambien al esclavo). */
    ax_pic_eoi(irq);
}

