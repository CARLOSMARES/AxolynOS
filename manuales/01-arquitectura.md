# Arquitectura de AxolynOS

## 1. Vision general

AxolynOS es un sistema operativo x86-64 de 64 bits con un **microkernel**
escrito en C++ y ensamblador. La idea central es separar el nucleo de todo
lo que puede crecer:

```
   +------------------------------------------------------+
   |  Aplicaciones nativas (C++)     Aplicaciones Axolyn  |   user mode / VM
   +----------------+-------------------------+-----------+
                    |  syscalls (int 0x80)     | bytecode
                    v                          v
   +------------------------------------------------------+
   |  libaxos (apis/)                VM (Kernel/vm/)      |   ring 0
   +-------------------------+----------------------------+
   |                    MICROKERNEL                        |
   |  planificador | memoria | procesos | IPC | modulos   |
   +-------------------------+----------------------------+
                    |  servicios (AxModuleContext)
                    v
   +------------------------------------------------------+
   |  Modulos del kernel (.axmod)                          |
   +------------------------------------------------------+
```

* El **microkernel** se ocupa solo de lo imprescindible: CPU, memoria,
  hilos, llamadas al sistema, IPC y carga de modulos.
* La **logica de usuario** (ficheros, procesos, utilidades) vive en
  modulos y en las aplicaciones.
* El **compilador propio** (`axc`) traduce el lenguaje Axolyn a bytecode
  que ejecuta la VM que vive dentro del kernel.

## 2. Mapa de memoria

### 2.1 Fisico

| Rango | Uso |
|---|---|
| `0x00000000 - 0x000FFFFF` | BIOS, EBDA, tablas de paginas del arranque |
| `0x00100000 - 0x001FFFFF` | zona libre entre el area del BIOS y el kernel |
| `0x00200000 - 0x021FFFFF` | region reservada del kernel (32 MiB) |
| `0x00A00000` | tablas de paginas (kernel + mapa bajo) |
| `0x00E00000` | bitmap del gestor de memoria fisica |
| `0x00F00000` | heap del kernel (8 MiB) |
| `0x01700000` | area de staging para modulos (8 MiB) |
| `0x01F00000` | initramfs (imagen AXFS) |
| `0x04000000 - 0x0BFFFFFF` | region de user mode (8 ranuras de 16 MiB) |
| resto | disponible para el PMM |

### 2.2 Virtual

| Rango | Uso | Anillo |
|---|---|---|
| `0x0000000004000000` | base de las aplicaciones (8 ranuras de 16 MiB) | 3 |
| `0x000000000C000000` | fin de la region de user mode (192 MiB) | - |
| `0xFFFFFFFF80200000` | imagen del kernel | 0 |
| `0xFFFFFFFF82200000` | ventana de modulos (16 ranuras de 1 MiB) | 0 |
| `0xFFFFFFFFA0000000` | sombra de las tablas de paginas de user mode | 0 |

El sistema usa **un unico espacio de direcciones** con permisos por
pagina: el codigo de user mode ve el rango bajo con el bit `U/S`
activado y el kernel, el rango alto con el bit desactivado. El hardware
impide que una aplicacion toque el kernel.

## 3. Flujo de arranque

```
BIOS
 |  carga el sector 0
 v
stage1 (NASM, 512 bytes)                Kernel/bootloader/stage1.asm
 |  lee el geometria del disco
 |  carga stage2 en 0x8000 con int 13h
 v
stage2 (NASM)                           Kernel/bootloader/stage2.asm
 |  A20, modo protegido de 32 bits
 |  mapa de memoria con int 15h (E820)
 |  carga el kernel.elf y el initrd
 |  tablas de pagina de 4 niveles
 |  modo largo de 64 bits
 v
_start (GAS)                            Kernel/src/boot.S
 |  pila, BSS a cero, copia del AxBootInfo
 v
kmain / ax_kernel_init                  Kernel/src/kernel.cpp
 |  consola -> PMM -> VMM -> heap -> GDT -> IDT -> PIC -> timer
 |  planificador, syscalls, IPC, ficheros, modulos
 v
shell del kernel                        Kernel/src/shell.cpp
```

## 4. Subsistemas

| Subsistema | Ficheros | Responsabilidad |
|---|---|---|
| CPU | `gdt.cpp`, `idt.cpp`, `isr.cpp`, `isr_stubs.S`, `pic.cpp` | GDT con anillos 0/3, IDT, excepciones, IRQ |
| Memoria | `pmm.cpp`, `vmm.cpp`, `heap.cpp` | frames fisicos, paginacion, `kmalloc` |
| Hilos | `sched.cpp`, `context.S` | round-robin, bloqueo, despertar |
| Syscalls | `syscall.cpp` | tabla de llamadas al sistema |
| Procesos | `proc.cpp`, `elf.cpp` | carga de ELF, pilas de usuario, `spawn` |
| IPC | `ipc.cpp` | puertos de mensajes |
| Modulos | `module.cpp` | carga, descarga, tabla de simbolos |
| VM | `Kernel/vm/axvm.cpp`, `vmhost.cpp` | bytecode Axolyn y sus nativas |
| Consola | `console.cpp`, `panic.cpp` | VGA, serie, `printf`, panicos |

## 5. Formato de disco

La imagen `build/axolynos.img` tiene un formato propietario:

```
LBA 0        stage1 (512 bytes)
LBA 1..63    stage2
LBA 64       cabecera AXDSK
             kernel.elf
             initrd.axfs
```

La cabecera AXDSK describe donde esta cada pieza. No hay sistema de
ficheros real todavia: el kernel trabaja sobre el initrd (una imagen AXFS
en memoria) y no escribe en disco.

## 6. Decisiones de diseño

* **C++ sin STL en el kernel.** El kernel se compila con `-ffreestanding
  -nostdlib -fno-exceptions -fno-rtti`: el codigo es mas cercano al metal
  y no hay dependencia de la libstdc++ del anfitrion.
* **Un solo espacio de direcciones.** Simplifica el arranque y la
  conmutacion de contexto; la separacion kernel/usuario se hace con los
  bits de permisos de las paginas.
* **Modulos sin relocaciones.** Cada modulo se enlaza en una ranura fija,
  asi que cargarlo es copiar bytes. La contrapartida es que los modulos
  usan el `AxModuleContext` en lugar de llamar al kernel directamente.
* **Bytecode para el lenguaje propio.** La VM del kernel es pequena,
  portable y permite ejecutar programas escritos en un lenguaje que no
  depende de un compilador de C.
