/*
 * mksys - Crea un archivo de sistema protegido (.axsys) para AxolynOS.
 *
 * Uso:
 *   mksys <entrada> <salida.axsys> <nombre> <producto> <fabricante> [descripcion]
 *         [-k clave=valor]... [-t raw|elf|data|config] [--no-sign]
 *
 * El contenido de "entrada" se envuelve con una cabecera que incluye
 * metadatos, un CRC-32 del contenido y una firma SHA-256.  El kernel valida
 * los tres antes de cargarlo (ver Kernel/src/sysfile.cpp).
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */
/* SHA-256 (implementacion estandar, sin dependencias)                 */
/* ------------------------------------------------------------------ */
static const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = hh + S1 + ch + SHA_K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha256(const uint8_t* data, uint64_t len, uint8_t out[32])
{
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint64_t full = len / 64;
    for (uint64_t i = 0; i < full; ++i) {
        sha256_transform(h, data + i * 64);
    }

    uint8_t tail[128];
    uint64_t rem = len - full * 64;
    std::memcpy(tail, data + full * 64, rem);
    tail[rem++] = 0x80;

    uint64_t tail_blocks = (rem + 8 + 63) / 64;
    std::memset(tail + rem, 0, tail_blocks * 64 - rem);
    uint64_t bits = len * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_blocks * 64 - 8 + i] = (uint8_t)(bits >> (56 - i * 8));
    }
    for (uint64_t i = 0; i < tail_blocks; ++i) {
        sha256_transform(h, tail + i * 64);
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

/* ------------------------------------------------------------------ */
/* CRC-32 IEEE                                                         */
/* ------------------------------------------------------------------ */
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, uint64_t len)
{
    for (uint64_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return crc;
}

static uint32_t crc32(const uint8_t* data, uint64_t len)
{
    return ~crc32_update(0xFFFFFFFFu, data, len);
}

/* ------------------------------------------------------------------ */
/* Formato .axsys                                                      */
/* ------------------------------------------------------------------ */
#pragma pack(push, 1)
struct AxSysFileHeader {
    char     magic[8];
    uint16_t version;
    uint16_t flags;
    uint32_t meta_count;
    uint32_t content_type;
    uint64_t content_size;
    uint64_t content_crc32;
    uint64_t header_crc32;
    char     name[32];
    char     product[32];
    char     manufacturer[32];
    char     description[64];
    uint32_t reserved;
};

struct AxSysFileMeta {
    char key[16];
    char value[64];
};
#pragma pack(pop)

#define AX_SYSFILE_MAGIC       "AXSYS001"
#define AX_SYSFILE_VERSION     1
#define AX_SYSFILE_FLAG_READONLY (1u << 0)
#define AX_SYSFILE_FLAG_KERNEL   (1u << 1)
#define AX_SYSFILE_FLAG_SIGNED   (1u << 2)

static void copy_fixed(char* dst, size_t size, const std::string& src)
{
    std::memset(dst, 0, size);
    std::memcpy(dst, src.c_str(), src.size() < size ? src.size() : size - 1);
}

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::printf("uso: mksys <entrada> <salida.axsys> <nombre> <producto> "
                    "<fabricante> [descripcion] [-k clave=valor]... "
                    "[-t raw|elf|data|config] [--no-sign]\n");
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    std::string name = argv[3];
    std::string product = argv[4];
    std::string manufacturer = argv[5];
    std::string description = (argc > 6 && argv[6][0] != '-') ? argv[6] : "";

    std::vector<std::pair<std::string, std::string>> meta;
    uint32_t content_type = 1;  /* RAW */
    bool sign = true;

    for (int i = 6; i < argc; ++i) {
        if (std::strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            std::string kv = argv[++i];
            size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "mksys: metadato sin '=': %s\n", kv.c_str());
                return 1;
            }
            meta.push_back({ kv.substr(0, eq), kv.substr(eq + 1) });
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            std::string t = argv[++i];
            if (t == "raw")            content_type = 1;
            else if (t == "elf")       content_type = 2;
            else if (t == "data")      content_type = 3;
            else if (t == "config")    content_type = 4;
            else {
                std::fprintf(stderr, "mksys: tipo desconocido: %s\n", t.c_str());
                return 1;
            }
        } else if (std::strcmp(argv[i], "--no-sign") == 0) {
            sign = false;
        }
    }

    if (meta.size() > 8) {
        std::fprintf(stderr, "mksys: maximo 8 metadatos\n");
        return 1;
    }

    /* Lee el contenido. */
    FILE* f = std::fopen(input_path, "rb");
    if (!f) {
        std::fprintf(stderr, "mksys: no se puede abrir %s\n", input_path);
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long file_size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (file_size < 0) {
        std::fclose(f);
        std::fprintf(stderr, "mksys: tamano invalido\n");
        return 1;
    }
    std::vector<uint8_t> content((size_t)file_size);
    if (file_size > 0 && std::fread(content.data(), 1, (size_t)file_size, f) != (size_t)file_size) {
        std::fclose(f);
        std::fprintf(stderr, "mksys: error de lectura\n");
        return 1;
    }
    std::fclose(f);

    /* Cabecera. */
    AxSysFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, AX_SYSFILE_MAGIC, 8);
    hdr.version     = AX_SYSFILE_VERSION;
    hdr.flags       = AX_SYSFILE_FLAG_READONLY | AX_SYSFILE_FLAG_KERNEL;
    if (sign) {
        hdr.flags |= AX_SYSFILE_FLAG_SIGNED;
    }
    hdr.meta_count   = (uint32_t)meta.size();
    hdr.content_type = content_type;
    hdr.content_size = (uint64_t)content.size();
    hdr.content_crc32 = crc32(content.data(), content.size());
    copy_fixed(hdr.name, sizeof(hdr.name), name);
    copy_fixed(hdr.product, sizeof(hdr.product), product);
    copy_fixed(hdr.manufacturer, sizeof(hdr.manufacturer), manufacturer);
    copy_fixed(hdr.description, sizeof(hdr.description), description);
    hdr.header_crc32 = 0;
    hdr.header_crc32 = (uint64_t)crc32((const uint8_t*)&hdr, sizeof(hdr));

    /* Firma SHA-256 del contenido. */
    uint8_t signature[32];
    if (sign) {
        sha256(content.data(), content.size(), signature);
    } else {
        std::memset(signature, 0, sizeof(signature));
    }

    /* Escribe el fichero. */
    FILE* out = std::fopen(output_path, "wb");
    if (!out) {
        std::fprintf(stderr, "mksys: no se puede crear %s\n", output_path);
        return 1;
    }
    std::fwrite(&hdr, sizeof(hdr), 1, out);
    for (size_t i = 0; i < meta.size(); ++i) {
        AxSysFileMeta m;
        std::memset(&m, 0, sizeof(m));
        copy_fixed(m.key, sizeof(m.key), meta[i].first);
        copy_fixed(m.value, sizeof(m.value), meta[i].second);
        std::fwrite(&m, sizeof(m), 1, out);
    }
    std::fwrite(signature, 1, sizeof(signature), out);
    if (!content.empty()) {
        std::fwrite(content.data(), 1, content.size(), out);
    }
    std::fclose(out);

    std::printf("mksys: %s (%llu bytes, %llu metadatos, crc=%08llx, firma=%s)\n",
                output_path,
                (unsigned long long)content.size(),
                (unsigned long long)meta.size(),
                (unsigned long long)hdr.content_crc32,
                sign ? "sha256" : "no");
    return 0;
}
