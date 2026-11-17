/*
 * axcdemo - Muestra la configuracion de una aplicacion (.axc).
 *
 * Uso: axcdemo <aplicacion>
 *
 * Ejemplo de uso de la API axos::axc_find / axos::axc_list / axos::axc_conn.
 */

#include "axos/axos.hpp"

int main(int argc, char** argv)
{
    if (argc < 2) {
        axos::println("uso: axcdemo <aplicacion>");
        return 1;
    }

    long idx = axos::axc_find(argv[1]);
    if (idx < 0) {
        axos::print("axcdemo: no se encuentra ");
        axos::println(argv[1]);
        return 1;
    }

    axos::AppConfig c = axos::axc_list((unsigned int)idx);
    if (c.error < 0) {
        axos::println("axcdemo: error al leer la configuracion");
        return 1;
    }

    axos::print("aplicacion : ");
    axos::println(c.name);
    axos::print("version    : ");
    axos::println(c.version);
    axos::print("publicador : ");
    axos::println(c.publisher);
    if (c.exec[0]) {
        axos::print("ejecutable : ");
        axos::println(c.exec);
    }
    if (c.description[0]) {
        axos::print("descripcion: ");
        axos::println(c.description);
    }

    for (unsigned int i = 0; i < c.conn_count; ++i) {
        axos::AppConnection cn = axos::axc_conn((unsigned int)idx, i);
        if (cn.error < 0) {
            break;
        }
        axos::print("  ");
        axos::print(cn.key);
        axos::print(" -> ");
        axos::println(cn.value);
    }

    return 0;
}
