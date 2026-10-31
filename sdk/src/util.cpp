/*
 * AxolynOS SDK - utilidades varias
 */
#include "../include/axos/sdk.h"

/* Obtener el tamaño de un string hasta un maximo */
int sdk_strnlen(const char* s, int max)
{
    int len = 0;
    if (!s) return 0;
    while (len < max && s[len]) {
        ++len;
    }
    return len;
}

/* Copiar un string con limite */
int sdk_strncpy(char* dest, const char* src, int max)
{
    if (!dest || !src || max <= 0) return -1;
    
    int i = 0;
    for (; i < max - 1 && src[i]; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return i;
}

/* Comparar dos strings (hasta max caracteres) */
int sdk_strncmp(const char* a, const char* b, int max)
{
    if (!a || !b) return -1;
    
    int i = 0;
    while (i < max && a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] - b[i];
}

/* Convertir entero a string */
int sdk_int_to_str(int val, char* buf, int max)
{
    if (!buf || max <= 0) return -1;
    
    int i = 0;
    int is_negative = 0;
    if (val < 0) {
        is_negative = 1;
        val = -val;
    }
    
    if (val == 0) {
        if (max > 1) {
            buf[0] = '0';
            buf[1] = '\0';
            return 1;
        }
        return -1;
    }
    
    int digits[32];
    int dcount = 0;
    while (val > 0 && dcount < 32) {
        digits[dcount++] = '0' + (val % 10);
        val /= 10;
    }
    
    int pos = 0;
    if (is_negative && max > 1) {
        buf[pos++] = '-';
    }
    for (int j = dcount - 1; j >= 0 && pos < max - 1; --j) {
        buf[pos++] = digits[j];
    }
    buf[pos] = '\0';
    
    return pos;
}

/* Convertir unsigned entero a string */
int sdk_uint_to_str(unsigned int val, char* buf, int max)
{
    if (!buf || max <= 0) return -1;
    
    int i = 0;
    if (val == 0) {
        if (max > 1) {
            buf[0] = '0';
            buf[1] = '\0';
            return 1;
        }
        return -1;
    }
    
    int digits[32];
    int dcount = 0;
    while (val > 0 && dcount < 32) {
        digits[dcount++] = '0' + (val % 10);
        val /= 10;
    }
    
    int pos = 0;
    for (int j = dcount - 1; j >= 0 && pos < max - 1; --j) {
        buf[pos++] = digits[j];
    }
    buf[pos] = '\0';
    
    return pos;
}