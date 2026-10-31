/*
 * AxolynOS SDK - recolector de basura
 */
#include "../include/axos/sdk.h"

/* Activar recolector de basura */
void sdk_gc(void)
{
    sdk_print("GC: Ejecutando recolector de basura\n");
}

/* Forzar collection de una generacion */
int sdk_gc_generation(int gen)
{
    if (gen < 0) return AX_EINVAL;
    sdk_print("GC: Forzando collection generacion %d\n", gen);
    return 0;
}