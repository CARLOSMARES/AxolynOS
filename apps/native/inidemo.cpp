/*
 * inidemo - Muestra valores de un fichero INI.
 *
 * Uso: inidemo <fichero> [seccion] [clave]
 *
 * Ejemplo de uso de la API axos::ini_*.
 */

#include "axos/axos.hpp"

int main(int argc, char** argv)
{
    if (argc < 2) {
        axos::println("uso: inidemo <fichero> [seccion] [clave]");
        return 1;
    }

    long n = axos::ini_parse(argv[1]);
    if (n < 0) {
        axos::print("no se pudo cargar ");
        axos::println(argv[1]);
        return 1;
    }

    axos::print("entradas: ");
    axos::println_uint((unsigned long)n);

    if (argc == 2) {
        for (long i = 0; i < n; ++i) {
            axos::IniEntry e = axos::ini_enum((unsigned int)i);
            if (e.error < 0) {
                break;
            }
            if (e.section[0]) {
                axos::print("[");
                axos::print(e.section);
                axos::print("] ");
            }
            axos::print(e.key);
            axos::print(" = ");
            axos::println(e.value);
        }
        return 0;
    }

    char value[AX_INI_VALUE_LEN];
    if (argc == 3) {
        bool any = false;
        for (long i = 0; i < n; ++i) {
            axos::IniEntry e = axos::ini_enum((unsigned int)i);
            if (e.error < 0) {
                break;
            }
            if (axos::strcmp(e.section, argv[2]) == 0) {
                axos::print(e.key);
                axos::print(" = ");
                axos::println(e.value);
                any = true;
            }
        }
        if (!any) {
            axos::println("(seccion vacia o no encontrada)");
        }
        return 0;
    }

    if (axos::ini_get(argv[2], argv[3], value, sizeof(value)) < 0) {
        axos::println("clave no encontrada");
        return 1;
    }
    axos::println(value);
    return 0;
}
