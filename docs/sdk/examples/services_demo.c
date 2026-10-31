/* Ejemplo: Servicios del sistema */
#include <axos/sdk.h>
#include <axos/console.h>

/* Funcion auxiliar para listar servicios */
void list_services(void) {
    int count = sdk_service_list();
    sdk_print("Servicios registrados (%d):\n", count);
    for (int i = 0; i < count; i++) {
        AxServiceInfo info;
        if (sdk_service_get_info(i, &info) == 0) {
            sdk_print("  [%d] %s - %s\n", i, info.name, info.description);
        }
    }
}

/* Funcion para arrancar/parar un servicio */
void toggle_service(const char* name) {
    sdk_print("Arrancando servicio: %s...\n", name);
    if (sdk_service_start(name) == 0) {
        sdk_print("  -> Servicio arrancado exitosamente\n");
    } else {
        sdk_print("  -> No se pudo arrancar el servicio\n");
    }
    
    /* Pequena pausa */
    sdk_sleep(100);  /* si existe, o usar sdk_init + tiempo */
    
    sdk_print("Deteniendo servicio: %s...\n", name);
    if (sdk_service_stop(name) == 0) {
        sdk_print("  -> Servicio detenido exitosamente\n");
    } else {
        sdk_print("  -> No se pudo detener el servicio\n");
    }
}

int main(void) {
    sdk_init();
    
    /* Listar todos los servicios al inicio */
    sdk_print("=== Servicios del Sistema ===\n");
    list_services();
    sdk_print("\n");
    
    /* Ejemplos de control de servicios */
    toggle_service("power");
    sdk_print("\n");
    toggle_service("users");
    sdk_print("\n");
    toggle_service("network");
    
    sdk_halt();
    return 0;
}