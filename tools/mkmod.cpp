/*
 * AxolynOS - Herramienta de host: empaqueta un modulo del kernel.
 *
 *   mkmod <modulo.elf> <salida.axmod> <nombre> <ranura>
 *
 * Convierte un ELF64 ya enlazado (con Kernel/linker/module.ld) en el
 * binario plano .axmod que entiende el cargador de modulos del kernel:
 *
 *   [AxModHeader][codigo][datos]
 *
 * El kernel lo copia tal cual en AX_MODULES_VIRT_BASE + ranura*1 MiB, por
 * lo que el empaquetado debe conservar EXACTAMENTE los offsets que el
 * enlazador uso (code_size = .data - base, datos justo despues).
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#define AX_MOD_MAGIC "AXMOD001"
#define AX_MOD_MAGIC_LEN 8

#pragma pack(push, 1)
struct ModHeader {
    char     magic[AX_MOD_MAGIC_LEN];
    uint32_t abi_version;
    uint32_t flags;
    uint64_t entry_offset;
    uint64_t init_offset;
    uint64_t code_size;
    uint64_t data_size;
    uint64_t bss_size;
    uint32_t slot;
    uint32_t reserved;
    char     name[32];
    uint32_t checksum;
};
#pragma pack(pop)

struct Section {
    std::string name;
    uint64_t addr = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t type = 0;
    uint64_t entsize = 0;
    uint64_t link = 0;
};

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkmod: no se puede abrir %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && fread(buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "mkmod: error de lectura\n");
        exit(1);
    }
    fclose(f);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "uso: mkmod <modulo.elf> <salida.axmod> <nombre> <ranura>\n");
        return 1;
    }
    const char* inPath = argv[1];
    const char* outPath = argv[2];
    std::string name = argv[3];
    uint32_t slot = (uint32_t)strtoul(argv[4], nullptr, 0);

    std::vector<uint8_t> image = readFile(inPath);
    if (image.size() < 64) { fprintf(stderr, "mkmod: ELF demasiado pequeno\n"); return 1; }
    if (memcmp(image.data(), "\x7f" "ELF", 4) != 0) {
        fprintf(stderr, "mkmod: no es un ELF\n");
        return 1;
    }

    uint64_t e_entry    = rd64(&image[0x18]);
    uint64_t e_shoff    = rd64(&image[0x28]);
    uint16_t e_shentsize = (uint16_t)(image[0x3A] | (image[0x3B] << 8));
    uint16_t e_shnum    = (uint16_t)(image[0x3C] | (image[0x3D] << 8));
    uint16_t e_shstrndx = (uint16_t)(image[0x3E] | (image[0x3F] << 8));

    if (e_shnum == 0 || e_shoff == 0) {
        fprintf(stderr, "mkmod: el ELF no tiene tabla de secciones\n");
        return 1;
    }

    auto shdr = [&](uint16_t i) -> const uint8_t* {
        return &image[(size_t)e_shoff + (size_t)i * e_shentsize];
    };

    /* Tabla de nombres de las secciones */
    const uint8_t* shstr = shdr(e_shstrndx);
    uint64_t shstr_off = rd64(shstr + 0x18);
    uint64_t shstr_size = rd64(shstr + 0x20);

    std::vector<Section> sections(e_shnum);
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t* s = shdr(i);
        Section& sec = sections[i];
        uint32_t nameOff = rd32(s + 0x00);
        sec.type     = rd32(s + 0x04);
        sec.addr     = rd64(s + 0x10);
        sec.offset   = rd64(s + 0x18);
        sec.size     = rd64(s + 0x20);
        sec.link     = rd32(s + 0x28);
        sec.entsize  = rd64(s + 0x38);
        if (nameOff < shstr_size) {
            const char* n = (const char*)&image[(size_t)shstr_off + nameOff];
            sec.name = n;
        }
    }

    Section* text = nullptr;
    Section* data = nullptr;
    Section* bss = nullptr;
    uint64_t base = 0;
    for (auto& s : sections) {
        if (s.name == ".text" && !text) text = &s;
        else if (s.name == ".data" && !data) data = &s;
        else if (s.name == ".bss" && !bss) bss = &s;
    }
    if (!text) { fprintf(stderr, "mkmod: falta la seccion .text\n"); return 1; }
    base = text->addr;

    /* El codigo va desde .text hasta el inicio de .data (incluye .rodata) */
    uint64_t code_size = (data ? data->addr : (text->addr + text->size)) - base;
    uint64_t code_file_off = text->offset;
    uint64_t code_file_len = (data ? (data->offset - text->offset) : text->size);
    if (code_file_off + code_file_len > image.size()) {
        fprintf(stderr, "mkmod: el ELF esta truncado\n");
        return 1;
    }

    uint64_t data_size = data ? data->size : 0;
    uint64_t bss_size = bss ? bss->size : 0;

    ModHeader hdr{};
    memcpy(hdr.magic, AX_MOD_MAGIC, AX_MOD_MAGIC_LEN);
    hdr.abi_version = 1;
    hdr.flags = 0;
    hdr.entry_offset = (e_entry >= base) ? (e_entry - base) : 0;
    hdr.init_offset  = hdr.entry_offset;
    hdr.code_size = code_size;
    hdr.data_size = data_size;
    hdr.bss_size  = bss_size;
    hdr.slot = slot;
    memset(hdr.name, 0, sizeof(hdr.name));
    strncpy(hdr.name, name.c_str(), sizeof(hdr.name) - 1);

    /* Checksuma simple (suma de bytes del codigo) */
    uint32_t sum = 0;
    for (uint64_t i = 0; i < code_file_len; ++i) sum += image[(size_t)(code_file_off + i)];
    hdr.checksum = sum;

    FILE* out = fopen(outPath, "wb");
    if (!out) { fprintf(stderr, "mkmod: no se puede escribir %s\n", outPath); return 1; }
    fwrite(&hdr, sizeof(hdr), 1, out);
    fwrite(&image[(size_t)code_file_off], 1, (size_t)code_file_len, out);
    if (data_size > 0) {
        fwrite(&image[(size_t)data->offset], 1, (size_t)data_size, out);
    }
    fclose(out);

    printf("mkmod: %s -> %s (ranura %u, codigo %llu, datos %llu, bss %llu)\n",
           inPath, outPath, slot,
           (unsigned long long)code_size, (unsigned long long)data_size,
           (unsigned long long)bss_size);
    return 0;
}
