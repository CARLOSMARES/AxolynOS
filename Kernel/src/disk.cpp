/*
 * AxolynOS - Driver de disco ATA (PIO, LBA28).
 *
 * El instalador y AxFS en disco necesitan leer y escribir sectores de un
 * disco real.  Este driver habla con los controladores ATA clasicos
 * (PIIX/ICH en modo IDE, que es lo que usan QEMU, VirtualBox y la mayoria de
 * placas antiguas) mediante el puerto de E/S 0x1F0/0x170 y PIO de 16 bits.
 *
 * Canales:
 *   - primario   : 0x1F0 (comando), 0x3F6 (control)
 *   - secundario : 0x170 (comando), 0x376 (control)
 *
 * Cada canal tiene maestro (drive 0) y esclavo (drive 1).  Los dispositivos
 * ATAPI (CD/DVD) se detectan y se descartan: no se pueden formatear con AxFS.
 *
 * La API publica compacta los discos presentes: ax_disk_read(0, ...) siempre
 * se refiere al primer disco ATA util, aunque fisicamente sea el esclavo del
 * canal secundario.
 */
#include "ax_disk.h"
#include "ax_console.h"
#include "ax_io.h"
#include "ax_ahci.h"

#define ATA_REG_DATA      0
#define ATA_REG_ERROR     1
#define ATA_REG_FEATURES  1
#define ATA_REG_SECCOUNT  2
#define ATA_REG_LBA0      3
#define ATA_REG_LBA1      4
#define ATA_REG_LBA2      5
#define ATA_REG_DRIVE     6
#define ATA_REG_STATUS    7
#define ATA_REG_COMMAND   7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7

typedef struct {
    uint16_t io;       /* puerto base de comandos */
    uint16_t ctrl;     /* puerto de control/alterno */
    uint8_t  slave;    /* 0 = maestro, 1 = esclavo */
} AxAtaChannel;

static AxDiskInfo   g_info[AX_DISK_MAX_DRIVES];
static AxAtaChannel g_chan[AX_DISK_MAX_DRIVES];
static int          g_disk_count = 0;

/* ------------------------------------------------------------------ */
/* Utilidades de bajo nivel                                            */
/* ------------------------------------------------------------------ */

static inline void ata_delay(uint16_t ctrl)
{
    /* Cuatro lecturas del registro de estado alterno equivalen a ~400 ns. */
    (void)ax_inb(ctrl);
    (void)ax_inb(ctrl);
    (void)ax_inb(ctrl);
    (void)ax_inb(ctrl);
}

static int ata_wait_not_busy(uint16_t io, uint32_t limit)
{
    for (uint32_t i = 0; i < limit; ++i) {
        uint8_t status = ax_inb(io + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_drq(uint16_t io, uint32_t limit)
{
    for (uint32_t i = 0; i < limit; ++i) {
        uint8_t status = ax_inb(io + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -1;
}

static void ata_select(uint16_t io, uint16_t ctrl, uint8_t slave, uint32_t lba)
{
    ax_outb(io + ATA_REG_DRIVE,
            (uint8_t)(0xE0 | (slave ? 0x10 : 0x00) |
                      ((lba >> 24) & 0x0F)));
    ata_delay(ctrl);
}

static void ata_read_words(uint16_t io, void* buffer, uint32_t words)
{
    __asm__ volatile("rep insw"
                     : "+D"(buffer), "+c"(words)
                     : "d"(io)
                     : "memory");
}

static void ata_write_words(uint16_t io, const void* buffer, uint32_t words)
{
    __asm__ volatile("rep outsw"
                     : "+S"(buffer), "+c"(words)
                     : "d"(io)
                     : "memory");
}

/* ------------------------------------------------------------------ */
/* Identificacion                                                      */
/* ------------------------------------------------------------------ */

static bool ata_identify(uint16_t io, uint16_t ctrl, uint8_t slave,
                         AxDiskInfo* out)
{
    /* Seleccion y comprobacion de presencia: un canal vacio devuelve 0x00 o
       0xFF de forma estable. */
    ax_outb(io + ATA_REG_DRIVE, (uint8_t)(0xA0 | (slave ? 0x10 : 0x00)));
    ata_delay(ctrl);
    if (ax_inb(io + ATA_REG_STATUS) == 0xFF) {
        return false;
    }

    ax_outb(io + ATA_REG_SECCOUNT, 0);
    ax_outb(io + ATA_REG_LBA0, 0);
    ax_outb(io + ATA_REG_LBA1, 0);
    ax_outb(io + ATA_REG_LBA2, 0);
    ax_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(ctrl);

    uint8_t status = ax_inb(io + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return false;
    }
    if (ata_wait_not_busy(io, 1000000) != 0) {
        return false;
    }

    /* Firma ATAPI/SATA: si LBA1:LBA0 valen 0x14:0x14 es un CD/DVD. */
    uint8_t lba1 = ax_inb(io + ATA_REG_LBA1);
    uint8_t lba2 = ax_inb(io + ATA_REG_LBA2);
    if (lba1 == 0x14 && lba2 == 0x14) {
        return false;           /* ATAPI, no es un disco AxFS */
    }
    if (ax_inb(io + ATA_REG_STATUS) & ATA_SR_ERR) {
        return false;
    }
    if (ata_wait_drq(io, 1000000) != 0) {
        return false;
    }

    uint16_t id[256];
    ata_read_words(io, id, 256);

    uint64_t sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    /* Si el dispositivo anuncia LBA48 se usa su tamano, pero solo se
       direccionan 28 bits (limitacion del PIO implementado). */
    uint64_t sectors48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                         ((uint64_t)id[102] << 32) |
                         ((uint64_t)id[103] << 48);
    if (sectors48 > sectors) {
        sectors = sectors48;
    }
    if (sectors == 0) {
        return false;
    }

    out->present = true;
    out->is_ata = true;
    out->is_atapi = false;
    out->sector_size = 512;
    out->sectors = sectors;
    out->name[0] = '\0';
    return true;
}

/* ------------------------------------------------------------------ */
/* Lectura / escritura PIO                                             */
/* ------------------------------------------------------------------ */

static int ata_read_sectors(uint16_t io, uint16_t ctrl, uint8_t slave,
                            uint32_t lba, uint32_t count, void* buffer)
{
    if (lba + count > 0x0FFFFFFFu) {
        return -1;
    }

    uint8_t* p = (uint8_t*)buffer;
    while (count > 0) {
        uint32_t chunk = count > 255 ? 255 : count;

        if (ata_wait_not_busy(io, 1000000) != 0) {
            return -1;
        }
        ata_select(io, ctrl, slave, lba);
        ax_outb(io + ATA_REG_FEATURES, 0);
        ax_outb(io + ATA_REG_SECCOUNT, (uint8_t)chunk);
        ax_outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        ax_outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        ax_outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        ax_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        ata_delay(ctrl);

        for (uint32_t s = 0; s < chunk; ++s) {
            if (ata_wait_drq(io, 1000000) != 0) {
                return -1;
            }
            ata_read_words(io, p, 256);
            p += 512;
        }
        ata_delay(ctrl);

        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static int ata_write_sectors(uint16_t io, uint16_t ctrl, uint8_t slave,
                             uint32_t lba, uint32_t count, const void* buffer)
{
    if (lba + count > 0x0FFFFFFFu) {
        return -1;
    }

    const uint8_t* p = (const uint8_t*)buffer;
    while (count > 0) {
        uint32_t chunk = count > 255 ? 255 : count;

        if (ata_wait_not_busy(io, 1000000) != 0) {
            return -1;
        }
        ata_select(io, ctrl, slave, lba);
        ax_outb(io + ATA_REG_FEATURES, 0);
        ax_outb(io + ATA_REG_SECCOUNT, (uint8_t)chunk);
        ax_outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        ax_outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        ax_outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        ax_outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        ata_delay(ctrl);

        for (uint32_t s = 0; s < chunk; ++s) {
            if (ata_wait_drq(io, 1000000) != 0) {
                return -1;
            }
            ata_write_words(io, p, 256);
            p += 512;
        }
        ata_delay(ctrl);

        lba += chunk;
        count -= chunk;
    }

    /* Vaciar la cache interna del disco. */
    ax_outb(io + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    (void)ata_wait_not_busy(io, 1000000);
    return 0;
}

/* ------------------------------------------------------------------ */
/* API publica                                                         */
/* ------------------------------------------------------------------ */

static void ata_set_name(AxDiskInfo* info, int index)
{
    const char* prefix = "hd";
    info->name[0] = prefix[0];
    info->name[1] = prefix[1];
    info->name[2] = (char)('0' + index);
    info->name[3] = '\0';
}

void ax_disk_init(void)
{
    g_disk_count = 0;
    for (int i = 0; i < AX_DISK_MAX_DRIVES; ++i) {
        g_info[i].present = false;
        g_info[i].sectors = 0;
        g_info[i].sector_size = 512;
        g_info[i].is_ata = false;
        g_info[i].is_atapi = false;
        g_info[i].name[0] = '\0';
    }

    /* AHCI se intenta primero: en q35/VirtualBox el disco suele colgar de
       un controlador AHCI.  Si no hay discos AHCI se cae a ATA PIO. */
    ax_ahci_init();

    static const uint16_t io_ports[2]   = { 0x1F0, 0x170 };
    static const uint16_t ctrl_ports[2] = { 0x3F6, 0x376 };

    for (int channel = 0; channel < 2; ++channel) {
        for (int slave = 0; slave < 2; ++slave) {
            AxDiskInfo info;
            for (uint32_t i = 0; i < sizeof(info); ++i) {
                ((uint8_t*)&info)[i] = 0;
            }
            if (!ata_identify(io_ports[channel], ctrl_ports[channel],
                              (uint8_t)slave, &info)) {
                continue;
            }
            if (g_disk_count >= AX_DISK_MAX_DRIVES) {
                break;
            }
            int index = g_disk_count++;
            g_info[index] = info;
            g_chan[index].io = io_ports[channel];
            g_chan[index].ctrl = ctrl_ports[channel];
            g_chan[index].slave = (uint8_t)slave;
            ata_set_name(&g_info[index], index);
            ax_printf("[disk] %s: %llu sectores (%llu MiB, canal %d, %s)\n",
                      g_info[index].name,
                      (unsigned long long)g_info[index].sectors,
                      (unsigned long long)(g_info[index].sectors / 2048ull),
                      channel, slave ? "esclavo" : "maestro");
        }
    }

    if (g_disk_count == 0) {
        ax_printf("[disk] no hay discos ATA/AHCI conectados\n");
    } else {
        ax_printf("[disk] %d disco(s) disponible(s)\n", g_disk_count);
    }
}

int ax_disk_count(void)
{
    return g_disk_count;
}

int ax_disk_get_info(int drive, AxDiskInfo* info)
{
    if (drive < 0 || drive >= g_disk_count || !info) {
        return -1;
    }
    *info = g_info[drive];
    return 0;
}

int ax_disk_read(int drive, uint64_t lba, uint32_t count, void* buf)
{
    if (drive < 0 || drive >= g_disk_count || !buf || count == 0) {
        return -1;
    }
    int rc = ata_read_sectors(g_chan[drive].io, g_chan[drive].ctrl,
                              g_chan[drive].slave, (uint32_t)lba, count, buf);
    if (rc != 0) {
        /* Segundo intento con AHCI por si el disco esta detras de ese
           controlador. */
        rc = ax_ahci_read_lba(lba, count, buf);
    }
    return rc;
}

int ax_disk_write(int drive, uint64_t lba, uint32_t count, const void* buf)
{
    if (drive < 0 || drive >= g_disk_count || !buf || count == 0) {
        return -1;
    }
    int rc = ata_write_sectors(g_chan[drive].io, g_chan[drive].ctrl,
                               g_chan[drive].slave, (uint32_t)lba, count, buf);
    if (rc != 0) {
        rc = ax_ahci_write_lba(lba, count, buf);
    }
    return rc;
}
