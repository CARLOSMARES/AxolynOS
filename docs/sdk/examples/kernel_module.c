/* Ejemplo: Módulo kernel basico */
/* Este archivo va en Kernel/modules/mi_modulo/mi_modulo.c */

#include "ax_module.h"

/* Numero de syscall personalizado (>= 100) */
#define AXMOD_MI_MODULO_SALUDO 110

/* Funcion de syscall personalizada */
static int mi_modulo_saludo(uint64_t* args, uint64_t* ret) {
    /* args no usado en este ejemplo */
    (void)args;
    *ret = (uint64_t)"hola desde el modulo";  /* puntero al string */
    return 0;
}

/* Punto de entrada obligatorio para modulos kernel */
extern "C" int _ax_module_entry(AxModuleContext* ctx) {
    /* Verificar contexto valido */
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;  /* Error: contexto invalido o version incompatible */
    }
    
    /* Imprimir mensaje en la consola del kernel */
    ctx->print("[modulo] modulo 'mi_modulo' cargado en el kernel\n");
    
    /* Imprimir mensaje adicional */
    ctx->print("Hola desde el modulo kernel AXolynOS\n");
    
    /* Registrar una syscall personalizada para aplicaciones user mode */
    ctx->register_syscall(AXMOD_MI_MODULO_SALUDO, mi_modulo_saludo);
    
    /* Publicar un simbolo que otros modulos o aplicaciones pueden buscar */
    ctx->publish("modulo.hola", (void*)mi_modulo_saludo);
    
    return 0;  /* 0 = modulo cargado exitosamente */
}