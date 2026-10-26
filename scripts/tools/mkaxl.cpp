/*
 * AxolynOS - Herramienta de host: empaqueta una biblioteca .axl.
 *
 *   mkaxl <entrada.a> <salida.axl> <nombre>
 *
 * Un .axl es la cabecera ax_axl_header seguida del archivo estatico "ar":
 *
 *   [ax_axl_header][!<arch>\n ... miembros ...]
 *
 * La herramienta axl2a extrae el archivo para enlazar.  El build usa el
 * .axl como formato de distribucion de libaxos.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "ax_pkg.h"

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkaxl: no se puede abrir %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); fprintf(stderr, "mkaxl: tamano invalido\n"); exit(1); }
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && fread(buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "mkaxl: error de lectura\n");
        exit(1);
    }
    fclose(f);
    return buf;
}

/* Cuenta los miembros reales de un archivo "ar" (GNU o BSD). */
static uint32_t countMembers(const std::vector<uint8_t>& ar)
{
    if (ar.size() < 8 || std::memcmp(ar.data(), "!<arch>\n", 8) != 0) {
        return 0;
    }
    uint32_t count = 0;
    size_t pos = 8;
    while (pos + 60 <= ar.size()) {
        const uint8_t* h = &ar[pos];
        if (h[58] != '`' || h[59] != '\n') {
            break;
        }
        char sizeBuf[11];
        std::memcpy(sizeBuf, h + 48, 10);
        sizeBuf[10] = '\0';
        long size = std::strtol(sizeBuf, nullptr, 10);
        if (size < 0) break;
        /* El miembro "/" es la tabla de simbolos; no es codigo. */
        if (h[0] != '/') {
            ++count;
        }
        pos += 60 + (size_t)size;
        if (pos & 1) ++pos;              /* alineacion a 2 bytes */
    }
    return count;
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        fprintf(stderr, "uso: mkaxl <entrada.a> <salida.axl> <nombre>\n");
        return 1;
    }
    const char* inPath  = argv[1];
    const char* outPath = argv[2];
    const char* name    = argv[3];

    std::vector<uint8_t> ar = readFile(inPath);
    if (ar.size() < 8 || std::memcmp(ar.data(), "!<arch>\n", 8) != 0) {
        fprintf(stderr, "mkaxl: %s no es un archivo ar\n", inPath);
        return 1;
    }

    ax_axl_header hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic = AX_PKG_MAGIC_AXL;
    hdr.version = AX_PKG_VERSION;
    hdr.kind = AX_PKG_KIND_AR;
    hdr.payload_size = (uint32_t)ar.size();
    hdr.payload_crc32 = ax_pkg_crc32(ar.data(), (uint32_t)ar.size());
    hdr.members = countMembers(ar);
    std::snprintf(hdr.name, sizeof(hdr.name), "%s", name);
    std::snprintf(hdr.arch, sizeof(hdr.arch), "x86_64");

    FILE* out = fopen(outPath, "wb");
    if (!out) { fprintf(stderr, "mkaxl: no se puede crear %s\n", outPath); return 1; }
    if (fwrite(&hdr, 1, sizeof(hdr), out) != sizeof(hdr) ||
        (ar.size() && fwrite(ar.data(), 1, ar.size(), out) != ar.size())) {
        fclose(out);
        fprintf(stderr, "mkaxl: error de escritura\n");
        return 1;
    }
    fclose(out);

    printf("mkaxl: %s -> %s (%zu bytes, %u miembros, crc %08x)\n",
           inPath, outPath, sizeof(hdr) + ar.size(),
           hdr.members, hdr.payload_crc32);
    return 0;
}
