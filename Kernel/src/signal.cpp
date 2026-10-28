/*
 * AxolynOS - Senales de proceso.
 *
 * Implementacion minima pero funcional de SIGINT/SIGTERM/SIGQUIT y del
 * resto de senales POSIX habituales.  No hay cambio de contexto a un
 * manejador de user mode: las senales con accion por defecto terminan el
 * proceso y las que tienen manejador se entregan como AX_EINTR (la
 * aplicacion las consulta con AX_SYS_SIGPENDING).
 */
#include "ax_signal.h"
#include "ax_sched.h"
#include "ax_kernel.h"
#include "ax_console.h"
#include "axos/abi.h"

static uint64_t g_foreground_pid = 0;

static uint32_t sig_bit(int sig)
{
    return (uint32_t)1u << ((uint32_t)sig - 1u);
}

static bool sig_valid(int sig)
{
    return sig > 0 && sig <= AX_SIG_MAX;
}

static bool sig_default_terminates(int sig)
{
    switch (sig) {
        case AX_SIGCHLD:
        case AX_SIGCONT:
        case AX_SIGSTOP:
            return false;
        default:
            return true;
    }
}

void ax_signal_init(void)
{
    g_foreground_pid = 0;
}

int ax_signal_send(uint64_t pid, int sig)
{
    if (!sig_valid(sig)) {
        return AX_EINVAL;
    }
    if (pid == 0) {
        pid = ax_proc_current_pid();
    }

    AxThread* t = ax_thread_by_id(pid);
    if (!t || t->state == AX_THREAD_FREE) {
        return AX_ENOENT;
    }

    /* SIGKILL no se puede ignorar ni capturar. */
    if (sig == AX_SIGKILL) {
        t->sig_pending = 0;
        t->sig_taken = 0;
        return ax_proc_kill(pid);
    }

    uint64_t handler = t->sig_handlers[sig];
    if (handler == AX_SIG_IGN ||
        (!sig_default_terminates(sig) && handler == AX_SIG_DFL)) {
        return 0;
    }

    t->sig_pending |= sig_bit(sig);
    if (t->state == AX_THREAD_BLOCKED) {
        ax_thread_wakeup(t);
    }
    return 0;
}

int ax_signal_set(int sig, uint64_t handler)
{
    if (!sig_valid(sig) || sig == AX_SIGKILL || sig == AX_SIGSTOP) {
        return AX_EINVAL;
    }
    AxThread* t = ax_current_thread();
    if (!t) {
        return AX_EINVAL;
    }
    t->sig_handlers[sig] = handler;
    return 0;
}

uint32_t ax_signal_pending_mask(void)
{
    AxThread* t = ax_current_thread();
    return t ? t->sig_pending : 0;
}

uint32_t ax_signal_taken_mask(void)
{
    AxThread* t = ax_current_thread();
    return t ? t->sig_taken : 0;
}

bool ax_signal_is_pending(int sig)
{
    if (!sig_valid(sig)) {
        return false;
    }
    return (ax_signal_pending_mask() & sig_bit(sig)) != 0;
}

int ax_signal_take(int sig)
{
    if (!sig_valid(sig)) {
        return 0;
    }
    AxThread* t = ax_current_thread();
    if (!t) {
        return 0;
    }
    uint32_t bit = sig_bit(sig);
    int hit = (t->sig_taken & bit) ? 1 : 0;
    t->sig_taken &= ~bit;
    return hit;
}

void ax_signal_check(void)
{
    AxThread* t = ax_current_thread();
    if (!t || t->sig_pending == 0) {
        return;
    }

    for (int sig = 1; sig <= AX_SIG_MAX; ++sig) {
        uint32_t bit = sig_bit(sig);
        if ((t->sig_pending & bit) == 0) {
            continue;
        }

        uint64_t handler = t->sig_handlers[sig];
        if (handler == AX_SIG_IGN || !sig_default_terminates(sig)) {
            t->sig_pending &= ~bit;
            continue;
        }
        if (handler == AX_SIG_DFL) {
            t->sig_pending &= ~bit;
            ax_printf("[proc] senal %d: terminando pid %u\n",
                      sig, (unsigned)t->id);
            ax_thread_exit(128 + sig);
            /* ax_thread_exit() no retorna. */
        }

        /* Hay manejador propio: se entrega como interrupcion de la
           lectura (AX_EINTR).  La aplicacion consulta la senal. */
        t->sig_pending &= ~bit;
        t->sig_taken |= bit;
    }
}

void ax_signal_set_foreground(uint64_t pid)
{
    g_foreground_pid = pid;
}

uint64_t ax_signal_foreground(void)
{
    return g_foreground_pid;
}

void ax_signal_foreground_interrupt(int sig)
{
    if (g_foreground_pid) {
        (void)ax_signal_send(g_foreground_pid, sig);
    }
}

int ax_signal_from_char(char c)
{
    switch ((unsigned char)c) {
        case 0x03: return AX_SIGINT;    /* Ctrl+C */
        case 0x1C: return AX_SIGQUIT;   /* Ctrl+\ */
        case 0x1A: return AX_SIGSTOP;   /* Ctrl+Z */
        default:   return 0;
    }
}
