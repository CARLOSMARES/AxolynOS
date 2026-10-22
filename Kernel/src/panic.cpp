/*
 * AxolynOS - Panic del kernel.
 *
 * ax_panic() y ax_panic_regs() son la ultima linea de defensa: imprimen el
 * diagnostico completo (incluido el volcado de registros) y detienen la CPU.
 * Nunca retornan.
 */

#include "ax_types.h"
#include "ax_cpu.h"
#include "ax_console.h"

/* Ultimo marco de excepcion guardado por Kernel/src/isr.cpp. */
extern ax_interrupt_frame ax_last_exception_frame;
extern volatile int      ax_last_exception_valid;

/* ------------------------------------------------------------------ */
/* Detencion de la CPU                                                 */
/* ------------------------------------------------------------------ */
[[noreturn]] static void ax_panic_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/* ------------------------------------------------------------------ */
/* Volcado de registros                                                 */
/* ------------------------------------------------------------------ */
static void ax_dump_frame(const ax_interrupt_frame* frame)
{
    ax_printf("  RIP=%p  CS=%p  RFLAGS=%p  RSP=%p  SS=%p\n",
              (void*)(uintptr_t)frame->rip, (void*)(uintptr_t)frame->cs,
              (void*)(uintptr_t)frame->rflags, (void*)(uintptr_t)frame->rsp,
              (void*)(uintptr_t)frame->ss);
    ax_printf("  RAX=%p  RBX=%p  RCX=%p  RDX=%p\n",
              (void*)(uintptr_t)frame->rax, (void*)(uintptr_t)frame->rbx,
              (void*)(uintptr_t)frame->rcx, (void*)(uintptr_t)frame->rdx);
    ax_printf("  RSI=%p  RDI=%p  RBP=%p\n",
              (void*)(uintptr_t)frame->rsi, (void*)(uintptr_t)frame->rdi,
              (void*)(uintptr_t)frame->rbp);
    ax_printf("  R8 =%p  R9 =%p  R10=%p  R11=%p\n",
              (void*)(uintptr_t)frame->r8, (void*)(uintptr_t)frame->r9,
              (void*)(uintptr_t)frame->r10, (void*)(uintptr_t)frame->r11);
    ax_printf("  R12=%p  R13=%p  R14=%p  R15=%p\n",
              (void*)(uintptr_t)frame->r12, (void*)(uintptr_t)frame->r13,
              (void*)(uintptr_t)frame->r14, (void*)(uintptr_t)frame->r15);
    ax_printf("  vector=%llu  error=%llu\n",
              (unsigned long long)frame->vector, (unsigned long long)frame->error);
    if ((frame->cs & 0x03) != 0) {
        ax_printf("  >>> el fallo se produjo en USER MODE (anillo %llu)\n",
                  (unsigned long long)(frame->cs & 0x03));
    }
}

[[noreturn]] void ax_panic_regs(const char* what)
{
    ax_printf("\n==================================================\n");
    ax_printf("  KERNEL PANIC: %s\n", what ? what : "(sin informacion)");
    ax_printf("==================================================\n");

    if (ax_last_exception_valid) {
        ax_printf("Registros del ultimo fallo:\n");
        ax_dump_frame(&ax_last_exception_frame);
    } else {
        ax_printf("No hay marco de excepcion guardado.\n");
    }

    ax_printf("CR0=%p  CR2=%p  CR3=%p  CR4=%p\n",
              (void*)(uintptr_t)ax_read_cr0(), (void*)(uintptr_t)ax_read_cr2(),
              (void*)(uintptr_t)ax_read_cr3(), (void*)(uintptr_t)ax_read_cr4());
    ax_printf("El kernel se detiene.\n");
    ax_panic_halt();
}

[[noreturn]] void ax_panic(const char* file, int line, const char* fmt, ...)
{
    ax_printf("\n==================================================\n");
    ax_printf("  KERNEL PANIC en %s:%d\n", file ? file : "?", line);
    ax_printf("==================================================\n");

    if (fmt) {
        va_list args;
        va_start(args, fmt);
        ax_vprintf(fmt, args);
        va_end(args);
        ax_printf("\n");
    }

    /* El volcado de registros lo hace ax_panic_regs(), que reutiliza el
       ultimo marco de excepcion guardado por el manejador (si lo hay). */
    ax_panic_regs("asercion o error fatal");
}
