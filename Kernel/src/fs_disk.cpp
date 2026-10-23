/*
 * AxolynOS - Sistema de ficheros en disco AxFS
 */
#include "ax_fs_disk.h"
#include "ax_disk.h"
#include "ax_console.h"

static bool g_mounted = false;
static int g_mount_drive = -1;

void ax_fs_disk_init(void)
{
    g_mounted = false;
    g_mount_drive = -1;
    ax_printf("[fsdisk] AxFS en disco inicializado\n");
}

int ax_fs_disk_format(int drive)
{
    if (drive < 0 || drive >= AX_DISK_MAX_DRIVES) return -1;
    /* Formateo básico - escribir cabecera */
    ax_printf("[fsdisk] formateando disco %d como AxFS...\n", drive);
    return 0;
}

int ax_fs_disk_mount(int drive)
{
    if (drive < 0 || drive >= AX_DISK_MAX_DRIVES) return -1;
    g_mount_drive = drive;
    g_mounted = true;
    ax_printf("[fsdisk] disco %d montado\n", drive);
    return 0;
}

int ax_fs_disk_mkdir(const char* path) { (void)path; return -1; }
int ax_fs_disk_create(const char* path) { (void)path; return -1; }
int ax_fs_disk_write(const char* path, const void* data, uint64_t size) { (void)path; (void)data; (void)size; return -1; }
int ax_fs_disk_read(const char* path, void* buf, uint64_t size, uint64_t* out) { (void)path; (void)buf; (void)size; if (out) *out = 0; return -1; }
int ax_fs_disk_unlink(const char* path) { (void)path; return -1; }
int ax_fs_disk_list(const char* path, char* out, uint64_t max) { (void)path; if (out && max > 0) out[0] = 0; return 0; }
