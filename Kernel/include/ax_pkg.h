/*
 * AxolynOS - Formatos de paquete .axe (ejecutable) y .axl (libreria).
 *
 * Un .axe es un contenedor con cabecera propia seguido del ejecutable
 * (ELF64 PIE).  El nucleo comprueba la cabecera, el CRC y el tamano antes
 * de pasar el payload al cargador ELF.
 *
 * Un .axl es un contenedor con cabecera propia seguido de un archivo
 * estatico "ar" (el mismo que produce `ar rcs`).  La herramienta axl2a
 * extrae el archivo para enlazar; el nucleo solo necesita la cabecera
 * para listar la biblioteca en la shell (`libs`).
 *
 * Todas las estructuras van empaquetadas y en little-endian (x86_64).
 */
#ifndef AX_PKG_H
#define AX_PKG_H

#include <stdint.h>

#define AX_PKG_VERSION 1

/* Firmas: 'A','X','E','1' y 'A','X','L','1'. */
#define AX_PKG_MAGIC_AXE 0x31455841u
#define AX_PKG_MAGIC_AXL 0x314C5841u

/* Tipo de payload de un .axe. */
#define AX_PKG_KIND_ELF64 1u

/* Tipo de payload de un .axl. */
#define AX_PKG_KIND_AR 1u

/* Banderas de un .axe. */
#define AX_PKG_FLAG_PIE     (1u << 0)
#define AX_PKG_FLAG_STATIC  (1u << 1)

#if defined(__GNUC__)
#  define AX_PKG_PACKED __attribute__((packed))
#else
#  define AX_PKG_PACKED
#endif

/* Cabecera de .axe: 68 bytes. */
typedef struct AX_PKG_PACKED {
    uint32_t magic;         /* AX_PKG_MAGIC_AXE */
    uint16_t version;       /* AX_PKG_VERSION */
    uint16_t kind;          /* AX_PKG_KIND_ELF64 */
    uint32_t flags;         /* AX_PKG_FLAG_* */
    uint32_t payload_size;  /* bytes que siguen a esta cabecera */
    uint32_t payload_crc32; /* CRC-32 (IEEE) del payload */
    uint32_t entry_hint;    /* e_entry del ELF (informativo) */
    uint32_t load_hint;     /* e_vaddr base del ELF (informativo) */
    char     name[32];      /* nombre logico, p. ej. "hello" */
    char     arch[8];       /* "x86_64" */
} ax_axe_header;

/* Cabecera de .axl: 60 bytes. */
typedef struct AX_PKG_PACKED {
    uint32_t magic;         /* AX_PKG_MAGIC_AXL */
    uint16_t version;       /* AX_PKG_VERSION */
    uint16_t kind;          /* AX_PKG_KIND_AR */
    uint32_t payload_size;  /* bytes del archivo ar */
    uint32_t payload_crc32; /* CRC-32 (IEEE) del payload */
    uint32_t members;       /* numero de miembros del archivo ar */
    char     name[32];      /* nombre logico, p. ej. "libaxos" */
    char     arch[8];       /* "x86_64" */
} ax_axl_header;

/* CRC-32 IEEE 802.3 (polinomio 0xEDB88320), sin tablas. */
static inline uint32_t ax_pkg_crc32(const void* data, uint32_t size)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

#endif /* AX_PKG_H */
