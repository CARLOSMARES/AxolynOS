/*
 * AxolynOS - Aplicacion "sysinfo".
 *
 * Informacion del sistema: kernel, hilos, modulos, uptime y memoria.
 *
 * Syscalls usadas:
 *      AX_SYS_SYSINFO (23)  -> datos generales del kernel
 *      AX_SYS_THREADS (26)  -> lista de hilos
 *      AX_SYS_MODLIST(14)  -> lista de modulos
 *      AX_SYS_TIME    (8)   -> uptime
 *
 * Devuelve 0 si todo va bien y 1 si el kernel no responde.
 */
#include "axos/axos.hpp"

namespace {

void copy_field(char* dst, const char* src, unsigned long max)
{
    unsigned long i = 0;

    for (; i < max && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Nombre corto del estado de un hilo (ver AX_SYS_THREADS). */
const char* thread_state(unsigned int state)
{
    if (state == 0) { return "libre"; }
    if (state == 1) { return "listo"; }
    if (state == 2) { return "ejecutando"; }
    if (state == 3) { return "bloqueado"; }
    if (state == 4) { return "zombi"; }
    return "desconocido";
}

} /* namespace */

int main(int argc, char** argv)
{
    AxSysInfo    info;
    AxThreadInfo threads[64];
    AxModInfo    modules[16];
    char         name[32];
    char         version[24];
    long         count;

    (void)argc;
    (void)argv;

    axos::println("=== Informacion del sistema ===");
    axos::println("");

    if (axos::sys_info(&info) >= 0) {
        copy_field(name,    info.kernel_name,    sizeof(info.kernel_name));
        copy_field(version, info.kernel_version, sizeof(info.kernel_version));

        axos::print("Kernel        : "); axos::println(name);
        axos::print("Version       : "); axos::println(version);
        axos::print("CPU           : "); axos::println_uint(info.cpu_count);
        axos::print("Hilos         : "); axos::println_uint(info.thread_count);
        axos::print("Uptime        : ");
        axos::print_uint((unsigned long)info.uptime_ms);
        axos::println(" ms");
        axos::print("Hora Unix     : ");
        axos::print_uint((unsigned long)info.unix_ms);
        axos::println(" ms");
        axos::print("RAM total     : ");
        axos::print_uint((unsigned long)(info.total_phys / (1024UL * 1024UL)));
        axos::println(" MiB");
        axos::print("RAM libre     : ");
        axos::print_uint((unsigned long)(info.free_phys / (1024UL * 1024UL)));
        axos::println(" MiB");
    } else {
        axos::println("No se pudo leer AX_SYS_SYSINFO.");
        return 1;
    }

    axos::println("");
    axos::println("--- Hilos ---");
    count = axos::threads(threads, 64UL);
    if (count <= 0) {
        axos::println("  (no se pudo leer AX_SYS_THREADS)");
    } else {
        for (long i = 0; i < count && i < 64; ++i) {
            char tname[32];
            copy_field(tname, threads[i].name, sizeof(threads[i].name));
            axos::print("  pid=");
            axos::print_uint(threads[i].pid);
            axos::print("  ");
            axos::print(tname);
            axos::print("  [");
            axos::print(thread_state(threads[i].state));
            axos::println("]");
        }
    }

    axos::println("");
    axos::println("--- Modulos ---");
    count = axos::modules(modules, 16UL);
    if (count <= 0) {
        axos::println("  (no se pudo leer AX_SYS_MODLIST o no hay modulos)");
    } else {
        for (long i = 0; i < count && i < 16; ++i) {
            char mname[32];
            copy_field(mname, modules[i].name, sizeof(modules[i].name));
            axos::print("  [");
            axos::print_int(i);
            axos::print("] ");
            axos::print(mname);
            axos::print("  tam=");
            axos::print_uint((unsigned long)modules[i].size);
            axos::print("  vaddr=0x");
            axos::print_hex(modules[i].vaddr);
            axos::println("");
        }
    }

    axos::println("");
    axos::println("--- Reloj del kernel ---");
    axos::print("Uptime actual : ");
    axos::print_uint((unsigned long)axos::uptime_ms());
    axos::println(" ms");

    return 0;
}
