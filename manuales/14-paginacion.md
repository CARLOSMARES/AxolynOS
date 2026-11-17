# Sistema de paginacion

AxolynOS tiene dos capas de memoria virtual:

- **VMM** (`Kernel/src/vmm.cpp`): las tablas de 4 niveles, el mapa de
  identidad del kernel y de la region de user mode, y el mapeo/desmapeo de
  paginas.
- **Paginacion por demanda** (`Kernel/src/paging.cpp`): decide cuando una
  pagina de user mode esta en RAM, cuando se mapea a cero y cuando se
  evacua al fichero de intercambio.

El resultado es un sistema de memoria virtual con **demanda por fallo** y
**swap en disco**, parecido al `pagefile.sys` de Windows.

---

## 1. La region de user mode

Cada aplicacion ocupa una ranura de 16 MiB dentro de la region de user mode
(`AX_USER_VIRT_BASE .. AX_USER_REGION_END`, 64 MiB a 192 MiB). La region
entera (128 MiB) esta reservada en el PMM y mapeada en identidad, asi que la
direccion virtual de una pagina coincide con su direccion fisica.

El gestor de paginacion lleva una tabla con **32768 paginas** de 4 KiB y tres
estados posibles:

| Estado      | Significado                                              |
|-------------|---------------------------------------------------------|
| `FREE`      | No mapeada. Se sirve a cero cuando se toca.             |
| `MAPPED`    | Presente en RAM y accesible.                            |
| `SWAPPED`   | Evacuada al fichero de intercambio y desmapeada.        |

### Ciclo de vida

```text
           touch / #PF                 evict (swap out)
FREE ---------------------> MAPPED <--------------------> SWAPPED
 ^                            |                             |
 |                            |                             |
 +---------------------------( swap in, #PF )---------------+
```

- Al crear un proceso, su ranura se prepara con `ax_paging_free_slot()`: las
  paginas se desmapean y pasan a `FREE`.
- El cargador ELF llama a `ax_paging_touch()` antes de copiar cada segmento,
  y la preparacion de la pila hace lo mismo con la pila. Las paginas pasan a
  `MAPPED`.
- Cuando una pagina que esta en `FREE` se toca sin estar mapeada, se produce
  un **fallo de pagina** (#PF) y el manejador del kernel la mapea a cero.
- `swapout` escribe la pagina en el fichero de intercambio y la desmapea
  (`SWAPPED`). El siguiente acceso provoca un #PF que la recupera de disco.

---

## 2. El fichero de intercambio

El fichero de intercambio es `/system/pagefile.axswap` en el volumen AxFS-D.
Se crea automaticamente al arrancar el servicio `paging` si el disco esta
montado, con un tamano de **8 MiB** (2048 paginas de 4 KiB).

- Si no hay disco montado, el sistema sigue funcionando con la paginacion por
  demanda a cero, pero no se puede evacuar (`swapout` devuelve error).
- El tamano maximo que se rastrea es de 16 MiB (4096 ranuras).

### Ordenes de la shell

| Orden                        | Descripcion                                  |
|------------------------------|----------------------------------------------|
| `pageinfo`                   | Estado de la paginacion y del swap.          |
| `swap`                       | Estado del intercambio.                      |
| `swap on`                    | Activa el intercambio (crea el fichero).     |
| `swap off`                   | Desactiva la evacuacion.                     |
| `swapout <direccion>`        | Evaca una pagina concreta (hexadecimal).     |
| `swapout all`                | Evacua todas las paginas de user mode.       |

Ejemplo:

```text
axolyn:/> pageinfo
paginacion de user mode:
  paginas totales : 32768
  mapeadas        : 42
  libres (cero)   : 32726
  evacuadas       : 0
  fallos de pag.  : 42
  swap in/out     : 0 / 0
  intercambio     : inactivo (0 de 0 ranuras, 0 bytes)

axolyn:/> swap on
[paging] intercambio activo: /system/pagefile.axswap (2048 ranuras de 4096 bytes)
intercambio activado

axolyn:/> swapout 0x40000000
pagina 0x40000000 evacuada

axolyn:/> pageinfo
  ...
  evacuadas       : 1
  swap in/out     : 0 / 1
  intercambio     : activo (1 de 2048 ranuras, 8388608 bytes)
```

---

## 3. Paginacion de salida en la shell

Ademas de la memoria virtual, la shell incluye un **paginador de texto** para
que los listados largos no se desplacen fuera de la pantalla.

| Orden                | Descripcion                                    |
|----------------------|------------------------------------------------|
| `more <fichero>`     | Muestra un fichero por pantallas.              |
| `type <fich> /p`     | Igual que `type` pero paginando.               |
| `dir [ruta] /p`      | Lista un directorio paginando las entradas.    |

Tras cada pantalla (23 lineas, `AX_VGA_HEIGHT - 2`) se muestra:

```text
-- mas -- [espacio] continuar, [q] salir
```

- **Espacio** (o cualquier otra tecla): sigue con la siguiente pantalla.
- **q**: abandona la salida.

---

## 4. Syscalls

Las syscalls de paginacion son las numeros **51 a 54** (ver
`apis/include/axos/abi.h`):

| Num | Nombre             | Argumentos                        | Devuelve        |
|-----|--------------------|-----------------------------------|-----------------|
| 51  | `AX_SYS_PAGEINFO`  | `rdi=AxPagingInfo*`               | 0               |
| 52  | `AX_SYS_SWAP_OUT`  | `rdi=direccion de user mode`      | 0 o -errno      |
| 53  | `AX_SYS_SWAP_ENABLE` | `rdi=ruta (0 = por defecto)`    | 0 o -errno      |
| 54  | `AX_SYS_SWAP_DISABLE` | ninguno                         | 0               |

### Estructura `AxPagingInfo`

```c
typedef struct {
    ax_u64 total_pages;     /* paginas de la region de user mode   */
    ax_u64 mapped_pages;    /* presentes en RAM                     */
    ax_u64 free_pages;      /* no mapeadas (demanda cero)           */
    ax_u64 swapped_pages;   /* evacuadas al fichero de intercambio  */
    ax_u64 page_faults;     /* fallos de pagina atendidos           */
    ax_u64 swap_ins;        /* paginas recuperadas                  */
    ax_u64 swap_outs;       /* paginas evacuadas                    */
    ax_u64 swap_enabled;    /* 1 si se permite evacuar              */
    ax_u64 swap_slots;      /* ranuras totales del fichero          */
    ax_u64 swap_used;       /* ranuras ocupadas                     */
    ax_u64 swap_bytes;      /* tamano del fichero de intercambio    */
} AxPagingInfo;
```

### Ejemplo en C

```c
#include <axos/axos.hpp>

int main(void)
{
    axos::PagingInfo p = axos::paging_info();
    axos::println("paginas mapeadas: ");
    axos::println_uint(p.mapped_pages);

    axos::swap_enable();                 /* usa /system/pagefile.axswap */
    axos::swap_out(0x40000000);          /* evacua una pagina */
    axos::swap_disable();
    return 0;
}
```

Tablas de paginas y direcciones de usuario son unicamente del kernel: las
aplicaciones no pueden mapear paginas manualmente.

---

## 5. Integracion con el kernel

### Manejador de fallos de pagina

`Kernel/src/isr.cpp` intercepta el vector 14 (`.AX_EXC_PGFAULT`). Si el fallo
es de presencia (`P=0`) y la direccion esta dentro de la region de user
mode, llama a `ax_paging_on_fault()`:

- Si la pagina esta en `SWAPPED`, la lee del fichero y la mapea.
- Si la pagina esta en `FREE`, la mapea y la pone a cero.

Si no se puede resolver, el kernel entra en panico como antes.

### Cargador ELF y pila

`Kernel/src/elf.cpp` y `Kernel/src/proc.cpp` llaman a `ax_paging_touch()`
antes de escribir en una pagina de user mode, de modo que el sistema puede
arrancar las aplicaciones con las paginas sin mapear.

### Integridad del mapa

El VMM usa paginas de 2 MiB para la region de user mode. Para poder
desmapear paginas de 4 KiB individualmente, `ax_vmm_map()` y `ax_vmm_unmap()`
dividen automaticamente la pagina grande en una tabla de 512 entradas de
4 KiB (`ax_vmm_split_2m()`), conservando los permisos.

---

## 6. Depuracion

| Sintoma                                        | Causa probable                              |
|------------------------------------------------|---------------------------------------------|
| `swapout` devuelve error                       | El disco no esta montado o `swap off`.      |
| La aplicacion se bloquea al tocar memoria      | No se llamo a `ax_paging_touch()`.          |
| Fallo de pagina en kernel                      | Se accedio a una pagina de user mode libre. |
| `pageinfo` muestra muchas paginas libres      | Es normal: se mapean por demanda.           |
