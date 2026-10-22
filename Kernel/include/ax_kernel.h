/*
 * AxolynOS - Nucleo del kernel: orden de arranque y servicios globales.
 */
#ifndef AX_KERNEL_H
#define AX_KERNEL_H

#include "ax_types.h"

typedef enum ax_boot_stage_e {
    AX_BOOT_STAGE_EARLY = 0,
    AX_BOOT_STAGE_MEMORY,
    AX_BOOT_STAGE_CPU,
    AX_BOOT_STAGE_SCHED,
    AX_BOOT_STAGE_FS,
    AX_BOOT_STAGE_MODULES,
    AX_BOOT_STAGE_USER,
    AX_BOOT_STAGE_READY
} ax_boot_stage_t;

/* Puntos de entrada (Kernel/src/kernel.cpp) */
void ax_kernel_init(AxBootInfo* info);
void ax_kernel_early_init(AxBootInfo* info);

/* Estado global */
uint64_t ax_boot_time_ms(void);
ax_boot_stage_t ax_boot_stage(void);
AxBootInfo* ax_boot_info(void);

/* Apagado / reinicio */
void ax_halt(void);
void ax_reboot(void);

/* Tiempo */
uint64_t ax_timer_ticks(void);
uint64_t ax_timer_ms(void);

/* Gestion de procesos en user mode (Kernel/src/proc.cpp) */
int  ax_proc_spawn(const char* path, const char* const* argv, int argc);
int  ax_proc_wait(uint64_t pid);
int  ax_proc_exit_code(uint64_t pid);
int  ax_proc_kill(uint64_t pid);
uint64_t ax_proc_current_pid(void);
int  ax_proc_fill_info(AxThreadInfo* out, uint32_t max);

/* Puertos de fichero abiertos por el proceso actual */
int  ax_proc_open(const char* path, uint64_t flags);
int  ax_proc_read(int fd, void* buf, uint64_t len);
int  ax_proc_write(int fd, const void* buf, uint64_t len);
int  ax_proc_close(int fd);
int  ax_proc_lseek(int fd, int64_t offset, int whence);

/* Lee una linea de la entrada estandar (consola). */
int  ax_proc_gets(char* buf, uint64_t max);

/* Shell del kernel (Kernel/src/shell.cpp) */
void ax_shell_init(void);
void ax_shell_thread(void* arg);

/* Integracion de la maquina virtual de bytecode (Kernel/src/vmhost.cpp) */
int  ax_vm_run(const char* path, const char* const* argv, int argc);
int  ax_vm_stop(uint64_t pid);
int  ax_vm_fill_info(AxVmInfo* out, uint32_t max);

#endif /* AX_KERNEL_H */
