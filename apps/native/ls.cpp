/*
 * AxolynOS - Aplicacion "ls".
 *
 * Lista el contenido del sistema de ficheros (el initrd se expone como
 * un arbol de directorios) y, si el kernel no expone ningun directorio,
 * lista los modulos cargados.
 *
 * Syscalls usadas:
 *      AX_SYS_OPEN   (3)  -> abre un directorio
 *      AX_SYS_READ   (2)  -> lee su contenido
 *      AX_SYS_CLOSE  (4)  -> cierra el descriptor
 *      AX_SYS_MODLIST(14) -> lista los modulos del kernel
 *
 * Devuelve 0 si se pudo listar algo y 1 si no.
 */
#include "axos/axos.hpp"

namespace {

/* Rutas que se intentan listar, en este orden. */
const char* g_paths[] = { "/", "/initrd", "/bin", "/etc", "/home" };

/*
 * Abre una ruta y vuelca su contenido por pantalla.
 * Devuelve true si el kernel devolvio algun dato.
 */
bool list_path(const char* path)
{
    long fd = axos::open(path, AX_O_RDONLY);
    char buffer[1024];
    bool found = false;

    if (fd < 0) {
        return false;
    }

    axos::print("--- ");
    axos::print(path);
    axos::println(" ---");

    for (;;) {
        long n = axos::read_file((int)fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        axos::write(1, buffer, (unsigned long)n);
        found = true;
        if ((unsigned long)n < sizeof(buffer)) {
            break;
        }
    }

    axos::close((int)fd);

    if (found) {
        axos::println("");
    }
    return found;
}

/* Lista los modulos que el kernel tiene cargados. */
bool list_modules()
{
    AxModInfo modules[16];
    long       count = axos::modules(modules, 16UL);

    if (count <= 0) {
        return false;
    }

    axos::println("--- Modulos del kernel ---");
    for (long i = 0; i < count && i < 16; ++i) {
        axos::print("  [");
        axos::print_int(i);
        axos::print("] ");
        axos::print(modules[i].name);
        axos::print("  vaddr=0x");
        axos::print_hex(modules[i].vaddr);
        axos::print("  tam=");
        axos::print_uint((unsigned long)modules[i].size);
        axos::print(" bytes  estado=");
        axos::print_uint(modules[i].state);
        axos::println("");
    }
    axos::println("");
    return true;
}

} /* namespace */

int main(int argc, char** argv)
{
    bool listed = false;

    /* Si se pasan rutas, se listan esas. */
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (!list_path(argv[i])) {
                axos::print("ls: no se pudo leer '");
                axos::print(argv[i]);
                axos::println("'");
            } else {
                listed = true;
            }
        }
        return listed ? 0 : 1;
    }

    axos::println("=== Contenido del sistema de ficheros ===");
    axos::println("");

    for (unsigned long i = 0; i < sizeof(g_paths) / sizeof(g_paths[0]); ++i) {
        if (list_path(g_paths[i])) {
            listed = true;
        }
    }

    if (!listed) {
        axos::println("El kernel no expone directorios; se listan los modulos.");
        axos::println("");
        if (!list_modules()) {
            axos::println("No hay modulos cargados.");
            return 1;
        }
    }

    return 0;
}
