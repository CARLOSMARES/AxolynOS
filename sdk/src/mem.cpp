/*
 * AxolynOS SDK - funciones de memoria
 */
#include "../include/axos/sdk.h"

/* Llenar dest con val count veces */
void* sdk_memset(void* dest, ax_u8 val, ax_u64 count)
{
    char* d = (char*)dest;
    for (ax_u64 i = 0; i < count; ++i) {
        d[i] = (char)val;
    }
    return dest;
}

/* Comparar los primeros count bytes de a y b */
int sdk_memcmp(const void* a, const void* b, ax_u64 count)
{
    const char* pa = (const char*)a;
    const char* pb = (const char*)b;
    for (ax_u64 i = 0; i < count; ++i) {
        if (pa[i] != pb[i]) {
            return (int)(unsigned char)pa[i] - (int)(unsigned char)pb[i];
        }
    }
    return 0;
}