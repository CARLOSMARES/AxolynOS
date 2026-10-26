/*
 * AxolynOS - Herramienta de host: extrae una biblioteca .axl.
 *
 *   axl2a <entrada.axl> <salida.a>
 *
 * Comprueba la cabecera ax_axl_header (firma, version, tipo, tamano y CRC)
 * y escribe el archivo estatico "ar" que contiene.  Es el paso inverso de
 * mkaxl y permite que el enlazador use libaxos.axl directamente.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "ax_pkg.h"

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "axl2a: no se puede abrir %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); fprintf(stderr, "axl2a: tamano invalido\n"); exit(1); }
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && fread(buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "axl2a: error de lectura\n");
        exit(1);
    }
    fclose(f);
    return buf;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "uso: axl2a <entrada.axl> <salida.a>\n");
        return 1;
    }
    const char* inPath  = argv[1];
    const char* outPath = argv[2];

    std::vector<uint8_t> pkg = readFile(inPath);

    if (pkg.size() < sizeof(ax_axl_header)) {
        fprintf(stderr, "axl2a: %s es demasiado pequeno\n", inPath);
        return 1;
    }
    ax_axl_header hdr;
    std::memcpy(&hdr, pkg.data(), sizeof(hdr));

    if (hdr.magic != AX_PKG_MAGIC_AXL) {
        fprintf(stderr, "axl2a: firma .axl incorrecta (0x%08x)\n", hdr.magic);
        return 1;
    }
    if (hdr.version != AX_PKG_VERSION) {
        fprintf(stderr, "axl2a: version no soportada (%u)\n", (unsigned)hdr.version);
        return 1;
    }
    if (hdr.kind != AX_PKG_KIND_AR) {
        fprintf(stderr, "axl2a: tipo de payload no soportado (%u)\n", (unsigned)hdr.kind);
        return 1;
    }
    if ((uint64_t)hdr.payload_size > pkg.size() - sizeof(hdr)) {
        fprintf(stderr, "axl2a: payload truncado (%u bytes)\n", hdr.payload_size);
        return 1;
    }

    const uint8_t* payload = pkg.data() + sizeof(hdr);
    uint32_t crc = ax_pkg_crc32(payload, hdr.payload_size);
    if (crc != hdr.payload_crc32) {
        fprintf(stderr, "axl2a: CRC incorrecto (0x%08x != 0x%08x)\n",
                crc, hdr.payload_crc32);
        return 1;
    }
    if (hdr.payload_size < 8 ||
        std::memcmp(payload, "!<arch>\n", 8) != 0) {
        fprintf(stderr, "axl2a: el payload no es un archivo ar\n");
        return 1;
    }

    FILE* out = fopen(outPath, "wb");
    if (!out) { fprintf(stderr, "axl2a: no se puede crear %s\n", outPath); return 1; }
    if (hdr.payload_size &&
        fwrite(payload, 1, hdr.payload_size, out) != hdr.payload_size) {
        fclose(out);
        fprintf(stderr, "axl2a: error de escritura\n");
        return 1;
    }
    fclose(out);

    printf("axl2a: %s -> %s (%u bytes, %u miembros, crc %08x)\n",
           inPath, outPath, hdr.payload_size, hdr.members, hdr.payload_crc32);
    return 0;
}
