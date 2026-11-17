/*
 * sysfiles - Lista los archivos de sistema .axsys cargados por el kernel.
 *
 * Ejemplo de uso de la API axos::sysfile_*.
 */

#include "axos/axos.hpp"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    axos::println("archivos de sistema (.axsys):");

    bool any = false;
    for (unsigned int i = 0; i < 16; ++i) {
        axos::SysFileInfo info = axos::sysfile_list(i);
        if (info.error < 0) {
            break;
        }
        any = true;
        axos::print("  [");
        axos::print_uint((unsigned long)info.index);
        axos::print("] ");
        axos::print(info.name);
        axos::print("  ");
        axos::print(info.product);
        axos::print("  ");
        axos::println(info.manufacturer);
        axos::print("      bytes=");
        axos::print_uint(info.content_size);
        axos::print("  firma=");
        axos::print(info.signature_ok ? "valida" : "no");
        axos::print("  meta=");
        axos::println_uint(info.meta_count);
    }

    if (!any) {
        axos::println("  (no hay archivos cargados)");
    }

    return 0;
}
