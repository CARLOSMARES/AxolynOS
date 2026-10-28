/*
 * AxolynOS - Arranque del kernel y servicios globales.
 *
 * Secuencia de arranque:
 *   1. Consola VGA + puerto serie y banner inicial.
 *   2. PMM: se reserva la region del kernel y se libera el resto segun la
 *      tabla E820 que entrega el firmware.
 *   3. VMM: se construye la paginacion de 4 niveles (ventana del kernel en
 *      la mitad superior y mapa de identidad bajo) y se carga CR3.
 *   4. Heap del kernel sobre AX_HEAP_PHYS.
 *   5. GDT + TSS, IDT, PIC y temporizador.
 *   6. Se registran los subsistemas de los demas modulos (planificador,
 *      syscalls, IPC, VFS y modulos) si ya estan enlazados.
 *   7. Banner final con el resumen de memoria y se activan las
 *      interrupciones.
 */

#include "ax_types.h"
#include "ax_cpu.h"
#include "ax_memory.h"
#include "ax_console.h"
#include "ax_kernel.h"
#include "ax_config.h"
#include "ax_sched.h"
#include "ax_syscall.h"
#include "ax_ipc.h"
#include "ax_axfs.h"
#include "ax_module.h"
#include "ax_power.h"
#include "ax_pci.h"
#include "ax_disk.h"
#include "ax_audio.h"
#include "ax_video.h"
#include "ax_fs_disk.h"
#include "ax_install.h"
#include "ax_gc.h"
#include "ax_net.h"
#include "ax_keyboard.h"
#include "ax_user.h"
#include "ax_signal.h"

/* ------------------------------------------------------------------ */
/* Pilas propias del kernel                                             */
/* ------------------------------------------------------------------ */
/*
 * El kernel usa tres pilas:
 *
 *  - Pila de trabajo de los manejadores (ax_kernel_stack_top): es la que
 *    consulta isr_stubs.S al entrar en un manejador.  Vive en su propia
 *    region para no pisar la pila del proceso interrumpido.
 *
 *  - Pila de entrada (ax_entry_stack_top): la CPU la usa sola cuando una
 *    interrupcion llega desde user mode (campo rsp0 del TSS).  Es distinta de
 *    la anterior porque, si no, el marco que apila la CPU se solaparia con el
 *    que usa el manejador de C.
 *
 *  - Pila de excepcion IST (ax_ist_stack_top): la usan el NMI y el doble
 *    fallo, para poder diagnosticar el problema cuando la pila principal
 *    esta corrupta.
 */
static uint64_t ax_handler_stack[4096] __attribute__((aligned(16)));   /* 32 KiB */
static uint64_t ax_entry_stack[2048]  __attribute__((aligned(16)));   /* 16 KiB */
static uint64_t ax_ist_stack[2048]    __attribute__((aligned(16)));   /* 16 KiB */

/*
 * Pila de trabajo de los manejadores: la usa Kernel/src/isr_stubs.S.
 * Se declaran dentro de un bloque "extern C" (y no con "extern ... = valor")
 * porque el汇编ador las lee por
 * su nombre, sin mangling, y porque deben estar en .bss: el arranque pone a
 * cero el .bss antes de ejecutar nada.  El valor real de cada cima se
 * calcula en kmain(), una vez que el .bss ya esta a cero.
 */
extern "C" {
uint64_t ax_kernel_stack_top;
uint64_t ax_entry_stack_top;
uint64_t ax_ist_stack_top;
}

/* ------------------------------------------------------------------ */
/* Estado global del kernel                                             */
/* ------------------------------------------------------------------ */
/* Copia estatica del AxBootInfo: boot.S la rellena con el contenido que
   entrega el bootloader en la direccion fisica 0x1000.  Se declara dentro de
   un bloque "extern C" (y no como "extern ... = valor") para que sea una
   definicion en .bss sin aviso del compilador. */
extern "C" {
AxBootInfo ax_boot_info_storage;
}

static AxBootInfo*   ax_boot_info_ptr = nullptr;
static ax_boot_stage_t ax_boot_stage_value = AX_BOOT_STAGE_EARLY;
static uint64_t      ax_boot_time_value = 0;

/* Resumen de memoria (usado por el arranque y por el shell). */
static void ax_meminfo_summary(void);

/* ------------------------------------------------------------------ */
/* Referencias a los subsistemas del kernel                             */
/* ------------------------------------------------------------------ */
/*
 * Los subsistemas (planificador, syscalls, IPC, VFS y gestor de modulos)
 * viven en ficheros propios del kernel.  Se llaman directamente: el kernel
 * no arranca si falta alguno de ellos.
 */

/* Timer (Kernel/src/timer.cpp), enlace C++. */
void ax_timer_init(void);

/* ------------------------------------------------------------------ */
/* Utilidades propias                                                   */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

/* ------------------------------------------------------------------ */
/* Servicios globales                                                   */
/* ------------------------------------------------------------------ */
AxBootInfo* ax_boot_info(void)
{
    return ax_boot_info_ptr;
}

uint64_t ax_boot_time_ms(void)
{
    return ax_boot_time_value;
}

ax_boot_stage_t ax_boot_stage(void)
{
    return ax_boot_stage_value;
}

void ax_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void ax_poweroff(void)
{
    ax_printf("[kernel] apagando...\n");
    /* ACPI PM1a_CNT (S5) via puertos 0xB004/0x604 en QEMU/bochs; mejor esfuerzo. */
    __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604) : "memory");
    __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004) : "memory");
    /* APM legacy */
    __asm__ volatile("mov $0x5307, %%ax; mov $0x0001, %%bx; mov $0x0003, %%cx; out %%ax, %%dx" : : "d"((uint16_t)0xB004) : "ax","bx","cx");
    ax_halt();
}

void ax_reboot(void)
{
    /* Reinicio "a lo clasico": el controlador de teclado (puerto 0x64) reinicia
       la CPU cuando recibe el comando 0xFE. */
    ax_printf("[kernel] reiniciando...\n");
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64) : "memory");

    /* Si el reinicio no funciona, provocamos un triple fault: la CPU no
       puede recuperarse de un fallo de paginacion con IDT invalida. */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3 & ~0x1000ull) : "memory");
    ax_halt();
}

/* ------------------------------------------------------------------ */
/* Arranque                                                             */
/* ------------------------------------------------------------------ */
void ax_kernel_early_init(AxBootInfo* info)
{
    ax_boot_info_ptr = info;
    ax_boot_stage_value = AX_BOOT_STAGE_EARLY;

    /* Consola: primero la VGA, despues el puerto serie.  A partir de aqui
       todo lo que se imprime sale por los dos sitios. */
    ax_serial_init();
    ax_console_init();

    ax_printf("\n");
    ax_printf("=====================================================\n");
    ax_printf("  %s v%s\n", AX_KERNEL_NAME, AX_VERSION_STRING);
    ax_printf("  nucleo de bajo nivel: consola, memoria, CPU y E/S\n");
    ax_printf("=====================================================\n");

    if (info) {
        ax_printf("[boot] memoria detectada: %llu MiB\n",
                  (unsigned long long)(info->mem_size / (1024ull * 1024ull)));
        ax_printf("[boot] initrd en %p (%llu bytes)\n",
                  (void*)(uintptr_t)info->initrd_phys,
                  (unsigned long long)info->initrd_size);
        ax_printf("[boot] entradas E820: %llu\n",
                  (unsigned long long)info->e820_entries);
    }
}

void ax_kernel_init(AxBootInfo* info)
{
    if (!info) {
        info = &ax_boot_info_storage;
    }
    ax_boot_info_ptr = info;

    /* --- Memoria fisica --------------------------------------------- */
    ax_boot_stage_value = AX_BOOT_STAGE_MEMORY;

    const ax_e820_entry* e820 = nullptr;
    uint64_t e820_count = 0;
    if (info->e820_ptr && info->e820_entries) {
        /* El puntero de la tabla E820 es fisico, pero el mapa de identidad
           bajo permite leerlo como si fuera virtual. */
        e820 = (const ax_e820_entry*)(uintptr_t)info->e820_ptr;
        e820_count = info->e820_entries;
    }

    ax_pmm_init(info->mem_size, e820, e820_count);
    ax_printf("[mem] PMM: %llu MiB totales, %llu MiB libres\n",
              (unsigned long long)(ax_pmm_total_bytes() / (1024ull * 1024ull)),
              (unsigned long long)(ax_pmm_free_bytes() / (1024ull * 1024ull)));

    /*
     * La region de user mode se reserva entera: las aplicaciones en ring 3
     * se colocan ahi por el mapa de identidad, de modo que el kernel no debe
     * entregar esos frames a nadie mas.
     */
    ax_pmm_reserve(AX_USER_VIRT_BASE, AX_USER_REGION_END - AX_USER_VIRT_BASE);
    ax_printf("[mem] region de user mode reservada: %llu MiB\n",
              (unsigned long long)((AX_USER_REGION_END - AX_USER_VIRT_BASE) /
                                   (1024ull * 1024ull)));

    /* --- Paginacion ------------------------------------------------- */
    ax_vmm_init();
    ax_printf("[mem] VMM: PML4 en %p, %llu MiB mapeados en identidad\n",
              (void*)(uintptr_t)ax_vmm_pml4_phys(),
              (unsigned long long)(ax_vmm_low_mapped_bytes() / (1024ull * 1024ull)));

    /* --- Heap del kernel -------------------------------------------- */
    ax_heap_init();
    ax_printf("[mem] heap: %llu MiB en %p\n",
              (unsigned long long)(ax_heap_total() / (1024ull * 1024ull)),
              (void*)(uintptr_t)AX_HEAP_PHYS);

    /* --- CPU: GDT, TSS e IDT ----------------------------------------- */
    ax_boot_stage_value = AX_BOOT_STAGE_CPU;

    ax_gdt_init();
    ax_tss_init(ax_entry_stack_top);
    ax_idt_init();
    ax_printf("[cpu] GDT, TSS e IDT instaladas (%d vectores + int 0x80)\n",
              AX_VECTOR_COUNT);

    /* --- PIC y temporizador ----------------------------------------- */
    ax_pic_init();
    ax_timer_init();
    ax_keyboard_init();
    ax_printf("[cpu] PIC remapeado a 0x20-0x2F, PIT a %d Hz, teclado PS/2 (IRQ1)\n",
              AX_TIMER_FREQ_HZ);

    /* --- Subsistemas del kernel --------------------------------------- */
    ax_boot_stage_value = AX_BOOT_STAGE_SCHED;

    ax_sched_init();
    ax_printf("[sub] planificador inicializado\n");

    ax_syscall_init();
    ax_printf("[sub] syscalls inicializadas\n");

    ax_ipc_init();
    ax_printf("[sub] IPC inicializado\n");

    /* --- Sistema de ficheros ------------------------------------------ */
    ax_boot_stage_value = AX_BOOT_STAGE_FS;

    ax_vfs_init();

    /* --- Modulos del kernel ------------------------------------------- */
    ax_boot_stage_value = AX_BOOT_STAGE_MODULES;

    ax_module_init();
    ax_printf("[sub] gestor de modulos inicializado\n");

    /* Los modulos presentes en el initrd se cargan automaticamente. */
    ax_module_autoload();

    /* --- Hardware / Drivers ------------------------------------------- */
    ax_power_init();
    ax_pci_init();
    ax_disk_init();
    ax_audio_init();
    ax_video_init();
    ax_user_init();
    ax_signal_init();
    ax_fs_disk_init();

    /* Si el volumen tiene /etc/passwd se cargan las cuentas; si no, la shell
       pedira crear la primera cuenta antes de mostrar el prompt. */
    ax_user_boot();

    ax_install_init();
    ax_gc_init();
    ax_net_init();

    /* --- User mode ---------------------------------------------------- */
    ax_boot_stage_value = AX_BOOT_STAGE_USER;

    ax_printf("[sub] region de user mode: %p - %p (%u ranuras de %u MiB)\n",
              (void*)(uintptr_t)AX_USER_VIRT_BASE,
              (void*)(uintptr_t)AX_USER_REGION_END,
              (unsigned)AX_USER_MAX_SLOTS,
              (unsigned)(AX_USER_SLOT_SIZE / (1024ull * 1024ull)));

    /* --- Resumen final ----------------------------------------------- */
    ax_boot_time_value = ax_timer_ms();

    ax_meminfo_summary();

    /*
     * El shell del kernel es un hilo mas.  Se crea DESPUES del resumen:
     * en cuanto hay un hilo listo, el siguiente tick del temporizador le
     * cede el CPU y el banner de la shell se mezclaria con las ultimas
     * lineas del arranque.
     */
    ax_shell_init();

    ax_boot_stage_value = AX_BOOT_STAGE_READY;

    /* Por ultimo se activan las interrupciones: hasta aqui el kernel ha
       funcionado con IF=0 para evitar condiciones de carrera. */
    ax_enable_interrupts();

    ax_printf("[kernel] %s listo.\n", AX_KERNEL_NAME);

    /*
     * kmain() no debe retornar: boot.S ejecuta "cli; hlt" al volver y el
     * sistema se quedaria sin temporizador ni teclado.  ax_sched_start()
     * entrega el CPU al primer hilo listo (el shell) y no retorna.
     */
    ax_sched_start();

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* ------------------------------------------------------------------ */
/* Resumen de memoria                                                   */
/* ------------------------------------------------------------------ */
static void ax_meminfo_summary(void)
{
    AxMemInfo mem;
    ax_memset(&mem, 0, sizeof(mem));
    if (ax_pmm_fill_info(&mem) != 0) {
        return;
    }
    mem.low_mapped = ax_vmm_low_mapped_bytes();

    ax_printf("\n");
    ax_printf("  --- memoria ---\n");
    ax_printf("  total      : %llu MiB (%llu paginas)\n",
              (unsigned long long)(mem.total_phys / (1024ull * 1024ull)),
              (unsigned long long)mem.total_pages);
    ax_printf("  libres     : %llu MiB (%llu paginas)\n",
              (unsigned long long)(mem.free_phys / (1024ull * 1024ull)),
              (unsigned long long)mem.free_pages);
    ax_printf("  reservadas : %llu paginas\n", (unsigned long long)mem.reserved_pages);
    ax_printf("  heap       : %llu KiB usados de %llu KiB\n",
              (unsigned long long)(mem.heap_used / 1024ull),
              (unsigned long long)(mem.heap_total / 1024ull));
    ax_printf("  kernel     : %p (fisico %p)\n",
              (void*)(uintptr_t)AX_KERNEL_VIRT_BASE,
              (void*)(uintptr_t)AX_KERNEL_PHYS_BASE);
    ax_printf("  mapa bajo  : %llu MiB en identidad\n",
              (unsigned long long)(mem.low_mapped / (1024ull * 1024ull)));
    ax_printf("\n");
}

/* ------------------------------------------------------------------ */
/* Punto de entrada                                                     */
/* ------------------------------------------------------------------ */
/*
 * El arranque (Kernel/src/boot.S) llama a kmain() con un puntero al AxBootInfo
 * ya copiado en memoria del kernel.  El prototipo se declara con enlace C
 * porque el ensamblador lo llama por su nombre sin mangling.
 */
extern "C" void kmain(AxBootInfo* info)
{
    /* Las cimas de pila se calculan aqui (y no como constantes inicializadas)
       porque el .bss lo pone a cero el arranque: un valor inicializado en
       .data apuntaria a una direccion que decide el enlazador, mientras que
       estas son las direcciones reales de las pilas dentro del kernel. */
    ax_kernel_stack_top = (uint64_t)(uintptr_t)&ax_handler_stack[4096];
    ax_entry_stack_top  = (uint64_t)(uintptr_t)&ax_entry_stack[2048];
    ax_ist_stack_top    = (uint64_t)(uintptr_t)&ax_ist_stack[2048];

    ax_kernel_early_init(info);
    ax_kernel_init(info);
}
