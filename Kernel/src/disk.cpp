/*
 * AxolynOS - Driver de disco (esqueleto IDE/AHCI)
 */
#include "ax_disk.h"
#include "ax_console.h"
#include "ax_ahci.h"

static AxDiskInfo g_disks[AX_DISK_MAX_DRIVES];
static int g_disk_count = 0;

void ax_disk_init(void)
{
    g_disk_count = 0;
    for (int i = 0; i < AX_DISK_MAX_DRIVES; ++i) {
        g_disks[i].present = false;
        g_disks[i].sectors = 0;
        g_disks[i].sector_size = 512;
    }
    /* AHCI se intentará inicializar si hay controlador PCI */
    ax_ahci_init();
    g_disk_count = 1;
    g_disks[0].present = true;
    g_disks[0].is_ata = true;
    g_disks[0].sector_size = 512;
    g_disks[0].sectors = 0;
    /* Copia explicita: un bucle sobre el literal leeria fuera de el. */
    const char* n = "axdisk0";
    int i = 0;
    while (n[i] && i < (int)sizeof(g_disks[0].name) - 1) {
        g_disks[0].name[i] = n[i];
        ++i;
    }
    g_disks[0].name[i] = '\0';
    ax_printf("[disk] driver inicializado\n");
}

int ax_disk_count(void) { return g_disk_count; }

int ax_disk_get_info(int drive, AxDiskInfo* info)
{
    if (drive < 0 || drive >= g_disk_count || !info || !g_disks[drive].present) return -1;
    *info = g_disks[drive];
    return 0;
}

int ax_disk_read(int drive, uint64_t lba, uint32_t count, void* buf)
{
    if (drive < 0 || drive >= g_disk_count || !buf || count == 0) return -1;
    return ax_ahci_read_lba(lba, count, buf);
}

int ax_disk_write(int drive, uint64_t lba, uint32_t count, const void* buf)
{
    if (drive < 0 || drive >= g_disk_count || !buf || count == 0) return -1;
    return ax_ahci_write_lba(lba, count, buf);
}
