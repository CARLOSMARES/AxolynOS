/*
 * AxolynOS SDK - Encriptación de datos basica
 */
#include "../include/axos/sdk.h"
#include <axos/syscall.h>

/* Encriptar datos (AES-like simple) */
int sdk_encrypt_data(const void* input, ax_u32 input_size, void* output, ax_u32* output_size)
{
    if (!input || !output || !output_size || input_size == 0) return AX_EINVAL;
    
    /* Algoritmo simplificado: XOR con clave fija */
    const ax_u8 key = 0xAB;
    const ax_u8* in = (const ax_u8*)input;
    ax_u8* out = (ax_u8*)output;
    
    for (ax_u32 i = 0; i < input_size; ++i) {
        out[i] = in[i] ^ key;
    }
    
    if (output_size) *output_size = input_size;
    return 0;
}

/* Desencriptar datos */
int sdk_decrypt_data(const void* input, ax_u32 input_size, void* output, ax_u32* output_size)
{
    /* La encriptacion XOR es su propia inversa */
    return sdk_encrypt_data(input, input_size, output, output_size);
}