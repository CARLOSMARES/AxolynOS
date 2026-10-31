/*
 * AxolynOS SDK - Manejo del compilador y nuevas funcionalidades
 */
#include "../include/axos/sdk.h"

/* Agregar una nueva nativa al compilador */
int sdk_compiler_add_native(const char* name, int id, int (*func)(uint64_t*, uint64_t*))
{
    if (!name || name[0] == 0) return AX_EINVAL;
    if (id < 100) return AX_EINVAL;  /* Nuevas natives empiezan >= 100 */
    
    sdk_print("Compiler: Agregando nativa '%s' con id %d\n", name, id);
    /* En kernel real registraria la syscall */
    return 0;
}

/* Remover una nativa del compilador */
int sdk_compiler_remove_native(const char* name)
{
    if (!name || name[0] == 0) return AX_EINVAL;
    
    sdk_print("Compiler: Removiendo nativa '%s'\n", name);
    return 0;  /* Placeholder */
}

/* Obtener informacion del compilador */
int sdk_compiler_info(void)
{
    sdk_print("Compiler: Informacion del compilador Axolyn\n");
    return 0;  /* Placeholder */
}