/*
 * AxolynOS - Aplicacion "meminfo".
 *
 * Muestra la informacion de memoria del kernel:
 *      AX_SYS_MEMINFO  (19) -> gestor de memoria fisico (PMM)
 *      AX_SYS_HEAPINFO (20) -> heap del kernel
 *
 * Devuelve 0 si todo va bien y 1 si el kernel no responde.
 */
#include "axos/axos.hpp"

namespace {

/* MMiB con una coma decimal (evita desbordes al multiplicar). */
void print_mib(unsigned long bytes)
{
    axos::print_uint(bytes / (1024UL * 1024UL));
    axos::print(" MiB (");
    axos::print_uint(bytes);
    axos::print(" bytes)");
}

} /* namespace */

int main(int argc, char** argv)
{
    AxMemInfo  mem;
    AxHeapInfo heap;
    bool       have_mem  = false;
    bool       have_heap = false;

    (void)argc;
    (void)argv;

    axos::println("=== Informacion de memoria del kernel ===");
    axos::println("");

    if (axos::mem_info(&mem) >= 0) {
        have_mem = true;

        axos::println("-- Gestor de memoria fisico --");
        axos::print("Memoria total      : ");    print_mib(mem.total_phys);      axos::println("");
        axos::print("Memoria libre      : ");    print_mib(mem.free_phys);       axos::println("");
        axos::print("Paginas totales    : ");    axos::println_uint(mem.total_pages);
        axos::print("Paginas libres     : ");    axos::println_uint(mem.free_pages);
        axos::print("Paginas reservadas : ");    axos::println_uint(mem.reserved_pages);
        axos::print("Base virtual kernel: 0x");  axos::println_hex(mem.kernel_virt);
        axos::print("Heap usado         : ");    print_mib(mem.heap_used);      axos::println("");
        axos::print("Heap total         : ");    print_mib(mem.heap_total);     axos::println("");
        axos::print("Mapeado bajo       : 0x");  axos::println_hex(mem.low_mapped);
        axos::println("");
    } else {
        axos::println("No se pudo leer AX_SYS_MEMINFO.");
    }

    if (axos::heap_info(&heap) >= 0) {
        have_heap = true;

        axos::println("-- Heap del kernel --");
        axos::print("Bytes usados       : "); print_mib(heap.used);  axos::println("");
        axos::print("Bytes totales      : "); print_mib(heap.total); axos::println("");
        axos::print("Bloques activos    : "); axos::println_uint(heap.blocks);
        axos::print("Bloques libres     : "); axos::println_uint(heap.free_blocks);
        axos::println("");
    } else {
        axos::println("No se pudo leer AX_SYS_HEAPINFO.");
    }

    if (!have_mem && !have_heap) {
        return 1;
    }
    return 0;
}
