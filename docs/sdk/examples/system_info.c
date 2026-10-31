/* Ejemplo: Informacion del sistema con el SDK */
#include <axos/sdk.h>
#include <axos/types.h>

void print_sysinfo(void) {
    AxSysInfo info;
    if (sdk_sysinfo(&info) == 0) {
        sdk_print("=== Informacion del Sistema ===\n");
        sdk_print("Kernel: %s %s\n", info.kernel_name, info.kernel_version);
        sdk_print("CPU: %u hilos activos\n", info.cpu_count);
        sdk_print("Hilos: %u thread activos\n", info.thread_count);
        sdk_print("RAM total: %u MiB\n",
                  info.total_phys / (1024 * 1024));
        sdk_print("RAM libre: %u MiB\n",
                  info.free_phys / (1024 * 1024));
        sdk_print("===============================\n");
    } else {
        sdk_print("Error: No se pudo obtener informacion del sistema\n");
    }
}

int main(void) {
    sdk_init();
    print_sysinfo();
    /* Mantener el sistema corriendo */
    for (;;) {
        /* Aqui podrian ir tareas periodicas */
    }
}