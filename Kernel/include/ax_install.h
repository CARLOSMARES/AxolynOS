/*
 * AxolynOS - Instalador de sistema en disco (BIOS/Legacy).
 *
 * Copia el cargador, el kernel y el initrd (que se esta ejecutando) a un
 * disco ATA y crea un volumen AxFS-D.  El payload stage1/stage2/kernel se
 * incluye en el initrd bajo /install; el initrd se toma de la copia en RAM
 * que cargo el cargador, de modo que no hay dependencia circular al
 * construirlo.
 */
#ifndef AX_INSTALL_H
#define AX_INSTALL_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LBA donde el instalador coloca la cabecera AXDSK. */
#define AX_INSTALL_HEADER_LBA 64ull

typedef struct {
    bool     ready;
    int      drive;
    uint64_t kernel_bytes;
    uint64_t initrd_bytes;
    uint64_t first_free_lba;
} AxInstallInfo;

int ax_install_format(int drive);
int ax_install_system(int drive, bool format_fs);
int ax_install_info(AxInstallInfo* out);
void ax_install_init(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_INSTALL_H */
