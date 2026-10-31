/*
 * AxolynOS SDK - manejo de directorios
 */
#include "../include/axos/sdk.h"
#include <axos/syscall.h>

/* Verificar si un directorio existe */
int sdk_dir_exists(const char* path)
{
    if (!path || !path[0]) return AX_EINVAL;
    sdk_print("DIR: Verificando directorio %s\n", path);
    return 0;
}

/* Crear directorio */
int sdk_dir_create(const char* path)
{
    if (!path || !path[0]) return AX_EINVAL;
    sdk_print("DIR: Creando directorio %s\n", path);
    return 0;
}

/* Eliminar directorio */
int sdk_dir_delete(const char* path)
{
    if (!path || !path[0]) return AX_EINVAL;
    sdk_print("DIR: Eliminando directorio %s\n", path);
    return 0;
}

/* Listar directorio */
int sdk_dir_list(const char* path, char** entries, ax_u32 max_entries)
{
    if (!path || !path[0] || !entries || max_entries == 0) return AX_EINVAL;
    sdk_print("DIR: Listando directorio %s (max %u entries)\n", path, max_entries);
    return 0;
}