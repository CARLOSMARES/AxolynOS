/*
 * AxolynOS - Editores de texto (edit, vi, vim, nvim).
 *
 * Los cuatro comparten un motor de buffer y un renderizador ANSI/VGA:
 *
 *   edit   editor de pantalla completa tipo Notepad (Ctrl+S / Ctrl+Q)
 *   vi     editor modal clasico (normal / insercion / comando)
 *   vim    vi con un subconjunto extendido (gg, G, numeros de linea, undo)
 *   nvim   el mismo motor que vim, con nombre y estado propios
 *
 * El texto se carga y guarda en AxFS-D.  El buffer maximo es de 64 KiB, que
 * es suficiente para editar los ficheros de configuracion y las notas de un
 * sistema basico.  No hay dependencias de libc.
 */
#ifndef AX_EDITOR_H
#define AX_EDITOR_H

#include "ax_types.h"

#define AX_EDITOR_MODE_EDIT 0
#define AX_EDITOR_MODE_VI   1
#define AX_EDITOR_MODE_VIM  2
#define AX_EDITOR_MODE_NVIM 3

/* cwd se usa para mostrar rutas; path ya viene resuelto por la shell. */
int ax_editor_run(const char* cwd, const char* path, int kind);

#endif /* AX_EDITOR_H */
