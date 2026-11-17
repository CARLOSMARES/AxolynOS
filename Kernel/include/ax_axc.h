/*
 * AxolynOS - Configuracion de aplicaciones (.axc).
 *
 * Un ".axc" describe una aplicacion: nombre, version, publicador, ejecutable,
 * icono y las conexiones que tiene con otras aplicaciones.  Es el equivalente
 * a los manifiestos de aplicacion de otros sistemas.
 *
 * El formato es texto plano con dos secciones:
 *
 *   [app]
 *   name        = calc
 *   version     = 1.2.0
 *   publisher   = AxolynOS Project
 *   exec        = /apps/calc.axe
 *   icon        = /system/icons/calc.ico
 *   description = Calculadora
 *
 *   [connections]
 *   run     = editor
 *   open    = files
 *   depends = libaxos
 *
 * La primera seccion es obligatoria y debe tener "name".  "version" y
 * "publisher" son opcionales pero recomendados.  Las conexiones son pares
 * "tipo = aplicacion destino".
 *
 * Documentacion: manuales/17-axc.md
 */
#ifndef AX_AXC_H
#define AX_AXC_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_AXC_MAX_APPS     32
#define AX_AXC_MAX_CONNS    8

/* Inicializa el registro de aplicaciones. */
void ax_axc_init(void);

/*
 * Carga un fichero .axc (initrd o AxFS-D) y lo anade al registro.
 * Devuelve el indice (>= 0) o -errno.
 */
int  ax_axc_load(const char* path);

/* Descarga una aplicacion del registro por indice. */
int  ax_axc_unload(uint32_t index);

int  ax_axc_count(void);
int  ax_axc_info(uint32_t index, AxcInfo* out);
int  ax_axc_conn(uint32_t index, uint32_t conn_index, AxcConnection* out);

/* Busca una aplicacion por nombre (no distingue mayusculas). */
int  ax_axc_find(const char* name);

/* Busca una conexion concreta de una aplicacion ("run", "open", ...). */
int  ax_axc_connection(const char* name, const char* key,
                       char* out, uint64_t max);

/* Lista el registro en texto (lineas "nombre  version  publisher"). */
int  ax_axc_list(char* out, uint64_t max);

/* Carga automaticamente los ficheros .axc de /config en el initrd. */
void ax_axc_autoload(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_AXC_H */
