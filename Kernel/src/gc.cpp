/*
 * AxolynOS - Recolector de basura (procesos e hilos).
 *
 * En AxolynOS un proceso es un hilo de user mode.  Cuando una aplicacion
 * termina su hilo queda en estado zombi hasta que alguien recoge el codigo
 * de salida.  Si la shell no lo hace (por ejemplo, una app que muere sola o
 * que se lanza desde otra app), sus pilas de kernel y su ranura de user mode
 * quedan reservadas.  Este modulo las libera.
 *
 * El GC:
 *   - libera las ranuras de user mode de procesos muertos,
 *   - cierra los descriptores de fichero que abrio el proceso,
 *   - reapea los hilos zombis y libera sus pilas (1 MiB + 16 KiB cada una),
 *   - lleva contadores para el comando 'gc'.
 */
#include "ax_gc.h"
#include "ax_kernel.h"
#include "ax_sched.h"
#include "ax_memory.h"
#include "ax_console.h"

#define AX_GC_ENTRY_STACK_SIZE 0x4000ull

static AxGcStats g_stats;

void ax_gc_init(void)
{
    g_stats.runs = 0;
    g_stats.total_threads = 0;
    g_stats.total_slots = 0;
    g_stats.total_fds = 0;
    g_stats.total_stack_bytes = 0;
    g_stats.last_threads = 0;
    g_stats.last_slots = 0;
    g_stats.last_fds = 0;
    g_stats.last_stack_bytes = 0;
    g_stats.pending = 0;
}

int ax_gc_collect(void)
{
    AxThreadInfo info[AX_MAX_THREADS];
    uint64_t zombies = 0;

    int n = ax_thread_fill_info(info, AX_MAX_THREADS);
    for (int i = 0; i < n; ++i) {
        if (info[i].state == AX_THREAD_ZOMBIE) {
            zombies++;
        }
    }

    uint64_t slots = 0;
    uint64_t fds = 0;
    ax_proc_gc(&slots, &fds);

    int reaped = ax_thread_reap_zombies();
    if ((uint64_t)reaped > zombies) {
        zombies = (uint64_t)reaped;
    }

    uint64_t stack_bytes = zombies *
        (AX_KERNEL_STACK_SIZE + AX_GC_ENTRY_STACK_SIZE);

    g_stats.runs++;
    g_stats.last_threads = zombies;
    g_stats.last_slots = slots;
    g_stats.last_fds = fds;
    g_stats.last_stack_bytes = stack_bytes;
    g_stats.total_threads += zombies;
    g_stats.total_slots += slots;
    g_stats.total_fds += fds;
    g_stats.total_stack_bytes += stack_bytes;
    g_stats.pending = ax_thread_zombie_count();

    return (int)(zombies + slots + fds);
}

void ax_gc_stats(AxGcStats* out)
{
    if (!out) {
        return;
    }
    *out = g_stats;
    out->pending = (uint64_t)ax_thread_zombie_count();
}

const char* ax_gc_state_name(void)
{
    return g_stats.runs ? "activo" : "inactivo";
}
