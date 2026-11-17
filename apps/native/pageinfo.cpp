/*
 * pageinfo - Muestra el estado del sistema de paginacion por demanda.
 *
 * Ejemplo de uso de la API axos::paging_info / axos::swap_*.
 */

#include "axos/axos.hpp"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    axos::PagingInfo p = axos::paging_info();
    if (p.error < 0) {
        axos::println("no se pudo obtener la informacion de paginacion");
        return 1;
    }

    axos::println("paginacion de user mode:");
    axos::print("  paginas totales : ");
    axos::println_uint(p.total_pages);
    axos::print("  mapeadas        : ");
    axos::println_uint(p.mapped_pages);
    axos::print("  libres (cero)   : ");
    axos::println_uint(p.free_pages);
    axos::print("  evacuadas       : ");
    axos::println_uint(p.swapped_pages);
    axos::print("  fallos de pag.  : ");
    axos::println_uint(p.page_faults);
    axos::print("  swap in/out     : ");
    axos::print_uint(p.swap_ins);
    axos::print(" / ");
    axos::println_uint(p.swap_outs);
    axos::print("  intercambio     : ");
    axos::print(p.swap_enabled ? "activo" : "inactivo");
    axos::print(" (");
    axos::print_uint(p.swap_used);
    axos::print("/");
    axos::print_uint(p.swap_slots);
    axos::println(" ranuras)");

    return 0;
}
