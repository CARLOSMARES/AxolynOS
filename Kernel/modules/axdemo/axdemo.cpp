/*
 * AxolynOS - Modulo de ejemplo "axdemo".
 *
 * Muestra como un modulo puede:
 *   - usar los servicios del contexto (print, alloc, register_syscall)
 *   - publicar simbolos que otros modulos pueden buscar
 *   - registrar syscalls propias que las aplicaciones pueden invocar
 *
 * Manual: manuales/05-modulos.md
 */
#include "ax_module.h"

#define AXMOD_AXDEMO_HELLO 110
#define AXMOD_AXDEMO_ALLOC 111

static const char g_greeting[] = "hola desde el modulo axdemo\n";

/* El contexto se guarda durante toda la vida del modulo. */
static AxModuleContext* g_ctx;
static void* g_buffer;

static void* ctx_alloc(uint64_t size)
{
    if (g_ctx == 0) {
        return 0;
    }
    return g_ctx->alloc(size);
}

static int axdemo_hello(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = (uint64_t)(uintptr_t)g_greeting;
    return 0;
}

static int axdemo_alloc(uint64_t* args, uint64_t* ret)
{
    uint64_t size = args[0];
    if (size == 0 || size > (1u << 20)) {
        return AX_EINVAL;
    }
    g_buffer = ctx_alloc(size);
    if (g_buffer == 0) {
        return AX_ENOMEM;
    }
    *ret = (uint64_t)(uintptr_t)g_buffer;
    return 0;
}

extern "C" int _ax_module_entry(AxModuleContext* ctx)
{
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;
    }
    g_ctx = ctx;

    ctx->print("[axdemo] modulo 'axdemo' cargado\n");
    ctx->print(g_greeting);

    ctx->register_syscall(AXMOD_AXDEMO_HELLO, axdemo_hello);
    ctx->register_syscall(AXMOD_AXDEMO_ALLOC, axdemo_alloc);
    ctx->publish("axdemo.hello", (void*)axdemo_hello);

    return 0;
}
