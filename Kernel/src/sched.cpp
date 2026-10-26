/*
 * AxolynOS - Planificador de hilos (round-robin) y gestion de contextos.
 *
 * Este modulo es el "cerebro" del sistema: decide que hilo se ejecuta, crea
 * los hilos de kernel y de user mode y mantiene la pila de kernel que el
 * trampolin de syscalls necesita para volver a ring 0.
 *
 * DISENO DE LAS PILAS
 * -------------------
 * El arranque (Kernel/src/isr_stubs.S) apila el marco de la CPU y despues
 * salta a la pila indicada por la variable global `ax_kernel_stack_top`
 * para ejecutar el manejador de C.  Por eso cada hilo tiene DOS pilas:
 *
 *   - Pila de kernel (AX_KERNEL_STACK_SIZE bytes): es la que se publica en
 *     `ax_kernel_stack_top`.  Su parte alta la usan los manejadores de C
 *     (interrupciones y syscalls) y su parte baja la usa el propio hilo
 *     cuando ejecuta codigo de kernel.
 *
 *   - Pila de entrada (AX_ENTRY_STACK_SIZE bytes): es la que usa la CPU
 *     para apilar el marco cuando una interrupcion llega desde user mode
 *     (campo rsp0 del TSS).  Tiene que ser POR HILO: si fuera compartida,
 *     el marco de un hilo interrumpido pisaria el de otro y al resumed el
 *     primero restauraria un marco corrupto.
 *
 * La division en dos zonas de la pila de kernel no es un capricho: los
 * manejadores de C arrancan en la cima de la pila y crecen hacia abajo,
 * asi que si el hilo tambien creciese desde la cima los dos se pisarian.
 *
 * ENTREGA DEL CONTROL TRAS EL ARRANQUE
 * ------------------------------------
 * El arranque (Kernel/src/boot.S) termina con `cli; hlt` justo despues de
 * que kmain() retorna: si el hilo de arranque llegase hasta ahi, el sistema
 * se quedaria congelado con las interrupciones desactivadas.  Para evitarlo
 * el planificador:
 *
 *   1. Parchea la direccion de retorno de kmain (que esta en la pila del
 *      arranque) para que, en lugar de volver a boot.S, el hilo de arranque
 *      se retire del planificador con ax_thread_exit().
 *   2. Arranca el planificador en cuanto los subsistemas estan listos
 *      (lo hace el gestor de modulos al final de su inicializacion).
 *
 * Asi el hilo de arranque sigue su ejecucion normal (el resumen final del
 * kernel se imprime igualmente) pero nunca llega al `cli; hlt` final.
 */

#include "ax_types.h"
#include "ax_sched.h"
#include "ax_cpu.h"
#include "ax_memory.h"
#include "ax_console.h"
#include "ax_kernel.h"

/* ------------------------------------------------------------------ */
/* Variables que publica el modulo de arranque                         */
/* ------------------------------------------------------------------ */
/*
 * `ax_kernel_stack_top` la define Kernel/src/kernel.cpp (con enlace C) y es
 * la que consulta el trampolin de syscalls para cambiar a la pila de kernel
 * del hilo activo.  Aqui solo se declara: el arranque ya la aporta.
 */
extern "C" uint64_t ax_kernel_stack_top;

/*
 * `__kernel_stack_top` lo define el script de enlazado (Kernel/linker/kernel.ld):
 * es la cima de la pila inicial del kernel, la misma que usa boot.S para
 * llamar a kmain().  La necesito para parchear la direccion de retorno.
 */
extern "C" uint64_t __kernel_stack_top;

/* Trampolines de Kernel/src/context.S */
extern "C" void ax_thread_trampoline(void);
extern "C" void ax_user_thread_trampoline(void);
extern "C" void ax_boot_park_trampoline(void);

/* Shell del kernel (Kernel/src/shell.cpp). */
void ax_shell_init(void);

/* ------------------------------------------------------------------ */
/* Constantes internas                                                 */
/* ------------------------------------------------------------------ */

/* Pila de entrada de la CPU: 16 KiB bastan de sobra para un marco. */
#define AX_ENTRY_STACK_SIZE 0x4000ull

/*
 * Zona reservada en la cima de la pila de kernel para los manejadores de C.
 * El hilo usa el resto como pila de ejecucion.
 */
#define AX_HANDLER_STACK_SIZE 0x2000ull

/*
 * Ventana virtual donde se mapean las pilas de kernel cuando el gestor de
 * memoria fisica no puede entregarlas ya mapeadas en identidad.  Vive por
 * encima de la ventana de modulos, que es el ultimo region mapeada.
 */
#define AX_STACK_WINDOW_BASE 0xFFFFFFFF84000000ull

/* Milisegundos que dura un tick del temporizador. */
#define AX_TICK_MS (1000ull / (uint64_t)AX_TIMER_FREQ_HZ)

/* Numero de palabras del marco de contexto en la pila. */
#define AX_CTX_WORDS      12
#define AX_CTX_OFF_RET     6      /* +48: direccion de retorno            */
#define AX_CTX_OFF_IRET    7      /* +56: marco de iretq                   */

/* RFLAGS de user mode: IF activado y el bit 1 reservado puesto a 1. */
#define AX_USER_RFLAGS    0x202ull

/* ------------------------------------------------------------------ */
/* Estado del planificador                                             */
/* ------------------------------------------------------------------ */

static AxThread g_threads[AX_MAX_THREADS];
static AxThread* g_current      = nullptr;
static AxThread* g_ready_head   = nullptr;
static AxThread* g_ready_tail   = nullptr;
static AxThread* g_idle         = nullptr;   /* hilo de reserva         */
static uint64_t  g_next_tid     = 1;
static uint64_t  g_uptime_ms    = 0;
static uint64_t  g_ticks        = 0;
static bool      g_started      = false;     /* el planificador ya roda  */
static uint64_t  g_boot_saved_rsp = 0;       /* rsp del contexto de arranque */

/* ------------------------------------------------------------------ */
/* Utilidades internas (el kernel no tiene libc)                       */
/* ------------------------------------------------------------------ */

static void ax_memzero(void* dst, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = 0;
    }
}

static uint64_t ax_strlen(const char* s)
{
    uint64_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

static void ax_strcopy(char* dst, const char* src, uint64_t max)
{
    uint64_t i = 0;
    if (!dst || !max) {
        return;
    }
    if (src) {
        while (i + 1 < max && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

/* Compara dos cadenas de forma binaria. */
static int ax_memeq(const char* a, const char* b, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Cola de hilos listos (lista doblemente enlazada)                    */
/* ------------------------------------------------------------------ */

static void ax_ready_push(AxThread* t)
{
    t->next = nullptr;
    t->prev = g_ready_tail;
    if (g_ready_tail) {
        g_ready_tail->next = t;
    } else {
        g_ready_head = t;
    }
    g_ready_tail = t;
}

static void ax_ready_remove(AxThread* t)
{
    if (t->prev) {
        t->prev->next = t->next;
    } else if (g_ready_head == t) {
        g_ready_head = t->next;
    }
    if (t->next) {
        t->next->prev = t->prev;
    } else if (g_ready_tail == t) {
        g_ready_tail = t->prev;
    }
    t->next = nullptr;
    t->prev = nullptr;
}

static AxThread* ax_ready_pop(void)
{
    AxThread* t = g_ready_head;
    if (t) {
        ax_ready_remove(t);
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* Asignacion de pilas                                                 */
/* ------------------------------------------------------------------ */

/*
 * Reserva la pila de kernel y la de entrada de un hilo.  Devuelve la cima
 * de la pila de kernel (que es la direccion que se publica en
 * ax_kernel_stack_top) o 0 si no hay memoria.
 */
static uint64_t ax_thread_alloc_stacks(uint64_t* entry_top_out)
{
    const uint64_t kpages  = AX_KERNEL_STACK_SIZE / AX_PAGE_SIZE;
    const uint64_t epages  = AX_ENTRY_STACK_SIZE / AX_PAGE_SIZE;
    const uint64_t pages   = kpages + epages;

    uint64_t phys = ax_pmm_alloc_pages(pages);
    if (!phys) {
        return 0;
    }

    uint64_t kstack = 0;

    /*
     * El VMM mantiene un mapa de identidad de la RAM (asi lo hace el
     * arranque), de modo que las paginas del PMM ya son direccionables
     * directamente.  Si no fuera asi, se crean paginas propias en una
     * ventana virtual del kernel.
     */
    if (ax_vmm_translate(phys) == phys) {
        kstack = phys;
    } else {
        kstack = AX_STACK_WINDOW_BASE;
        for (uint64_t i = 0; i < pages; i++) {
            if (!ax_vmm_map(kstack + i * AX_PAGE_SIZE, phys + i * AX_PAGE_SIZE,
                            AX_PAGE_PRESENT | AX_PAGE_WRITE)) {
                ax_pmm_free_pages(phys, pages);
                return 0;
            }
        }
    }

    if (entry_top_out) {
        *entry_top_out = kstack + AX_KERNEL_STACK_SIZE + AX_ENTRY_STACK_SIZE;
    }
    return kstack + AX_KERNEL_STACK_SIZE;
}

/* Libera las pilas de un hilo que ya ha terminado. */
static void ax_thread_free_stacks(AxThread* t)
{
    if (!t || !t->kernel_stack) {
        return;
    }
    const uint64_t pages = (AX_KERNEL_STACK_SIZE + AX_ENTRY_STACK_SIZE) /
                           AX_PAGE_SIZE;
    ax_pmm_free_pages(t->kernel_stack, pages);
    t->kernel_stack = 0;
    t->kernel_stack_top = 0;
}

/* ------------------------------------------------------------------ */
/* Cambio de contexto                                                  */
/* ------------------------------------------------------------------ */

/*
 * Publica la pila del hilo activo.  Es la variable que usa el trampolin de
 * syscalls (y el de interrupciones) para ejecutar el manejador de C con una
 * pila propia del hilo, y el campo rsp0 del TSS, que es donde la CPU
 * apila el marco cuando la interrupcion viene de user mode.
 */
static void ax_publish_stacks(AxThread* t)
{
    if (!t || !t->kernel_stack_top) {
        return;
    }
    ax_kernel_stack_top = t->kernel_stack_top;

    /*
     * La pila de entrada es por hilo: la CPU la usa al volver de ring 3.
     * Solo los hilos de user mode la necesitan; para los de kernel el
     * campo rsp0 no se usa (la CPU apila el marco en la pila actual).
     */
    if (t->is_user) {
        uint64_t entry_top = t->kernel_stack_top + AX_ENTRY_STACK_SIZE;
        ax_tss_set_rsp0(entry_top);
    }
}

/*
 * Prepara el marco inicial de un hilo en su pila y devuelve el rsp que hay
 * que restaurar para que empiece a ejecutarse.
 *
 * user_entry != 0  ->  hilo de user mode: el marco de iretq salta a la
 *                     aplicacion con cs/ss de anillo 3.
 * user_entry == 0  ->  hilo de kernel: la direccion de retorno apunta al
 *                     trampolin que invoca la funcion de entrada.
 */
static uint64_t ax_thread_build_frame(AxThread* t, uint64_t user_entry,
                                      uint64_t user_stack_top)
{
    /*
     * El marco vive en la parte baja de la pila de kernel: la zona alta
     * (AX_HANDLER_STACK_SIZE) queda reservada a los manejadores de C.
     */
    uint64_t frame_base = (t->kernel_stack_top - AX_HANDLER_STACK_SIZE)
                          - (AX_CTX_WORDS * 8);
    uint64_t* frame = (uint64_t*)(uintptr_t)frame_base;

    for (uint64_t i = 0; i < AX_CTX_WORDS; i++) {
        frame[i] = 0;
    }

    if (user_entry) {
        frame[AX_CTX_OFF_RET]  = (uint64_t)(uintptr_t)&ax_user_thread_trampoline;
        frame[AX_CTX_OFF_IRET + 0] = user_entry;                 /* rip */
        frame[AX_CTX_OFF_IRET + 1] = AX_SELECTOR_USER_CODE;      /* cs  */
        frame[AX_CTX_OFF_IRET + 2] = AX_USER_RFLAGS;             /* rflags */
        frame[AX_CTX_OFF_IRET + 3] = user_stack_top;             /* rsp */
        frame[AX_CTX_OFF_IRET + 4] = AX_SELECTOR_USER_DATA;      /* ss  */
    } else {
        frame[AX_CTX_OFF_RET]  = (uint64_t)(uintptr_t)&ax_thread_trampoline;
        /* Marco de iretq de relleno: un hilo de kernel nunca lo usa, pero
           deja la memoria del marco coherente por si alguien lo inspecciona. */
        frame[AX_CTX_OFF_IRET + 0] = 0;
        frame[AX_CTX_OFF_IRET + 1] = AX_SELECTOR_KERNEL_CODE;
        frame[AX_CTX_OFF_IRET + 2] = AX_USER_RFLAGS;
        frame[AX_CTX_OFF_IRET + 3] = t->kernel_stack_top;
        frame[AX_CTX_OFF_IRET + 4] = AX_SELECTOR_KERNEL_DATA;
    }

    return frame_base;
}

/*
 * Cambia del hilo actual al indicado.  El hilo que sale vuelve a la cola
 * de listos (si estaba en ejecucion) y el nuevo pasa a RUNNING.
 *
 * El estado de las interrupciones se restaura tal y como estaba: si el
 * cambio se hizo desde un manejador de interrupcion, el hilo vuelve con
 * IF=0, que es lo que el manejador espera.
 */
static void ax_switch_to(AxThread* next)
{
    if (!next || next == g_current) {
        return;
    }

    bool ints = ax_interrupts_enabled();
    ax_disable_interrupts();

    AxThread* prev = g_current;
    if (prev && prev->state == AX_THREAD_RUNNING) {
        prev->state = AX_THREAD_READY;
        ax_ready_push(prev);
    }

    g_current = next;
    next->state = AX_THREAD_RUNNING;
    ax_publish_stacks(next);

    if (prev) {
        ax_context_switch(&prev->ctx_rsp, next->ctx_rsp);
    } else {
        ax_context_switch(&g_boot_saved_rsp, next->ctx_rsp);
    }

    /* Hemos vuelto: restauramos el estado de las interrupciones. */
    if (ints) {
        ax_enable_interrupts();
    }
}

/*
 * Elige el siguiente hilo a ejecutar: el primero de la cola de listos y,
 * si no hay ninguno, el hilo idle (que solo espera interrupciones).
 */
static AxThread* ax_pick_next(void)
{
    AxThread* t = ax_ready_pop();
    if (t) {
        return t;
    }
    if (g_idle && g_idle != g_current && g_idle->state != AX_THREAD_ZOMBIE) {
        return g_idle;
    }
    return nullptr;
}

/* Espera pasiva hasta la proxima interrupcion (usado por el hilo idle). */
static void ax_idle_wait(void)
{
    if (ax_interrupts_enabled()) {
        __asm__ __volatile__("hlt");
    } else {
        __asm__ __volatile__("pause");
    }
}

/* Hilo de reserva: solo espera interrupciones cuando no hay nadie mas. */
static void ax_idle_thread(void* arg)
{
    (void)arg;
    for (;;) {
        ax_idle_wait();
        /* Si hay alguien en la cola, le toca correr. */
        if (g_ready_head) {
            ax_thread_yield();
        }
    }
}

/* ------------------------------------------------------------------ */
/* API del planificador                                                */
/* ------------------------------------------------------------------ */

void ax_sched_init(void)
{
    ax_memzero(g_threads, sizeof(g_threads));
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        g_threads[i].state = AX_THREAD_FREE;
    }

    g_ready_head   = nullptr;
    g_ready_tail   = nullptr;
    g_current      = nullptr;
    g_idle         = nullptr;
    g_next_tid     = 1;
    g_uptime_ms    = 0;
    g_ticks        = 0;
    g_started      = false;

    /*
     * El contexto de arranque (el codigo que llama a kmain) se registra como
     * un hilo mas, con identificador 0.  No tiene pila propia de ejecucion:
     * usa la pila inicial del kernel (la del .bss, la misma que preparo
     * boot.S).  Para los manejadores de C si necesita una pila: usa la pila
     * de trabajo que el arranque ya tenía publicada.
     */
    AxThread* boot = &g_threads[0];
    boot->id       = 0;
    boot->state    = AX_THREAD_RUNNING;
    boot->priority = 0;
    boot->kernel_stack     = 0;
    boot->kernel_stack_top = ax_kernel_stack_top;
    boot->is_user  = 0;
    boot->vm_id    = 0;
    ax_strcopy(boot->name, "main", AX_THREAD_NAME_LEN);
    g_current = boot;

    /*
     * Parcheamos la direccion de retorno de kmain: en lugar de volver a
     * boot.S (que acaba en `cli; hlt` y congelaria el sistema), el hilo de
     * arranque se retira del planificador.
     */
    {
        /* __kernel_stack_top es la parte alta de la pila inicial (creciente
         * hacia abajo). El marco de llamada guardado por la convención está
         * por debajo del tope; usamos un offset positivo que caiga dentro de
         * la pila del .bss para parchear la dirección de retorno. */
        uint64_t* slot = (uint64_t*)(uintptr_t)&__kernel_stack_top - 16;
        slot[0] = (uint64_t)(uintptr_t)&ax_boot_park_trampoline;
    }

    /*
     * Hilo idle: garantiza que siempre hay alguien a quien ceder el CPU.
     * Se crea aqui (y no se encola) para que solo se ejecute cuando no hay
     * ningun otro hilo listo.
     */
    g_idle = ax_thread_create_kernel("idle", ax_idle_thread, nullptr);
    if (g_idle) {
        /* El idle no se encola: es el ultimo recurso. */
        ax_ready_remove(g_idle);
    }

    /*
     * El arranque activa las interrupciones al final de su secuencia, pero
     * el planificador necesita que el tick del temporizador empiece a
     * correr para poder ceder el CPU y para despertar a los hilos dormidos.
     * Se activan aqui: hasta que exista un hilo listo el tick solo actualiza
     * el reloj.
     */
    ax_enable_interrupts();
}

AxThread* ax_thread_create_kernel(const char* name, void (*entry)(void*), void* arg)
{
    AxThread* t = nullptr;
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        if (g_threads[i].state == AX_THREAD_FREE) {
            t = &g_threads[i];
            break;
        }
    }
    if (!t) {
        return nullptr;
    }

    ax_memzero(t, sizeof(*t));

    uint64_t entry_top = 0;
    uint64_t kstack_top = ax_thread_alloc_stacks(&entry_top);
    if (!kstack_top) {
        t->state = AX_THREAD_FREE;
        return nullptr;
    }

    t->id               = g_next_tid++;
    t->state            = AX_THREAD_READY;
    t->priority         = 0;
    t->entry            = (void*)(uintptr_t)entry;
    t->arg              = arg;
    t->kernel_stack     = kstack_top - AX_KERNEL_STACK_SIZE;
    t->kernel_stack_top = kstack_top;
    t->user_stack       = 0;
    t->user_stack_top   = 0;
    t->is_user          = 0;
    t->vm_id            = 0;

    ax_strcopy(t->name, name ? name : "hilo", AX_THREAD_NAME_LEN);

    /*
     * El hilo ejecuta su codigo en la parte baja de la pila de kernel; la
     * cima queda reservada para los manejadores de C.
     */
    t->ctx_rsp = ax_thread_build_frame(t, 0, 0);

    ax_ready_push(t);
    return t;
}

AxThread* ax_thread_create_user(const char* name, uint64_t entry, uint64_t user_stack_top)
{
    AxThread* t = nullptr;
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        if (g_threads[i].state == AX_THREAD_FREE) {
            t = &g_threads[i];
            break;
        }
    }
    if (!t) {
        return nullptr;
    }

    ax_memzero(t, sizeof(*t));

    uint64_t entry_top = 0;
    uint64_t kstack_top = ax_thread_alloc_stacks(&entry_top);
    if (!kstack_top) {
        t->state = AX_THREAD_FREE;
        return nullptr;
    }

    t->id               = g_next_tid++;
    t->state            = AX_THREAD_READY;
    t->priority         = 0;
    t->kernel_stack     = kstack_top - AX_KERNEL_STACK_SIZE;
    t->kernel_stack_top = kstack_top;
    t->user_stack       = user_stack_top - AX_USER_STACK_SIZE;
    t->user_stack_top   = user_stack_top;
    t->is_user          = 1;
    t->vm_id            = 0;

    ax_strcopy(t->name, name ? name : "user", AX_THREAD_NAME_LEN);

    t->ctx_rsp = ax_thread_build_frame(t, entry, user_stack_top);

    ax_ready_push(t);
    return t;
}

void ax_thread_exit(int code)
{
    AxThread* cur = ax_current_thread();
    if (!cur) {
        return;
    }

    cur->exit_code = code;
    cur->state     = AX_THREAD_ZOMBIE;
    cur->wake_at_ms = 0;
    ax_ready_remove(cur);

    AxThread* next = ax_pick_next();
    if (next) {
        ax_switch_to(next);
    }

    /* Si no hay nadie mas, el hilo se queda parado: ya no puede volver. */
    for (;;) {
        ax_idle_wait();
    }
}

/* Libera un hilo zombi (pila incluida) y devuelve la ranura a la tabla. */
void ax_thread_reap(AxThread* thread)
{
    if (!thread || thread == g_current) {
        return;
    }
    if (thread->state != AX_THREAD_ZOMBIE) {
        return;
    }
    ax_thread_free_stacks(thread);
    ax_memzero(thread, sizeof(*thread));
    thread->state = AX_THREAD_FREE;
}

void ax_thread_yield(void)
{
    AxThread* cur = ax_current_thread();
    if (!cur) {
        return;
    }

    if (cur == g_idle) {
        /* El idle no cede el CPU a nadie: espera la proxima interrupcion. */
        ax_idle_wait();
        return;
    }

    AxThread* next = ax_pick_next();
    if (!next) {
        /* Solo estamos nosotros: no hay a quien ceder. */
        ax_idle_wait();
        return;
    }
    if (next == cur) {
        ax_ready_push(cur);
        return;
    }
    ax_switch_to(next);
}

void ax_thread_block(void)
{
    AxThread* cur = ax_current_thread();
    if (!cur) {
        return;
    }

    cur->state = AX_THREAD_BLOCKED;
    ax_ready_remove(cur);

    AxThread* next = ax_pick_next();
    if (!next) {
        /*
         * No hay nadie mas: deshacemos el bloqueo y esperamos a que el tick
         * nos despierte (sera el caso normal: habra un hilo idle).
         */
        cur->state = AX_THREAD_RUNNING;
        ax_idle_wait();
        return;
    }
    ax_switch_to(next);
}

void ax_thread_wakeup(AxThread* thread)
{
    if (!thread) {
        return;
    }
    if (thread->state == AX_THREAD_BLOCKED) {
        thread->state = AX_THREAD_READY;
        ax_ready_push(thread);
    }
}

void ax_thread_sleep(uint64_t ms)
{
    AxThread* cur = ax_current_thread();
    if (!cur) {
        return;
    }

    uint64_t deadline = g_uptime_ms + ms;
    cur->wake_at_ms = deadline;

    for (;;) {
        ax_thread_block();
        if (g_uptime_ms >= deadline) {
            break;
        }
        /* Nos despertaron antes de tiempo: volvemos a dormir. */
        cur->wake_at_ms = deadline;
    }
    cur->wake_at_ms = 0;
}

AxThread* ax_current_thread(void)
{
    return g_current;
}

AxThread* ax_thread_by_id(uint64_t id)
{
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        if (g_threads[i].state != AX_THREAD_FREE && g_threads[i].id == id) {
            return &g_threads[i];
        }
    }
    return nullptr;
}

uint32_t ax_thread_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        if (g_threads[i].state != AX_THREAD_FREE) {
            n++;
        }
    }
    return n;
}

int ax_thread_fill_info(AxThreadInfo* out, uint32_t max)
{
    uint32_t n = 0;
    if (!out) {
        return 0;
    }
    for (int i = 0; i < AX_MAX_THREADS && n < max; i++) {
        if (g_threads[i].state == AX_THREAD_FREE) {
            continue;
        }
        out[n].pid   = (uint32_t)g_threads[i].id;
        out[n].state = (uint32_t)g_threads[i].state;
        ax_strcopy(out[n].name, g_threads[i].name, AX_NAME_LEN);
        n++;
    }
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Tick del temporizador                                               */
/* ------------------------------------------------------------------ */

/*
 * Despierta los hilos cuyo temporizador ha expirado y cede el CPU al
 * siguiente hilo listo (round-robin).
 */
void ax_sched_tick(void)
{
    g_uptime_ms += AX_TICK_MS;
    g_ticks++;

    /* Despertar a los hilos dormidos. */
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        AxThread* t = &g_threads[i];
        if (t->state == AX_THREAD_BLOCKED && t->wake_at_ms && t->wake_at_ms <= g_uptime_ms) {
            t->wake_at_ms = 0;
            t->state = AX_THREAD_READY;
            ax_ready_push(t);
        }
    }

    /*
     * El arranque llama a ax_sched_start() al final de kmain(), asi que no
     * hace falta arrancar el planificador desde el tick.  (Antes se hacia
     * aqui pasados unos ticks y el shell podia adelantarse a la impresion
     * del resumen de memoria.)
     */

    AxThread* cur = g_current;
    if (!cur) {
        return;
    }

    if (!g_ready_head) {
        /* Nadie mas esperando: el hilo actual sigue. */
        return;
    }

    AxThread* next = ax_ready_pop();
    if (next && next != cur) {
        ax_switch_to(next);
    } else if (next) {
        ax_ready_push(next);
    }
}

/* ------------------------------------------------------------------ */
/* Arranque del planificador                                           */
/* ------------------------------------------------------------------ */

/* Crea el hilo del shell si todavia no existe. */
static void ax_sched_ensure_shell(void)
{
    for (int i = 0; i < AX_MAX_THREADS; i++) {
        if (g_threads[i].state != AX_THREAD_FREE && ax_strlen(g_threads[i].name) == 5 &&
            ax_memeq(g_threads[i].name, "shell", 5) == 0) {
            return;
        }
    }
    ax_shell_init();
}

void ax_sched_start(void)
{
    if (g_started) {
        return;
    }

    ax_sched_ensure_shell();

    g_started = true;

    AxThread* next = ax_pick_next();
    if (!next) {
        /* Sin hilos listos: se sigue en el contexto de arranque. */
        g_started = false;
        return;
    }

    ax_switch_to(next);

    /* ax_switch_to no retorna nunca (el hilo elegido toma el control). */
}

/* ------------------------------------------------------------------ */
/* Tiempo y punto de entrada de los hilos de kernel                    */
/* ------------------------------------------------------------------ */

uint64_t ax_uptime_ms(void)
{
    return g_uptime_ms;
}

/*
 * Funcion de entrada de los hilos de kernel: la invoca el trampolin de
 * Kernel/src/context.S.  Llama a la funcion de entrada del hilo con su
 * argumento y, cuando termine, retira el hilo.
 */
extern "C" void ax_thread_kernel_entry(void)
{
    AxThread* cur = ax_current_thread();
    if (cur && cur->entry) {
        void (*fn)(void*) = (void (*)(void*))(uintptr_t)cur->entry;
        fn(cur->arg);
    }
    ax_thread_exit(0);
}

/*
 * Retirada del hilo de arranque: el trampolin de Kernel/src/context.S salta
 * aqui en lugar de devolver el control a boot.S (que terminaria en
 * `cli; hlt` y dejaria el sistema sin interrupciones).
 */
extern "C" void ax_boot_park(void)
{
    ax_thread_exit(0);
}
