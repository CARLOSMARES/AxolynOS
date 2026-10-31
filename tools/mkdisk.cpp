/*
 * AxolynOS - Herramienta de host: genera la imagen de disco "AXDSK".
 *
 *   mkdisk --stage1 build/stage1.bin --stage2 build/stage2.bin \
 *         --kernel build/axolyn.elf --initrd build/initrd.axfs \
 *         --out build/axolynos.img
 *
 * Disposicion de la imagen (ver manuales/03-bootloader.md):
 *
 *   LBA 0        stage1 (512 bytes, firma 0xAA55 en 0x1FE)
 *   LBA 1..63    stage2 (stage2_sectors sectores, el resto se rellena con 0)
 *   LBA 64       cabecera del disco "AXDSK001" (1 sector)
 *   LBA 65..     kernel.elf (kernel_sectors sectores)
 *   LBA ..       initrd.axfs (initrd_sectors sectores)
 *
 * Cabecera (little-endian, offsets fijos):
 *
 *   0x00 char[8]  "AXDSK001"
 *   0x08 u32      version (1)
 *   0x0C u32      sectores de stage2
 *   0x10 u64      LBA de stage2 (1)
 *   0x18 u64      LBA del kernel
 *   0x20 u64      sectores del kernel
 *   0x28 u64      LBA del initrd
 *   0x30 u64      sectores del initrd
 *   0x38 u64      direccion fisica del kernel   (0x200000)
 *   0x40 u64      direccion virtual del kernel  (0xFFFFFFFF80200000)
 *   0x48 u64      direccion fisica del initrd   (0x1F00000)
 *   0x50 u64      direccion de AxBootInfo       (0x1000)
 *   0x58 u64      direccion de la tabla E820    (0x9000)
 *   0x60 u64      tamano de la memoria (0: lo detecta el firmware)
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#define AXDSK_MAGIC "AXDSK001"
#define AXDSK_MAGIC_LEN 8
#define AXDSK_VERSION 1

#define SECTOR_SIZE   512
#define STAGE2_LBA    1
#define STAGE2_MAX    63            /* LBA 1..63 */
#define HEADER_LBA    64

/* Valores por defecto del contrato (Kernel/include/ax_config.h). */
#define DEF_KERNEL_PHYS  0x0000000000200000ull
#define DEF_KERNEL_VIRT  0xFFFFFFFF80200000ull
#define DEF_INITRD_PHYS  0x0000000001F00000ull
#define DEF_BOOTINFO     0x000000000001000ull
#define DEF_E820         0x000000000009000ull

#pragma pack(push, 1)
struct AxdskHeader {
    char     magic[AXDSK_MAGIC_LEN];
    uint32_t version;
    uint32_t stage2_sectors;
    uint64_t stage2_lba;
    uint64_t kernel_lba;
    uint64_t kernel_sectors;
    uint64_t initrd_lba;
    uint64_t initrd_sectors;
    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t initrd_phys;
    uint64_t bootinfo_phys;
    uint64_t e820_phys;
    uint64_t mem_size;
    uint64_t reserved[9];     /* hasta completar el sector */
};
#pragma pack(pop)

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mkdisk: no se puede abrir %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && fread(buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "mkdisk: error de lectura en %s\n", path);
        exit(1);
    }
    fclose(f);
    return buf;
}

static void writeAt(FILE* out, uint64_t lba, const std::vector<uint8_t>& data) {
    if (fseek(out, (long)(lba * SECTOR_SIZE), SEEK_SET) != 0) {
        fprintf(stderr, "mkdisk: no se puede escribir en el LBA %llu\n",
                (unsigned long long)lba);
        exit(1);
    }
    if (!data.empty() && fwrite(data.data(), 1, data.size(), out) != data.size()) {
        fprintf(stderr, "mkdisk: error de escritura\n");
        exit(1);
    }
}

static void writeZeros(FILE* out, uint64_t lba, uint64_t sectors) {
    std::vector<uint8_t> zeros((size_t)(sectors * SECTOR_SIZE), 0);
    writeAt(out, lba, zeros);
}

int main(int argc, char** argv) {
    const char* stage1Path = nullptr;
    const char* stage2Path = nullptr;
    const char* kernelPath = nullptr;
    const char* initrdPath = nullptr;
    const char* outPath    = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                fprintf(stderr, "mkdisk: falta el valor de %s\n", what);
                exit(1);
            }
            return argv[++i];
        };
        if      (a == "--stage1") stage1Path = next("--stage1");
        else if (a == "--stage2") stage2Path = next("--stage2");
        else if (a == "--kernel") kernelPath = next("--kernel");
        else if (a == "--initrd") initrdPath = next("--initrd");
        else if (a == "--out")    outPath    = next("--out");
        else {
            fprintf(stderr, "mkdisk: opcion desconocida '%s'\n", a.c_str());
            return 1;
        }
    }

    if (!stage1Path || !stage2Path || !kernelPath || !initrdPath || !outPath) {
        fprintf(stderr,
                "uso: mkdisk --stage1 <f> --stage2 <f> --kernel <f> "
                "--initrd <f> --out <f>\n");
        return 1;
    }

    std::vector<uint8_t> stage1 = readFile(stage1Path);
    std::vector<uint8_t> stage2 = readFile(stage2Path);
    std::vector<uint8_t> kernel = readFile(kernelPath);
    std::vector<uint8_t> initrd = readFile(initrdPath);

    /* --- stage1: 512 bytes exactos y firma MBR ------------------------- */
    if (stage1.size() != SECTOR_SIZE) {
        fprintf(stderr, "mkdisk: %s debe ocupar exactamente 512 bytes (ocupa %zu)\n",
                stage1Path, stage1.size());
        return 1;
    }
    if (stage1[510] != 0x55 || stage1[511] != 0xAA) {
        fprintf(stderr, "mkdisk: %s no tiene la firma 0xAA55 en el offset 0x1FE\n",
                stage1Path);
        return 1;
    }

    /* --- stage2: como mucho 63 sectores --------------------------------- */
    uint64_t stage2Sectors = (stage2.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (stage2Sectors == 0) {
        fprintf(stderr, "mkdisk: %s esta vacio\n", stage2Path);
        return 1;
    }
    if (stage2Sectors > STAGE2_MAX) {
        fprintf(stderr, "mkdisk: %s ocupa %llu sectores y no cabe en %d\n",
                stage2Path, (unsigned long long)stage2Sectors, STAGE2_MAX);
        return 1;
    }
    stage2.resize((size_t)(stage2Sectors * SECTOR_SIZE), 0);

    /* --- kernel e initrd: alineados a sectores -------------------------- */
    uint64_t kernelSectors = (kernel.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint64_t initrdSectors = (initrd.size() + SECTOR_SIZE - 1) / SECTOR_SIZE;
    kernel.resize((size_t)(kernelSectors * SECTOR_SIZE), 0);
    initrd.resize((size_t)(initrdSectors * SECTOR_SIZE), 0);

    uint64_t kernelLba = HEADER_LBA + 1;
    uint64_t initrdLba = kernelLba + kernelSectors;

    /* --- Cabecera AXDSK ------------------------------------------------- */
    AxdskHeader hdr{};
    memcpy(hdr.magic, AXDSK_MAGIC, AXDSK_MAGIC_LEN);
    hdr.version       = AXDSK_VERSION;
    hdr.stage2_sectors = (uint32_t)stage2Sectors;
    hdr.stage2_lba    = STAGE2_LBA;
    hdr.kernel_lba    = kernelLba;
    hdr.kernel_sectors = kernelSectors;
    hdr.initrd_lba    = initrdLba;
    hdr.initrd_sectors = initrdSectors;
    hdr.kernel_phys   = DEF_KERNEL_PHYS;
    hdr.kernel_virt   = DEF_KERNEL_VIRT;
    hdr.initrd_phys   = DEF_INITRD_PHYS;
    hdr.bootinfo_phys = DEF_BOOTINFO;
    hdr.e820_phys     = DEF_E820;
    hdr.mem_size      = 0;          /* lo detecta el firmware con E820 */

    /* --- Escritura de la imagen ----------------------------------------- */
    FILE* out = fopen(outPath, "wb");
    if (!out) {
        fprintf(stderr, "mkdisk: no se puede escribir %s\n", outPath);
        return 1;
    }

    writeAt(out, 0, stage1);                       /* LBA 0  */
    writeAt(out, STAGE2_LBA, stage2);              /* LBA 1  */
    writeZeros(out, STAGE2_LBA + stage2Sectors,    /* hueco hasta LBA 64 */
               HEADER_LBA - (STAGE2_LBA + stage2Sectors));
    const uint8_t* hdrBytes = reinterpret_cast<const uint8_t*>(&hdr);
    writeAt(out, HEADER_LBA,
            std::vector<uint8_t>(hdrBytes, hdrBytes + sizeof(hdr)));
    writeAt(out, kernelLba, kernel);
    writeAt(out, initrdLba, initrd);
    fclose(out);

    printf("mkdisk: %s (%.1f KiB)\n", outPath,
           (double)((initrdLba + initrdSectors) * SECTOR_SIZE) / 1024.0);
    printf("  stage1  LBA %-6llu %zu bytes\n", 0ULL, stage1.size());
    printf("  stage2  LBA %-6llu %llu sectores (%zu bytes)\n",
           (unsigned long long)STAGE2_LBA, (unsigned long long)stage2Sectors,
           stage2.size());
    printf("  header  LBA %-6llu\n", (unsigned long long)HEADER_LBA);
    printf("  kernel  LBA %-6llu %llu sectores\n",
           (unsigned long long)kernelLba, (unsigned long long)kernelSectors);
    printf("  initrd  LBA %-6llu %llu sectores\n",
           (unsigned long long)initrdLba, (unsigned long long)initrdSectors);
    return 0;
}
