# AxolynOS - Indice de manuales

Este directorio contiene la documentacion tecnica del sistema y las guias
practicas para extenderlo.

| # | Manual | Para quien |
|---|---|---|
| 00 | [Indice](00-indice.md) | Todos |
| 01 | [Arquitectura del sistema](01-arquitectura.md) | Todos |
| 02 | [Sistema de compilacion](02-build-system.md) | Todos |
| 03 | [El bootloader](03-bootloader.md) | Quien toque el arranque |
| 04 | [El microkernel](04-kernel-microkernel.md) | Quien toque el kernel |
| 05 | [Modulos del kernel](05-modulos.md) | **Quien anada funcionalidad** |
| 06 | [Llamadas al sistema](06-syscalls.md) | Quien programe en user mode |
| 07 | [APIs de usuario](07-apis.md) | Quien use libaxos |
| 08 | [Crear aplicaciones](08-crear-aplicaciones.md) | **Quien cree programas** |
| 09 | [Lenguaje Axolyn](09-lenguaje-axolyn.md) | Quien programe en Axolyn |
| 10 | [Bytecode y maquina virtual](10-bytecode-vm.md) | Quien toque la VM |
| 11 | [El compilador axc](11-compilador.md) | Quien mejore el compilador |
| 12 | [Depuracion](12-depuracion.md) | Todos |

## Mapa rapido del codigo

```
Kernel/src/boot.S         arranque: pila, BSS, salto a kmain
Kernel/src/gdt.cpp        GDT con anillos 0 y 3 + TSS
Kernel/src/idt.cpp        IDT, puerta de syscall 0x80 (DPL 3)
Kernel/src/isr_stubs.S   trampolines de interrupcion y de syscall
Kernel/src/pmm.cpp        gestor de memoria fisica (bitmap)
Kernel/src/vmm.cpp        paginacion de 4 niveles
Kernel/src/heap.cpp       heap del kernel (kmalloc/kfree)
Kernel/src/sched.cpp      planificador round-robin e hilos
Kernel/src/syscall.cpp    despacho de llamadas al sistema
Kernel/src/ipc.cpp        puertos de mensajes
Kernel/src/elf.cpp        cargador ELF64 para user mode
Kernel/src/proc.cpp       procesos de user mode
Kernel/src/module.cpp     carga y descarga de modulos
Kernel/src/vmhost.cpp     integracion de la VM de bytecode
Kernel/vm/axvm.cpp        la maquina virtual (tambien se usa en el host)
Kernel/modules/           modulos de ejemplo
Kernel/bootloader/        stage1.asm y stage2.asm (NASM)
apis/                     libaxos: API de usuario
apps/                     aplicaciones nativas (C++) y Axolyn (bytecode)
compiladores/axc/         el compilador del lenguaje Axolyn
scripts/                  instalacion, compilacion, ejecucion y pruebas
```

## Convenciones del proyecto

* Codigo en ingles corto para los identificadores de API (`ax_*`), comentarios
  y documentacion en espanol.
* Todo el codigo del kernel es C++17 freestanding: sin STL, sin excepciones,
  sin RTTI y sin libc del anfitrion.
* El ensamblador del kernel usa **GAS (AT&T)** y el del arranque del disco
  usa **NASM (sintaxis Intel)**.
* Los numeros de syscall y las estructuras compartidas viven en
  `apis/include/axos/abi.h`: ese fichero es el contrato entre el kernel y
  las aplicaciones.
