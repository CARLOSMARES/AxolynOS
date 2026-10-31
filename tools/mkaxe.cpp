/*
 * AxolynOS - Herramienta de host: empaqueta un ejecutable .axe.
 *
 *   mkaxe <entrada.elf> <salida.axe> <nombre>
 *
 * Un .axe es la cabecera ax_axe_header seguida del ELF64 tal cual:
 *
 *   [ax_axe_header][ELF64]
 *
 * El kernel comprueba la firma, la version, el tipo, el tamano y el CRC
 * antes de entregar el payload al cargador ELF.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "ax_pkg.h"

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkaxe: no se puede abrir %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); fprintf(stderr, "mkaxe: tamano invalido\n"); exit(1); }
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && fread(buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "mkaxe: error de lectura\n");
        exit(1);
    }
    fclose(f);
    return buf;
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        fprintf(stderr, "uso: mkaxe <entrada.elf> <salida.axe> <nombre>\n");
        return 1;
    }
    const char* inPath  = argv[1];
    const char* outPath = argv[2];
    const char* name    = argv[3];

    std::vector<uint8_t> elf = readFile(inPath);

    /* --- Validacion minima de ELF64 little-endian --- */
    if (elf.size() < 64 || elf[0] != 0x7F || elf[1] != 'E' ||
        elf[2] != 'L' || elf[3] != 'F') {
        fprintf(stderr, "mkaxe: %s no es un ELF\n", inPath);
        return 1;
    }
    if (elf[4] != 2 || elf[5] != 1) {
        fprintf(stderr, "mkaxe: se esperaba ELF64 little-endian\n");
        return 1;
    }

    uint64_t entry = rd64(&elf[0x18]);
    uint16_t etype = rd16(&elf[0x10]);
    uint64_t phoff = rd64(&elf[0x20]);
    uint16_t phentsize = rd16(&elf[0x36]);
    uint16_t phnum = rd16(&elf[0x38]);

    uint64_t load = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        uint64_t off = phoff + (uint64_t)i * phentsize;
        if (off + 56 > elf.size()) break;
        const uint8_t* ph = &elf[(size_t)off];
        uint32_t type = rd32(ph);
        if (type == 1) {                 /* PT_LOAD */
            load = rd64(ph + 16);        /* p_vaddr */
            break;
        }
    }

    ax_axe_header hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic = AX_PKG_MAGIC_AXE;
    hdr.version = AX_PKG_VERSION;
    hdr.kind = AX_PKG_KIND_ELF64;
    hdr.flags = (etype == 3 ? AX_PKG_FLAG_PIE : 0u) | AX_PKG_FLAG_STATIC;
    hdr.payload_size = (uint32_t)elf.size();
    hdr.payload_crc32 = ax_pkg_crc32(elf.data(), (uint32_t)elf.size());
    hdr.entry_hint = (uint32_t)entry;
    hdr.load_hint = (uint32_t)load;
    std::snprintf(hdr.name, sizeof(hdr.name), "%s", name);
    std::snprintf(hdr.arch, sizeof(hdr.arch), "x86_64");

    FILE* out = fopen(outPath, "wb");
    if (!out) { fprintf(stderr, "mkaxe: no se puede crear %s\n", outPath); return 1; }
    if (fwrite(&hdr, 1, sizeof(hdr), out) != sizeof(hdr) ||
        (elf.size() && fwrite(elf.data(), 1, elf.size(), out) != elf.size())) {
        fclose(out);
        fprintf(stderr, "mkaxe: error de escritura\n");
        return 1;
    }
    fclose(out);

    printf("mkaxe: %s -> %s (%zu bytes, payload %u, crc %08x)\n",
           inPath, outPath, sizeof(hdr) + elf.size(),
           hdr.payload_size, hdr.payload_crc32);
    return 0;
}
