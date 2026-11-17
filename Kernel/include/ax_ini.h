/*
 * AxolynOS - Analizador de ficheros INI (estilo Windows).
 *
 * Soporta secciones [nombre], claves clave=valor, comentarios con ';' o '#',
 * claves entre comillas y busquedas que no distinguen mayusculas (igual que
 * el INI de Windows).
 *
 * Ademas incluye el tratamiento especial de "desktop.ini", el fichero que
 * Windows usa para personalizar la apariencia de una carpeta (nombre para
 * mostrar, icono, informacion emergente).
 *
 * Documentacion: manuales/16-ini.md
 */
#ifndef AX_INI_H
#define AX_INI_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_INI_MAX_SECTIONS 16
#define AX_INI_MAX_ENTRIES  96
#define AX_INI_NAME_MAX     32
#define AX_INI_VALUE_MAX    128
#define AX_INI_BUFFER_SIZE  8192

/* Analiza un texto INI en memoria.  Devuelve el numero de entradas o -errno. */
int  ax_ini_parse(const char* text, uint64_t size);

/* Carga y analiza un fichero INI (VFS o AxFS-D). */
int  ax_ini_load(const char* path);

/* Guarda la tabla actual como fichero INI en AxFS-D. */
int  ax_ini_save(const char* path);

/* Tabla en memoria. */
int  ax_ini_clear(void);
int  ax_ini_count(void);
int  ax_ini_enum(uint32_t index, AxIniEntry* out);
int  ax_ini_get(const char* section, const char* key, char* out, uint64_t max);
int  ax_ini_set(const char* section, const char* key, const char* value);
int  ax_ini_get_int(const char* section, const char* key, int def);
bool ax_ini_get_bool(const char* section, const char* key, bool def);

/* ------------------------------------------------------------------ */
/* desktop.ini: metadatos de una carpeta                               */
/* ------------------------------------------------------------------ */
typedef struct {
    bool valid;
    char display_name[64];
    char icon_file[128];
    char info_tip[256];
    int  icon_index;
} AxIniDesktop;

/*
 * Carga "<dir>/desktop.ini" y extrae la seccion [.ShellClassInfo].
 * "dir" es una ruta normalizada ( "/" para la raiz).  Si no existe el
 * fichero o no tiene esa seccion, "out->valid" queda en false.
 */
int  ax_ini_load_desktop(const char* dir, AxIniDesktop* out);

#ifdef __cplusplus
}
#endif

#endif /* AX_INI_H */
