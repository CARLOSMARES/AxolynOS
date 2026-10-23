/*
 * AxolynOS SDK - API para programar aplicaciones
 */
#ifndef AXOS_SDK_H
#define AXOS_SDK_H

#include "axos/abi.h"
#include "axos/syscall.h"
#include "axos/console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Entrada de aplicación */
int main(int argc, char** argv);

/* Utilidades del SDK */
void sdk_init(void);
int  sdk_version(void);

/* Gestión de energía */
int  sdk_poweroff(void);
int  sdk_reboot(void);
int  sdk_suspend(void);
void sdk_halt(void);

/* Acceso a disco */
int sdk_disk_read(ax_u64 lba, void* buf, ax_u32 count);
int sdk_disk_write(ax_u64 lba, const void* buf, ax_u32 count);

/* Información del sistema */
int sdk_sysinfo(AxSysInfo* info);
int sdk_meminfo(AxMemInfo* info);

#ifdef __cplusplus
}
#endif

#endif /* AXOS_SDK_H */
