/* Ejemplo basico: Hola Mundo con el SDK de AxolynOS */
#include <axos/sdk.h>
#include <axos/console.h>

int main(void) {
    /* Inicializar el SDK */
    sdk_init();

    /* Mostrar un mensaje en la consola serie */
    sdk_putstr("Hola, este es mi primera aplicacion con el SDK AxolynOS\n");

    /* Mostrar informacion del sistema */
    AxSysInfo info;
    if (sdk_sysinfo(&info) == 0) {
        sdk_print("Kernel: %s %s\n", info.kernel_name, info.kernel_version);
        sdk_print("CPU: %u hilos\n", info.cpu_count);
        sdk_print("RAM libre: %u MiB\n",
                  info.free_phys / (1024 * 1024));
    }

    /* Aplicacion: esperar un poco y salir */
    sdk_halt();  /* detiene el CPU (o sdk_poweroff() para apagar) */
    return 0;
}