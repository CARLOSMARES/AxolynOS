/*
 * AxolynOS - Driver AHCI (esqueleto)
 */
#include "ax_ahci.h"
#include "ax_console.h"

void ax_ahci_init(void)
{
    /* Inicialización básica - requiere acceso a memoria mapeada AHCI */
    ax_printf("[ahci] buscando controladores AHCI...\n");
}

int ax_ahci_read_lba(uint64_t lba, uint32_t count, void* buf)
{
    (void)lba; (void)count; (void)buf;
    /* Implementación completa requiere HBA mapeado */
    return -1;
}

int ax_ahci_write_lba(uint64_t lba, uint32_t count, const void* buf)
{
    (void)lba; (void)count; (void)buf;
    return -1;
}
