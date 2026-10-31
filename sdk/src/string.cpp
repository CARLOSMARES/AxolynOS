/*
 * AxolynOS SDK - funciones de string
 */
#include "../include/axos/sdk.h"

/* Concatenar src al final de dest, maximo max caracteres incluido el terminador nulo */
int sdk_string_concatenar(char* dest, ax_u32 max, const char* src)
{
    if (!dest || !src || max == 0) return -1;
    
    /* Encontrar fin de dest */
    char* d = dest;
    while (*d && (d - dest) < (long long)max) {
        ++d;
    }
    
    /* Calcular espacio restante */
    ax_u32 restante = (ax_u32)(max - (d - dest));
    
    /* Copiar src */
    ax_u32 i = 0;
    while (src[i] && i + 1 < restante) {
        d[i] = src[i];
        ++i;
    }
    d[i] = '\0';
    
    return (int)i;
}