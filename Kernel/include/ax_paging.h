/*
 * AxolynOS - Sistema de paginacion por demanda con intercambio (swap).
 *
 * El VMM (Kernel/src/vmm.cpp) mapea la region de user mode en identidad y el
 * PMM la reserva entera.  Este modulo anade la capa de paginacion que falta
 * para que las aplicaciones vivan en un verdadero sistema de memoria virtual:
 *
 *   - Las paginas de user mode se marcan como libres al liberar la ranura de
 *     un proceso y se mapean por demanda (a cero) cuando se tocan.
 *   - Una pagina mapeada se puede evacuar ("swap out"): su contenido se
 *     escribe en un fichero de intercambio y la entrada de tabla se borra.
 *   - El manejador de #PF del kernel lee la pagina de vuelta ("swap in") cuando
 *     la aplicacion la toca de nuevo.
 *
 * Como el mapa es de identidad, la pagina fisica de una direccion virtual de
 * user mode es la propia direccion: evacuar y recuperar no mueve marcos, solo
 * copia contenidos entre RAM y el fichero de intercambio.
 *
 * Documentacion: manuales/14-paginacion.md
 */
#ifndef AX_PAGING_H
#define AX_PAGING_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ruta por defecto del fichero de intercambio en AxFS-D. */
#define AX_PAGING_SWAP_PATH     "/system/pagefile.axswap"
#define AX_PAGING_SWAP_PATH_LEN 28

/* Tamano por defecto del fichero de intercambio (8 MiB = 2048 paginas). */
#define AX_PAGING_SWAP_SIZE     (8ull * 1024ull * 1024ull)

/* Maximo de ranuras de intercambio que se rastrean (16 MiB). */
#define AX_PAGING_SWAP_MAX      4096

/* Estado de una pagina de user mode. */
#define AX_PG_FREE     0   /* no mapeada: se sirve a cero por demanda */
#define AX_PG_MAPPED   1   /* mapeada y presente */
#define AX_PG_SWAPPED  2   /* evacuada al fichero de intercambio */

/* Numero de paginas de la region de user mode (128 MiB / 4 KiB). */
#define AX_USER_REGION_PAGES \
    ((AX_USER_REGION_END - AX_USER_VIRT_BASE) / AX_PAGE_SIZE)

/* Inicializa el estado de las paginas y el fichero de intercambio. */
void ax_paging_init(void);

/*
 * Prepara un rango de user mode para poder escribir en el: mapea a cero las
 * paginas libres y recupera las evacuadas.  Lo usan el cargador ELF y la
 * preparacion de la pila.  Devuelve 0 o -errno.
 */
int  ax_paging_touch(uint64_t virt, uint64_t size);

/*
 * Manejador de fallo de pagina.  Devuelve true si el fallo se resolvio (la
 * pagina queda mapeada y se puede reintentar la instruccion).
 */
bool ax_paging_on_fault(uint64_t addr, uint64_t error);

/* Evaca una pagina concreta al fichero de intercambio. */
int  ax_paging_evict_page(uint64_t virt);

/* Evacua todas las paginas de una ranura de user mode (16 MiB). */
int  ax_paging_evict_slot(uint32_t slot);

/* Libera las paginas de una ranura: las desmapea y recupera el swap. */
int  ax_paging_free_slot(uint32_t slot);

/* Evacua todas las paginas de user mode. */
int  ax_paging_evict_all(void);

/* Activa/desactiva el intercambio.  "path" puede ser 0 para la ruta por
   defecto.  Si el fichero no existe se crea con el tamano por defecto. */
int  ax_paging_swap_enable(const char* path);
int  ax_paging_swap_disable(void);

/* Consulta de estado. */
bool ax_paging_swap_is_enabled(void);
int  ax_paging_fill_info(AxPagingInfo* out);
uint64_t ax_paging_user_page_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_PAGING_H */
