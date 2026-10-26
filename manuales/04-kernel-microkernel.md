# El microkernel

El microkernel es el nucleo de AxolynOS: lo minimo imprescindible para
que el sistema sea un sistema. Todo lo demas (ficheros, procesos de
usuario, utilidades) se implementa encima, como modulos o como
aplicaciones.

```
Kernel/src/
  boot.S        arranque: pila, BSS, salto a kmain
  kernel.cpp    orden de inicializacion de los subsistemas
  gdt.cpp       GDT (anillo 0 y anillo 3) + TSS
  idt.cpp       IDT con 48 vectores
  isr_stubs.S   trampolines (excepciones, IRQ y syscall)
  isr.cpp       volcado de registros y panicos
  pic.cpp       PIC 8259
  timer.cpp     PIT a 100 Hz
  pmm.cpp       bitmap de memoria fisica
  vmm.cpp       paginacion de 4 niveles
  heap.cpp      heap del kernel
  sched.cpp     planificador e hilos
  context.S     cambio de contexto
  syscall.cpp   tabla de llamadas al sistema
  ipc.cpp       puertos de mensajes
  elf.cpp       cargador ELF64 de user mode
  proc.cpp      procesos de user mode
  module.cpp    carga de modulos
  vmhost.cpp    integracion de la VM de bytecode
  vfs.cpp       lector del initrd (AXFS)
  shell.cpp     interprete de ordenes del kernel
```

## 1. Arranque (`boot.S` + `kernel.cpp`)

`_start` recibe en `RDI` la direccion fisica del `AxBootInfo`, copia el
BSS a cero, prepara una pila de kernel y llama a `kmain()`.

`ax_kernel_init()` ordena el arranque:

```
consola y serie
  -> banner
  -> PMM (mapa E820 + reserva de la region del kernel)
  -> VMM (tablas de pagina propias)
  -> heap
  -> GDT + TSS
  -> IDT + trampolines
  -> PIC + timer
  -> planificador, syscalls, IPC
  -> VFS (initrd)
  -> modulos (carga automatica)
  -> user mode listo
  -> shell del kernel
```

El orden importa: no se puede asignar memoria antes del PMM, ni hacer
llamadas al sistema antes de instalar la IDT, ni crear hilos antes del
planificador.

## 2. CPU

### GDT (`gdt.cpp`)

| Selector | Segmento | Anillo |
|---|---|---|
| `0x08` | codigo del kernel | 0 |
| `0x10` | datos del kernel | 0 |
| `0x18` | codigo de usuario | 3 |
| `0x20` | datos de usuario | 3 |
| `0x28` | TSS | 0 |

Los selectores de usuario incluyen el RPL (0x1B y 0x23). La TSS fija
`rsp0`, la pila a la que se salta cuando una interrupcion llega desde
user mode.

### IDT (`idt.cpp`)

48 vectores: 32 excepciones de la CPU y 16 lineas de IRQ. La puerta del
vector `0x80` (syscall) se instala con **DPL 3**, que es lo que permite
que el codigo de usuario ejecute `int 0x80`.

### Excepciones (`isr.cpp`)

Cada excepcion imprime un volcado de registros y el contenido de `CR2`
(direccion que provoco el fallo de pagina) y entra en panic.

## 3. Memoria

### PMM (`pmm.cpp`)

Gestor de frames de 4 KiB con un bitmap. Al inicializarse:

1. Recorre la tabla E820 que paso el bootloader y marca como usables los
   tramos de tipo 1.
2. Reserva la region del kernel (`0x200000 - 0x2200000`), el initrd, las
   tablas de pagina y el propio bitmap.
3. Marca como no usables las zonas de firmware.

### VMM (`vmm.cpp`)

Paginacion de 4 niveles (`PML4 -> PDPT -> PD -> PT`):

* La ventana del kernel (`0xFFFFFFFF80200000`, 32 MiB) se mapea sobre su
  direccion fisica.
* El rango bajo se mapea en identidad con paginas de 4 KiB marcadas como
  de usuario, salvo las regiones reservadas (supervisor).

`ax_vmm_translate()` recorre la tabla y devuelve la direccion fisica de
una virtual; es la base del depurador y de la proteccion de paginas.

### Heap (`heap.cpp`)

Heap del kernel sobre la region reservada: bloques con cabecera de
tamano, lista de libres y fusion de bloques contiguos. `ax_malloc` es la
unica forma de pedir memoria en el kernel (los modulos tambien usan el
heap a traves de su contexto).

## 4. Hilos y planificador (`sched.cpp`, `context.S`)

Estados: `FREE`, `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE`.

* El planificador es round-robin: cada tick del timer (100 Hz) elige el
  siguiente hilo listo.
* `ax_thread_block()` saca al hilo de la cola de listos; `ax_thread_wakeup()`
  lo devuelve.
* `ax_thread_sleep(ms)` implementa la espera con marca de tiempo.

El cambio de contexto (`ax_context_switch`) guarda `r15..rbp` y el `rsp`
en el contexto del hilo saliente y los restaura del contexto del
entrante. Para volver a user mode, el marco de `iretq` preparado en la
pila del kernel permite regresar al anillo 3 con `cs=0x1B` y `ss=0x23`.

Cada cambio de contexto actualiza `ax_kernel_stack_top`, la variable que
usa el trampolin de syscall para saber que pila del kernel usar.

## 5. Llamadas al sistema (`syscall.cpp`)

Tabla de 28 entradas (`AX_SYS_*` en `apis/include/axos/abi.h`). Cada
entrada es una funcion con la firma:

```c
int handler(uint64_t* args, uint64_t* ret);
```

`args[0..5]` son `RDI, RSI, RDX, R10, R8, R9`; el valor de retorno se
escribe en `*ret`. Un valor negativo significa error (`AX_E*`, que ya
estan definidos como negativos en `apis/include/axos/abi.h`).

Los modulos pueden registrar syscalls adicionales en la misma tabla
(numeros a partir del 100).

## 6. Procesos y user mode (`proc.cpp`, `elf.cpp`)

`ax_proc_spawn()`:

1. Busca el binario en el initrd (`/apps/<nombre>.axe`, `.elf` o `.axb`).
2. Si el fichero es un `.axe`, comprueba la cabecera `AXE1`, el tamano y
   el CRC32 (`ax_pkg.h`) y se queda con el ELF64 que lleva dentro.
3. Reserva una ranura de user mode de 16 MiB en
   `AX_USER_VIRT_BASE (0x4000000) + ranura*16 MiB`; hay 8 ranuras y la
   region completa (128 MiB) se reserva en el PMM.
4. Carga el ELF en la ranura aplicando las relocaciones
   `R_X86_64_RELATIVE` y prepara una pila de 1 MiB en la parte alta de la
   ranura.
5. Escribe `argc`/`argv` en la pila y crea el hilo de user mode con el
   punto de entrada del ELF.

`ax_proc_wait(pid)` bloquea al llamador (`ax_thread_sleep`) hasta que el
hilo pasa a `ZOMBIE`, libera su ranura y reapea el hilo para que no queden
procesos muertos ocupando la tabla. El shell usa este modo sincrono en
`run`, de forma que la ranura 0 se reutiliza en ejecuciones sucesivas.

El usuario no puede tocar el kernel: el rango alto de direcciones esta
mapeado sin el bit de usuario, asi que cualquier acceso desde ring 3
provoca un fallo de pagina.

## 7. IPC (`ipc.cpp`)

Puertos de mensajes numerados (0..31). `ax_ipc_send()` encola un
mensaje (hasta 4 KiB) y despierta al receptor; `ax_ipc_recv()` bloquea al
hilo hasta que haya mensaje.

## 8. Modulos (`module.cpp`)

Ver [05-modulos.md](05-modulos.md). Resumen:

* El initrd puede contener ficheros `.axmod`.
* Cada modulo ocupa una ranura de 1 MiB en
  `AX_MODULES_VIRT_BASE + ranura*1 MiB`.
* Se le pasa un `AxModuleContext` con los servicios del kernel.
* Puede registrar syscalls y publicar simbolos.

## 9. Maquina virtual (`vmhost.cpp`)

El kernel crea una VM por programa Axolyn y la ejecuta en un hilo propio.
Las natives de la VM hacen de puente con las syscalls del kernel, de modo
que un programa Axolyn puede usar el sistema de ficheros, lanzar procesos
o consultar el reloj.

## 10. VFS (`vfs.cpp`)

El initrd es una imagen **AXFS** en memoria:

```
AxfsHeader { magic "AXFS0001", count, index_off, data_off, total_size }
AxfsEntry[] { name[64], type, flags, offset, size, parent }
blob de datos
```

`ax_vfs_read("/apps/hello.axb", &size)` devuelve un puntero a los bytes.
El sistema es de **solo lectura**: no hay escritura en disco todavia.

## 11. Shell (`shell.cpp`)

Un REPL que lee de la consola serie y ejecuta ordenes (`help`, `apps`,
`run`, `ax`, `mods`, `mem`, `threads`, `vm`, `uptime`, `reboot`). Es la
forma mas rapida de comprobar el estado del sistema.
