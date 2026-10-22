/*
 * AxolynOS - Aplicacion "hello".
 *
 * Saluda, muestra la version del kernel y la hora actual.
 *
 * Syscalls usadas:
 *      AX_SYS_SYSINFO (23) -> version, nombre, CPU, hilos y memoria
 *      AX_SYS_TIME    (8)  -> uptime en milisegundos
 *      AX_SYS_GETPID  (9)  -> pid de este proceso
 */
#include "axos/axos.hpp"

namespace {

/* Copia un campo de texto del kernel y lo termina en '\0'. */
void copy_field(char* dst, const char* src, unsigned long max)
{
    unsigned long i = 0;

    for (; i < max && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/*
 * Convierte milisegundos Unix en una fecha legible (UTC).
 * Usa el algoritmo de Hinnant para convertir dias en ano/mes/dia.
 */
void print_unix_date(unsigned long unix_ms)
{
    unsigned long total_seconds = unix_ms / 1000UL;
    unsigned long days          = total_seconds / 86400UL;
    unsigned long rest          = total_seconds % 86400UL;
    unsigned long hour          = rest / 3600UL;
    unsigned long minute        = (rest % 3600UL) / 60UL;
    unsigned long second        = rest % 60UL;

    unsigned long z     = days + 719468UL;
    unsigned long era   = z / 146097UL;
    unsigned long doe   = z - era * 146097UL;
    unsigned long yoe   = (doe - doe / 1460UL + doe / 36524UL - doe / 146096UL) / 365UL;
    unsigned long year  = yoe + era * 400UL;
    unsigned long doy   = doe - (365UL * yoe + yoe / 4UL - yoe / 100UL);
    unsigned long mp    = (5UL * doy + 2UL) / 153UL;
    unsigned long day   = doy - (153UL * mp + 2UL) / 5UL + 1UL;
    unsigned long month = (mp < 10UL) ? (mp + 3UL) : (mp - 9UL);

    if (month <= 2UL) {
        year += 1UL;
    }

    axos::print_uint(year);
    axos::print("-");
    if (month < 10UL) { axos::print("0"); }
    axos::print_uint(month);
    axos::print("-");
    if (day < 10UL) { axos::print("0"); }
    axos::print_uint(day);
    axos::print(" ");
    if (hour < 10UL) { axos::print("0"); }
    axos::print_uint(hour);
    axos::print(":");
    if (minute < 10UL) { axos::print("0"); }
    axos::print_uint(minute);
    axos::print(":");
    if (second < 10UL) { axos::print("0"); }
    axos::print_uint(second);
}

} /* namespace */

int main(int argc, char** argv)
{
    AxSysInfo info;
    char       name[32];
    char       version[24];
    bool       have_info = false;

    (void)argc;
    (void)argv;

    axos::println("========================================");
    axos::println("        Hola desde AxolynOS!");
    axos::println("========================================");

    if (axos::sys_info(&info) >= 0) {
        copy_field(name,    info.kernel_name,    sizeof(info.kernel_name));
        copy_field(version, info.kernel_version, sizeof(info.kernel_version));
        have_info = true;
    }

    if (have_info) {
        axos::print("Kernel    : "); axos::println(name);
        axos::print("Version   : "); axos::println(version);
        axos::print("CPU       : "); axos::println_uint(info.cpu_count);
        axos::print("Hilos     : "); axos::println_uint(info.thread_count);
        axos::print("Memoria   : ");
        axos::print_uint(info.total_phys / (1024UL * 1024UL));
        axos::print(" MiB totales, ");
        axos::print_uint(info.free_phys / (1024UL * 1024UL));
        axos::println(" MiB libres");
    } else {
        axos::println("Kernel    : (no se pudo leer AX_SYS_SYSINFO)");
    }

    axos::print("PID       : ");
    axos::println_int(axos::pid());

    axos::print("Uptime    : ");
    axos::print_uint((unsigned long)axos::uptime_ms());
    axos::println(" ms");

    if (have_info) {
        axos::print("Hora      : ");
        print_unix_date((unsigned long)info.unix_ms);
        axos::println(" UTC");
    }

    axos::println("");
    axos::println("Listo. Use 'ls', 'cat', 'meminfo' o 'sysinfo' para mas informacion.");

    return 0;
}
