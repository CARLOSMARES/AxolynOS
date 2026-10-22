/*
 * AxolynOS - Aplicacion "cat".
 *
 * Imprime por pantalla el contenido de uno o varios ficheros.
 *
 * Uso:  cat <fichero> [<fichero> ...]
 *
 * Syscalls usadas:
 *      AX_SYS_OPEN  (3)  -> abre el fichero
 *      AX_SYS_READ  (2)  -> lee su contenido
 *      AX_SYS_CLOSE (4)  -> cierra el descriptor
 *
 * Devuelve 0 si todos los ficheros se pudieron leer y 1 si algun fallo.
 */
#include "axos/axos.hpp"

namespace {

/* Vuelca un fichero por pantalla. Devuelve true si se leyo algo. */
bool dump_file(const char* path)
{
    long fd = axos::open(path, AX_O_RDONLY);
    char buffer[512];

    if (fd < 0) {
        axos::print("cat: no se puede abrir '");
        axos::print(path);
        axos::println("'");
        return false;
    }

    for (;;) {
        long n = axos::read_file((int)fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        axos::write(1, buffer, (unsigned long)n);
        if ((unsigned long)n < sizeof(buffer)) {
            break;
        }
    }

    axos::close((int)fd);
    return true;
}

} /* namespace */

int main(int argc, char** argv)
{
    int  failures = 0;
    bool ok;

    if (argc < 2) {
        axos::println("uso: cat <fichero> [<fichero> ...]");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        ok = dump_file(argv[i]);
        if (!ok) {
            ++failures;
        }
    }

    return (failures == 0) ? 0 : 1;
}
