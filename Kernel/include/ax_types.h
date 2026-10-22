/*
 * AxolynOS - Tipos basicos y utilidades del kernel.
 */
#ifndef AX_TYPES_H
#define AX_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include "ax_config.h"
#include "axos/abi.h"

typedef uint64_t ax_paddr_t;   /* direccion fisica */
typedef uint64_t ax_vaddr_t;   /* direccion virtual */

#define AX_NULL ((void*)0)

#define AX_UNUSED(x) ((void)(x))

#define AX_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define AX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define AX_MAX(a, b) ((a) > (b) ? (a) : (b))
#define AX_ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((uint64_t)(a) - 1))
#define AX_ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))
#define AX_IS_ALIGNED(x, a) (((x) & ((uint64_t)(a) - 1)) == 0)

#define AX_STRINGIFY_(x) #x
#define AX_STRINGIFY(x)  AX_STRINGIFY_(x)

/* Entrada de la tabla E820 entregada por el firmware. */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = usable */
    uint32_t reserved;
} ax_e820_entry;

#define AX_E820_USABLE 1

/* Bandas de paginacion */
#define AX_PAGE_PRESENT      (1ull << 0)
#define AX_PAGE_WRITE        (1ull << 1)
#define AX_PAGE_USER         (1ull << 2)
#define AX_PAGE_WRITE_THROUGH (1ull << 3)
#define AX_PAGE_CACHE_DISABLE (1ull << 4)

/* ------------------------------------------------------------------ */
/* Panico / aserciones                                                 */
/* ------------------------------------------------------------------ */
[[noreturn]] void ax_panic(const char* file, int line, const char* fmt, ...);
[[noreturn]] void ax_panic_regs(const char* what);

#define AX_ASSERT(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ax_panic(__FILE__, __LINE__, "asercion fallida: %s", #cond);       \
        }                                                                      \
    } while (0)

#define AX_ASSERT_MSG(cond, ...)                                               \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ax_panic(__FILE__, __LINE__, __VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#define AX_PANIC(...) ax_panic(__FILE__, __LINE__, __VA_ARGS__)

#endif /* AX_TYPES_H */
