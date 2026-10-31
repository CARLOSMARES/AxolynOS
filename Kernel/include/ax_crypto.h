/*
 * AxolynOS - Criptografia del nucleo (implementacion propia).
 *
 * Este modulo no depende de OpenSSL ni de ninguna libreria externa.  Se
 * usa para:
 *
 *   - SHA-256 y HMAC-SHA256 (autenticacion de registros).
 *   - PBKDF2-HMAC-SHA256 (derivacion de claves de usuario).
 *   - ChaCha20 (cifrado simetrico de la base de usuarios).
 *   - Un generador pseudoaleatorio ChaCha20 sembrado con TSC.
 *
 * Todo el codigo es freestanding: no usa malloc, libc ni excepciones.
 */
#ifndef AX_CRYPTO_H
#define AX_CRYPTO_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_SHA256_SIZE      32
#define AX_CHACHA20_KEY     32
#define AX_CHACHA20_NONCE   12
#define AX_CRYPTO_SALT      16
#define AX_CRYPTO_TAG       32

/* SHA-256 de un bloque de memoria. */
void ax_sha256(const void* data, uint64_t len, uint8_t out[AX_SHA256_SIZE]);

/* HMAC-SHA256. */
void ax_hmac_sha256(const void* key, uint64_t key_len,
                    const void* data, uint64_t data_len,
                    uint8_t out[AX_SHA256_SIZE]);

/*
 * PBKDF2-HMAC-SHA256.  "out_len" no debe superar 32 bytes: es suficiente
 * para las claves ChaCha20 y evita reservar buffers grandes en el kernel.
 */
void ax_pbkdf2_sha256(const char* password, uint64_t password_len,
                      const uint8_t* salt, uint64_t salt_len,
                      uint32_t iterations,
                      uint8_t* out, uint64_t out_len);

/* ChaCha20: cifra/descifra "len" bytes (la operacion es simetrica). */
void ax_chacha20_xor(const uint8_t key[AX_CHACHA20_KEY],
                     const uint8_t nonce[AX_CHACHA20_NONCE],
                     uint32_t counter,
                     const void* in, void* out, uint64_t len);

/* Comparacion en tiempo constante.  Devuelve true si son iguales. */
bool ax_crypto_equal(const void* a, const void* b, uint64_t len);

/* Rellena un buffer con bytes pseudoaleatorios. */
void ax_random_bytes(void* out, uint64_t len);

/* Inicializa explicitamente el generador (opcional). */
void ax_crypto_init(void);

/*
 * Huella hexadecimal de 32 bytes (64 caracteres + '\0').  Util para
 * depuracion y para el comando `crypto`.
 */
void ax_crypto_hex(const uint8_t* data, uint64_t len, char* out, uint64_t max);

#ifdef __cplusplus
}
#endif

#endif /* AX_CRYPTO_H */
