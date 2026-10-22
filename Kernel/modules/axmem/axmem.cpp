/*
 * AxolynOS - Modulo de ejemplo "axmem".
 *
 * Un modulo es codigo que se carga en el kernel en tiempo de ejecucion.
 * No puede llamar directamente a funciones del kernel (no hay
 * relocaciones): todo lo que necesita lo recibe en un AxModuleContext
 * en el momento de la carga.
 *
 * Manual: manuales/05-modulos.md
 */
#include "ax_module.h"

/* Los modulos usan numeros de syscall a partir de 100 (los del kernel
 * ocupan 0..27 segun apis/include/axos/abi.h). */
#define AXMOD_AXMEM_QUERY 100
#define AXMOD_AXMEM_FILL  101
#define AXMOD_AXMEM_INFO  102

static uint8_t  g_pattern[256];
static uint64_t g_calls;
static uint64_t g_bytes;

/* Devuelve el numero de llamadas atendidas. */
static int axmod_query(uint64_t* args, uint64_t* ret)
{
    (void)args;
    g_calls++;
    *ret = g_calls;
    return 0;
}

/* Rellena el patron interno con un byte y devuelve cuantos hay escritos. */
static int axmod_fill(uint64_t* args, uint64_t* ret)
{
    uint8_t value = (uint8_t)(args[0] & 0xFF);
    for (uint32_t i = 0; i < sizeof(g_pattern); ++i) {
        g_pattern[i] = value;
    }
    g_bytes = sizeof(g_pattern);
    *ret = g_bytes;
    return 0;
}

/* Devuelve informacion del modulo: llamadas, bytes y version del ABI. */
static int axmod_info(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = (g_calls << 32) | (g_bytes & 0xFFFFFFFFull);
    return 0;
}

/*
 * Punto de entrada del modulo. La firma la fija el cargador:
 * recibe el contexto de servicios y devuelve 0 si todo fue bien.
 */
extern "C" int _ax_module_entry(AxModuleContext* ctx)
{
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;
    }

    ctx->print("[axmem] modulo 'axmem' cargado en el kernel\n");
    ctx->print("[axmem] registrando syscalls 100, 101 y 102\n");

    ctx->register_syscall(AXMOD_AXMEM_QUERY, axmod_query);
    ctx->register_syscall(AXMOD_AXMEM_FILL,  axmod_fill);
    ctx->register_syscall(AXMOD_AXMEM_INFO,  axmod_info);

    ctx->publish("axmem.query", (void*)axmod_query);
    ctx->publish("axmem.fill",  (void*)axmod_fill);

    return 0;
}
