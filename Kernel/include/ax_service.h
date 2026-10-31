/*
 * AxolynOS - Gestor de servicios del sistema.
 *
 * Cada subsistema que se inicializa durante el arranque se registra aqui.
 * La splash muestra el avance y el shell puede listar, arrancar y parar
 * servicios con `services`, `service start <nombre>` y `start <nombre>`.
 */
#ifndef AX_SERVICE_H
#define AX_SERVICE_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_SERVICE_MAX      24
#define AX_SERVICE_NAME     32
#define AX_SERVICE_DESC     80

#define AX_SERVICE_STOPPED  0u
#define AX_SERVICE_RUNNING  1u
#define AX_SERVICE_FAILED   2u

typedef struct {
    char     name[AX_SERVICE_NAME];
    char     description[AX_SERVICE_DESC];
    uint32_t state;
    bool     auto_start;
} AxServiceInfoK;

void ax_service_init(void);
int  ax_service_register(const char* name, const char* description,
                         bool auto_start, void (*start)(void),
                         void (*stop)(void));
void ax_service_mark_started(const char* name);
int  ax_service_autostart(void);
int  ax_service_start(const char* name);
int  ax_service_stop(const char* name);
int  ax_service_enum(uint32_t index, AxServiceInfoK* out);
int  ax_service_count(void);
const char* ax_service_state_name(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif /* AX_SERVICE_H */
