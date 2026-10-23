/*
 * AxolynOS - Gestión de energía (apagado, reinicio, suspensión)
 */
#ifndef AX_POWER_H
#define AX_POWER_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ax_power_init(void);
void ax_power_halt(void);
void ax_power_reboot(void);
void ax_power_shutdown(void);
void ax_power_suspend(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_POWER_H */
