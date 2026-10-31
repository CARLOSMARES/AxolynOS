/*
 * AxolynOS SDK - manejo de usuarios
 */
#include "../include/axos/sdk.h"
#include <axos/syscall.h>

int sdk_user_create(const char* username, const char* password)
{
    if (!username || !username[0] || !password || !password[0]) return AX_EINVAL;
    sdk_print("USER: Creando usuario %s\n", username);
    return 0;
}

int sdk_user_exists(const char* username)
{
    if (!username || !username[0]) return AX_EINVAL;
    sdk_print("USER: Verificando existencia de %s\n", username);
    return 0;
}

int sdk_user_delete(const char* username)
{
    if (!username || !username[0]) return AX_EINVAL;
    sdk_print("USER: Eliminando usuario %s\n", username);
    return 0;
}

int sdk_user_list(char** usernames, ax_u32* count)
{
    if (!usernames || !count) return AX_EINVAL;
    sdk_print("USER: Listando usuarios\n");
    *count = 0;
    return 0;
}