/*
 * AxolynOS - Planificador de hilos y contexts.
 */
#ifndef AX_SCHED_H
#define AX_SCHED_H

#include "ax_types.h"

typedef struct AxThread AxThread;

typedef enum {
    AX_THREAD_FREE     = 0,
    AX_THREAD_READY    = 1,
    AX_THREAD_RUNNING  = 2,
    AX_THREAD_BLOCKED  = 3,
    AX_THREAD_ZOMBIE   = 4
} ax_thread_state;

struct AxThread {
    uint64_t     id;
    char         name[AX_THREAD_NAME_LEN];
    ax_thread_state state;
    int          exit_code;
    int          priority;
    uint64_t     kernel_stack;      /* base de la pila del kernel */
    uint64_t     kernel_stack_top;
    uint64_t     user_stack;        /* pila de user mode (0 si es kernel thread) */
    uint64_t     user_stack_top;
    uint64_t     ctx_rsp;           /* rsp guardado al cambiar de contexto */
    uint64_t     ctx_rip;
    uint64_t     ctx_rflags;
    uint64_t     ctx_cs;
    uint64_t     ctx_ss;
    void*        entry;             /* punto de entrada (kernel thread) */
    void*        arg;
    uint64_t     wake_at_ms;        /* 0 = no duerme */
    AxThread*    next;
    AxThread*    prev;
    int          is_user;
    uint32_t      vm_id;            /* id de VM si el hilo ejecuta bytecode */
};

void      ax_sched_init(void);
AxThread* ax_thread_create_kernel(const char* name, void (*entry)(void*), void* arg);
AxThread* ax_thread_create_user(const char* name, uint64_t entry, uint64_t user_stack_top);
void      ax_thread_exit(int code);
void      ax_thread_reap(AxThread* thread);
void      ax_thread_yield(void);
void      ax_thread_block(void);
void      ax_thread_wakeup(AxThread* thread);
void      ax_thread_sleep(uint64_t ms);
AxThread* ax_current_thread(void);
AxThread* ax_thread_by_id(uint64_t id);
uint32_t  ax_thread_count(void);
int       ax_thread_fill_info(AxThreadInfo* out, uint32_t max);
void      ax_sched_tick(void);
void      ax_sched_start(void);
uint64_t  ax_uptime_ms(void);

/* Cambio de contexto (Kernel/src/context.S)
 *
 * Se declara con enlace C porque la implementacion vive en ensamblador y el
 * simbolo exportado es "ax_context_switch" sin mangling. */
#ifdef __cplusplus
extern "C" {
#endif
void ax_context_switch(uint64_t* save_rsp, uint64_t new_rsp);
#ifdef __cplusplus
}
#endif

#endif /* AX_SCHED_H */
