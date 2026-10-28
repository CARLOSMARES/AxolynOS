/*
 * AxolynOS - Instalador en disco.
 *
 * Formato del disco instalado (identico a build/axolynos.img):
 *
 *   LBA 0        stage1 (MBR, 512 bytes)
 *   LBA 1..63    stage2
 *   LBA 64       cabecera AXDSK001
 *   LBA 65..     kernel.elf
 *   LBA ..       initrd.axfs
 *
 * El sistema de ficheros AxFS-D del usuario empieza en AXFS_DISK_LBA (4 MiB),
 * muy por encima del kernel y del initrd, para que formatear el volumen no
 * destruya el sistema arrancable.
 */
#include "ax_install.h"
#include "ax_fs_disk.h"
#include "ax_disk.h"
#include "ax_kernel.h"
#include "ax_axfs.h"
#include "ax_memory.h"
#include "ax_console.h"
#include "axos/abi.h"

#define AXDSK_MAGIC       "AXDSK001"
#define AXDSK_MAGIC_LEN   8
#define AXDSK_VERSION     1
#define STAGE2_LBA        1ull
#define STAGE2_MAX_SECT   63ull

#pragma pack(push, 1)
typedef struct {
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
    uint64_t reserved[9];
} AxdskHeader;
#pragma pack(pop)

static AxInstallInfo g_last;

static uint64_t inst_strlen(const char* s)
{
    uint64_t n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static void inst_memzero(void* dst, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = 0;
    }
}

static void inst_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static bool inst_memeq(const void* a, const void* b, uint64_t n)
{
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    while (n--) {
        if (*x++ != *y++) {
            return false;
        }
    }
    return true;
}

static uint64_t inst_sectors(uint64_t bytes)
{
    return (bytes + 511ull) / 512ull;
}

/* Copia un fichero del initrd a un buffer alineado a sector, con relleno. */
static uint8_t* inst_load_payload(const char* path, uint64_t* size_out,
                                  uint64_t* sectors_out)
{
    uint64_t size = 0;
    const void* data = ax_vfs_read(path, &size);
    if (!data || size == 0) {
        return nullptr;
    }
    uint64_t sectors = inst_sectors(size);
    uint64_t bytes = sectors * 512ull;
    uint8_t* buffer = (uint8_t*)ax_malloc(bytes);
    if (!buffer) {
        return nullptr;
    }
    inst_memzero(buffer, bytes);
    inst_memcpy(buffer, data, size);
    if (size_out) {
        *size_out = size;
    }
    if (sectors_out) {
        *sectors_out = sectors;
    }
    return buffer;
}

static int inst_write_zeros(int drive, uint64_t lba, uint64_t sectors)
{
    uint8_t zero[512];
    inst_memzero(zero, sizeof(zero));
    for (uint64_t i = 0; i < sectors; ++i) {
        if (ax_disk_write(drive, lba + i, 1, zero) != 0) {
            return AX_EIO;
        }
    }
    return 0;
}

/*
 * Crea los directorios padre de una ruta de AxFS-D ("a/b/c.txt" crea "a" y
 * "a/b").  Los errores de "ya existe" se ignoran.
 */
static void inst_make_parents(const char* path)
{
    char tmp[AXFS_DISK_NAME_MAX];
    uint64_t len = inst_strlen(path);
    if (len >= sizeof(tmp)) {
        return;
    }
    inst_memcpy(tmp, path, len + 1);

    for (uint64_t i = 0; i < len; ++i) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (tmp[0]) {
            (void)ax_fs_disk_mkdir(tmp);
        }
        tmp[i] = '/';
    }
}

/*
 * Copia TODO el contenido del initrd (aplicaciones, bibliotecas, ayuda y el
 * propio instalador) al volumen AxFS-D recien formateado.  Asi, al arrancar
 * desde el disco, "dir" muestra el mismo arbol que el medio de instalacion y
 * los ficheros se pueden editar/borrar de verdad.
 *
 * Se hacen dos pasadas: primero todos los directorios y despues los ficheros,
 * para que ninguna ruta dependa del orden de la tabla del initrd.
 */
static void inst_copy_initrd_to_disk(void)
{
    uint32_t total = ax_vfs_count();
    uint32_t dirs = 0;
    uint32_t files = 0;

    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < total; ++i) {
            const AxfsEntry* e = ax_vfs_entry(i);
            if (!e || !e->name[0]) {
                continue;
            }

            uint64_t len = inst_strlen(e->name);
            while (len > 0 && e->name[len - 1] == '/') {
                len--;
            }
            if (len == 0 || len >= AXFS_DISK_NAME_MAX) {
                continue;
            }
            char name[AXFS_DISK_NAME_MAX];
            inst_memcpy(name, e->name, len);
            name[len] = '\0';

            if (pass == 0 && e->type == AXFS_TYPE_DIR) {
                inst_make_parents(name);
                if (ax_fs_disk_mkdir(name) == 0) {
                    dirs++;
                }
            } else if (pass == 1 && e->type == AXFS_TYPE_FILE) {
                inst_make_parents(name);
                uint64_t size = 0;
                const void* data = ax_vfs_read(e->name, &size);
                if (!data && size != 0) {
                    continue;
                }
                if (ax_fs_disk_write(name, data, size) == 0) {
                    files++;
                }
            }
        }
    }

    ax_printf("install: copiados %u directorios y %u ficheros a AxFS-D\n",
              (unsigned)dirs, (unsigned)files);
}

int ax_install_format(int drive)
{
    int rc = ax_fs_disk_format(drive);
    if (rc == 0) {
        g_last.ready = true;
        g_last.drive = drive;
    }
    return rc;
}

int ax_install_system(int drive, bool format_fs)
{
    AxDiskInfo dinfo;
    if (ax_disk_get_info(drive, &dinfo) != 0) {
        ax_puts("install: no existe ese disco\n");
        return AX_ENOENT;
    }

    uint64_t stage1_size = 0, stage2_size = 0, kernel_size = 0;
    uint64_t stage2_sectors = 0, kernel_sectors = 0;
    uint8_t* stage1 = inst_load_payload("/install/stage1.bin",
                                        &stage1_size, nullptr);
    uint8_t* stage2 = inst_load_payload("/install/stage2.bin",
                                        &stage2_size, &stage2_sectors);
    uint8_t* kernel = inst_load_payload("/install/axolyn.elf",
                                        &kernel_size, &kernel_sectors);

    if (!stage1 || !stage2 || !kernel) {
        if (stage1) ax_free(stage1);
        if (stage2) ax_free(stage2);
        if (kernel) ax_free(kernel);
        ax_puts("install: faltan /install/stage1.bin, /install/stage2.bin "
                "o /install/axolyn.elf en el initrd\n");
        return AX_ENOENT;
    }
    if (stage1_size != 512) {
        ax_free(stage1);
        ax_free(stage2);
        ax_free(kernel);
        ax_puts("install: stage1.bin debe ocupar 512 bytes\n");
        return AX_EINVAL;
    }
    if (stage2_sectors == 0 || stage2_sectors > STAGE2_MAX_SECT) {
        ax_free(stage1);
        ax_free(stage2);
        ax_free(kernel);
        ax_puts("install: stage2.bin no cabe en LBA 1..63\n");
        return AX_EINVAL;
    }

    AxBootInfo* bi = ax_boot_info();
    if (!bi || !bi->initrd_phys || bi->initrd_size == 0) {
        ax_free(stage1);
        ax_free(stage2);
        ax_free(kernel);
        ax_puts("install: no se puede localizar el initrd en RAM\n");
        return AX_EIO;
    }

    uint64_t initrd_size = bi->initrd_size;
    uint64_t initrd_sectors = inst_sectors(initrd_size);
    uint8_t* initrd = (uint8_t*)ax_malloc(initrd_sectors * 512ull);
    if (!initrd) {
        ax_free(stage1);
        ax_free(stage2);
        ax_free(kernel);
        ax_puts("install: sin memoria para copiar el initrd\n");
        return AX_ENOMEM;
    }
    inst_memzero(initrd, initrd_sectors * 512ull);
    inst_memcpy(initrd, (const void*)(uintptr_t)bi->initrd_phys, initrd_size);

    uint64_t kernel_lba = AX_INSTALL_HEADER_LBA + 1ull;
    uint64_t initrd_lba = kernel_lba + kernel_sectors;
    uint64_t first_free = initrd_lba + initrd_sectors;

    if (first_free >= AXFS_DISK_LBA) {
        ax_printf("install: el initrd termina en LBA %llu y no deja sitio "
                  "para AxFS (limite %llu)\n",
                  (unsigned long long)first_free,
                  (unsigned long long)AXFS_DISK_LBA);
        ax_free(stage1);
        ax_free(stage2);
        ax_free(kernel);
        ax_free(initrd);
        return AX_ENOSPC;
    }

    ax_printf("install: disco %s, %llu MiB\n", dinfo.name,
              (unsigned long long)(dinfo.sectors / 2048ull));
    ax_printf("  stage1  LBA 0       512 bytes\n");
    ax_printf("  stage2  LBA 1       %llu sectores\n",
              (unsigned long long)stage2_sectors);
    ax_printf("  header  LBA 64\n");
    ax_printf("  kernel  LBA %llu   %llu sectores\n",
              (unsigned long long)kernel_lba,
              (unsigned long long)kernel_sectors);
    ax_printf("  initrd  LBA %llu   %llu sectores\n",
              (unsigned long long)initrd_lba,
              (unsigned long long)initrd_sectors);

    int rc = 0;

    /* MBR y stage2. */
    if (ax_disk_write(drive, 0, 1, stage1) != 0) {
        rc = AX_EIO;
        goto out;
    }
    if (ax_disk_write(drive, STAGE2_LBA, (uint32_t)stage2_sectors,
                      stage2) != 0) {
        rc = AX_EIO;
        goto out;
    }
    /* Limpia el resto de LBA 1..63 para no dejar restos de una instalacion
       anterior. */
    if (inst_write_zeros(drive, STAGE2_LBA + stage2_sectors,
                         STAGE2_MAX_SECT + 1ull -
                             (STAGE2_LBA + stage2_sectors)) != 0) {
        rc = AX_EIO;
        goto out;
    }

    /* Cabecera AXDSK. */
    {
        uint8_t header_sector[512];
        inst_memzero(header_sector, sizeof(header_sector));
        AxdskHeader hdr;
        inst_memzero(&hdr, sizeof(hdr));
        inst_memcpy(hdr.magic, AXDSK_MAGIC, AXDSK_MAGIC_LEN);
        hdr.version        = AXDSK_VERSION;
        hdr.stage2_sectors = (uint32_t)stage2_sectors;
        hdr.stage2_lba     = STAGE2_LBA;
        hdr.kernel_lba     = kernel_lba;
        hdr.kernel_sectors = kernel_sectors;
        hdr.initrd_lba     = initrd_lba;
        hdr.initrd_sectors = initrd_sectors;
        hdr.kernel_phys    = AX_KERNEL_PHYS_BASE;
        hdr.kernel_virt    = AX_KERNEL_VIRT_BASE;
        hdr.initrd_phys    = AX_INITRD_PHYS;
        hdr.bootinfo_phys  = 0x1000;
        hdr.e820_phys      = 0x9000;
        hdr.mem_size       = 0;
        inst_memcpy(header_sector, &hdr, sizeof(hdr));
        if (ax_disk_write(drive, AX_INSTALL_HEADER_LBA, 1,
                          header_sector) != 0) {
            rc = AX_EIO;
            goto out;
        }
    }

    if (ax_disk_write(drive, kernel_lba, (uint32_t)kernel_sectors,
                      kernel) != 0) {
        rc = AX_EIO;
        goto out;
    }
    if (ax_disk_write(drive, initrd_lba, (uint32_t)initrd_sectors,
                      initrd) != 0) {
        rc = AX_EIO;
        goto out;
    }

    /* Verificacion basica de lo escrito. */
    {
        uint8_t verify[512];
        if (ax_disk_read(drive, 0, 1, verify) != 0 ||
            verify[510] != 0x55 || verify[511] != 0xAA) {
            ax_puts("install: la verificacion del MBR ha fallado\n");
            rc = AX_EIO;
            goto out;
        }
        inst_memzero(verify, sizeof(verify));
        if (ax_disk_read(drive, AX_INSTALL_HEADER_LBA, 1, verify) != 0) {
            rc = AX_EIO;
            goto out;
        }
        AxdskHeader* back = (AxdskHeader*)verify;
        if (!inst_memeq(back->magic, AXDSK_MAGIC, AXDSK_MAGIC_LEN) ||
            back->kernel_sectors != kernel_sectors ||
            back->initrd_sectors != initrd_sectors) {
            ax_puts("install: la verificacion de AXDSK ha fallado\n");
            rc = AX_EIO;
            goto out;
        }
    }

    if (format_fs) {
        rc = ax_install_format(drive);
        if (rc != 0) {
            ax_printf("install: aviso, no se pudo formatear AxFS (error %d)\n",
                      rc);
            rc = 0;
        } else {
            inst_copy_initrd_to_disk();
            static const char welcome[] =
                "AxolynOS instalado en este disco.\r\n"
                "El sistema arranca con el cargador BIOS/Legacy.\r\n"
                "Usa 'help' para ver las ordenes, 'vi' o 'edit' para editar\r\n"
                "ficheros y 'dir' para listar este volumen AxFS.\r\n";
            ax_fs_disk_write("/LEEME.TXT", welcome,
                             sizeof(welcome) - 1);
            ax_printf("install: AxFS-D con %u entradas en la raiz del volumen\n",
                      (unsigned)ax_fs_disk_entry_count());
        }
    }

    g_last.ready = true;
    g_last.drive = drive;
    g_last.kernel_bytes = kernel_size;
    g_last.initrd_bytes = initrd_size;
    g_last.first_free_lba = first_free;

    ax_puts("install: sistema instalado. Reinicia y arranca desde el disco.\n");

out:
    ax_free(stage1);
    ax_free(stage2);
    ax_free(kernel);
    ax_free(initrd);
    return rc;
}

int ax_install_info(AxInstallInfo* out)
{
    if (!out) {
        return AX_EINVAL;
    }
    *out = g_last;
    return 0;
}

void ax_install_init(void)
{
    inst_memzero(&g_last, sizeof(g_last));
    g_last.drive = -1;
}
