/*
 * AxolynOS - Herramienta de host: empaqueta un directorio como initramfs
 * en formato AXFS (AXFS0001).
 *
 *   mkinitramfs <directorio> <salida.axfs>
 *
 * El formato es el que declara Kernel/include/ax_axfs.h:
 *
 *   [AxfsHeader][tabla de AxfsEntry][blob de datos]
 *
 *   AxfsHeader (48 bytes)
 *     0x00 char[8]  "AXFS0001"
 *     0x08 u32      version
 *     0x0C u32      count        (numero de entradas)
 *     0x10 u64      index_off    (offset de la tabla de entradas)
 *     0x18 u64      data_off     (offset del blob de datos)
 *     0x20 u64      total_size   (tamano total de la imagen)
 *     0x28 u32      flags
 *     0x2C u32      reserved
 *
 *   AxfsEntry (96 bytes)
 *     0x00 char[64] name        (ruta relativa a la raiz, con '/')
 *     0x40 u32      type        (1 = fichero, 2 = directorio)
 *     0x44 u32      flags
 *     0x48 u64      offset      (dentro del blob de datos)
 *     0x50 u64      size
 *     0x58 u32      parent      (indice de la entrada del directorio padre)
 *     0x5C u32      reserved
 *
 * La entrada 0 es siempre la raiz ("/"): un directorio cuyo padre es
 * ella misma.  Los nombres de los directorios llevan "/" final para que
 * el kernel pueda construir las rutas sin mirar el tipo de la entrada.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#define AXFS_MAGIC "AXFS0001"
#define AXFS_MAGIC_LEN 8
#define AXFS_VERSION 1

#define AXFS_TYPE_FILE 1
#define AXFS_TYPE_DIR  2

#define AXFS_NAME_MAX 64

#pragma pack(push, 1)
struct AxfsHeader {
    char     magic[AXFS_MAGIC_LEN];
    uint32_t version;
    uint32_t count;
    uint64_t index_off;
    uint64_t data_off;
    uint64_t total_size;
    uint32_t flags;
    uint32_t reserved;
};

struct AxfsEntry {
    char     name[AXFS_NAME_MAX];
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t parent;
    uint32_t reserved;
};
#pragma pack(pop)

namespace fs = std::filesystem;

struct Builder {
    std::vector<AxfsEntry> entries;
    std::vector<uint8_t>    data;
    bool                    verbose = false;

    /* Añade una entrada.  Devuelve su indice. */
    uint32_t addEntry(const std::string& name, uint32_t type,
                      uint32_t parent, uint64_t offset, uint64_t size) {
        if (name.size() >= AXFS_NAME_MAX) {
            fprintf(stderr,
                    "mkinitramfs: nombre demasiado largo (%zu >= %d): %s\n",
                    name.size(), AXFS_NAME_MAX, name.c_str());
            exit(1);
        }
        AxfsEntry e{};
        memcpy(e.name, name.c_str(), name.size());
        e.type     = type;
        e.flags    = 0;
        e.offset   = offset;
        e.size     = size;
        e.parent   = parent;
        e.reserved = 0;
        entries.push_back(e);
        return static_cast<uint32_t>(entries.size() - 1);
    }

    /* Recorre un directorio: primero la entrada del directorio y despues
     * su contenido (en orden alfabetico, para que la imagen sea
     * reproducible). */
    void walk(const fs::path& dir, const std::string& rel, uint32_t parent) {
        std::vector<fs::path> children;
        for (const auto& e : fs::directory_iterator(dir)) {
            children.push_back(e.path());
        }
        std::sort(children.begin(), children.end(),
                  [](const fs::path& a, const fs::path& b) {
                      return a.filename().string() < b.filename().string();
                  });

        for (const auto& p : children) {
            std::string name = rel + p.filename().string();
            if (fs::is_directory(p)) {
                uint32_t idx = addEntry(name + "/", AXFS_TYPE_DIR, parent, 0, 0);
                if (verbose) {
                    printf("  dir  %s (idx %u)\n", name.c_str(), idx);
                }
                walk(p, name + "/", idx);
            } else if (fs::is_regular_file(p)) {
                uint64_t off = data.size();
                uint64_t size = 0;
                FILE* f = fopen(p.string().c_str(), "rb");
                if (!f) {
                    fprintf(stderr, "mkinitramfs: no se puede abrir %s\n",
                            p.string().c_str());
                    exit(1);
                }
                uint8_t buf[64 * 1024];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                    data.insert(data.end(), buf, buf + n);
                    size += n;
                }
                fclose(f);
                uint32_t idx = addEntry(name, AXFS_TYPE_FILE, parent, off, size);
                if (verbose) {
                    printf("  file %s (%llu bytes, idx %u)\n", name.c_str(),
                           (unsigned long long)size, idx);
                }
            } else {
                fprintf(stderr, "mkinitramfs: se ignora %s (no regular)\n",
                        p.string().c_str());
            }
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: mkinitramfs <directorio> <salida.axfs>\n");
        return 1;
    }
    const char* inPath  = argv[1];
    const char* outPath = argv[2];

    if (!fs::exists(inPath) || !fs::is_directory(inPath)) {
        fprintf(stderr, "mkinitramfs: '%s' no es un directorio\n", inPath);
        return 1;
    }

    Builder b;
    b.verbose = (getenv("MKINITRAMFS_VERBOSE") != nullptr);

    /* Entrada 0: la raiz.  Su parent es ella misma. */
    b.addEntry("", AXFS_TYPE_DIR, 0, 0, 0);
    b.walk(fs::path(inPath), "", 0);

    AxfsHeader hdr{};
    memcpy(hdr.magic, AXFS_MAGIC, AXFS_MAGIC_LEN);
    hdr.version    = AXFS_VERSION;
    hdr.count      = static_cast<uint32_t>(b.entries.size());
    hdr.index_off  = sizeof(AxfsHeader);
    hdr.data_off   = sizeof(AxfsHeader) +
                     static_cast<uint64_t>(b.entries.size()) * sizeof(AxfsEntry);
    hdr.total_size = hdr.data_off + b.data.size();
    hdr.flags      = 0;
    hdr.reserved   = 0;

    FILE* out = fopen(outPath, "wb");
    if (!out) {
        fprintf(stderr, "mkinitramfs: no se puede escribir %s\n", outPath);
        return 1;
    }
    fwrite(&hdr, sizeof(hdr), 1, out);
    if (!b.entries.empty()) {
        fwrite(b.entries.data(), sizeof(AxfsEntry), b.entries.size(), out);
    }
    if (!b.data.empty()) {
        fwrite(b.data.data(), 1, b.data.size(), out);
    }
    fclose(out);

    printf("mkinitramfs: %s -> %s (%u entradas, %llu bytes de datos, %llu bytes total)\n",
           inPath, outPath, hdr.count,
           (unsigned long long)b.data.size(),
           (unsigned long long)hdr.total_size);
    return 0;
}
