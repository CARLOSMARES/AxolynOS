# AxolynOS

Sistema operativo x86-64 experimental escrito **desde cero** en ensamblador
(NASM + GAS) y **C++**, con microkernel, modulos cargables en caliente,
un **compilador propio** (lenguaje *Axolyn* -> bytecode) con su maquina
virtual ejecutada dentro del kernel, una biblioteca de APIs de usuario y
aplicaciones base.

```
AxolynOS/
├── Kernel/          microkernel: arranque, CPU, memoria, planificador,
│                    syscalls, IPC, cargador ELF, modulos y la VM
│   ├── include/     cabeceros del kernel (contratos internos)
│   ├── src/         implementacion en C++ y ensamblador
│   ├── vm/          maquina virtual de bytecode (AXB) compartida
│   ├── modules/     modulos del kernel (.axmod)
│   ├── bootloader/  stage1 + stage2 + UEFI (BOOTX64.EFI)
│   └── linker/      scripts de enlazado (kernel, modulos)
├── apis/            libaxos: API de usuario (syscalls, procesos, ficheros)
├── apps/            aplicaciones base nativas (C++), Axolyn (bytecode) y ayuda
│   └── help/        archivos de ayuda (.axh)
├── compiladores/    axc: compilador del lenguaje Axolyn + runner de pruebas
├── manuales/        documentacion del sistema y guias de uso
└── scripts/         instalacion, compilacion, ejecucion y pruebas
```

Formatos de archivo:
- `.axe` - Ejecutables AxolynOS (cabecera `AXE1` + ELF64 PIE + CRC32)
- `.axl` - Librerías AxolynOS (cabecera `AXL1` + archivo `ar` + CRC32)
- `.axh` - Archivos de ayuda del sistema (se consultan con `man`)
- `.axb` - Bytecode del lenguaje Axolyn
- `.axmod` - Módulos del kernel
- `.axfs` - Sistema de archivos initrd
- `.elf` - ELF nativo suelto (compatibilidad con versiones anteriores)

El formato `.axe` permite validar el ejecutable (tamaño y CRC32) antes de
cargarlo y lleva el nombre y la arquitectura esperada. El formato `.axl`
empaqueta un archivo `ar` completo: `build.sh` enlaza las aplicaciones
contra la biblioteca **extraída del `.axl`**, de modo que el formato se
prueba de extremo a extremo en cada compilación.

## Arranque

La misma ISO arranca en BIOS/Legacy y en UEFI x86-64:

```bash
bash scripts/build.sh iso

# BIOS (SeaBIOS)
qemu-system-x86_64 -cdrom build/AxolynOS.iso -boot d -m 512 \
                   -display none -serial stdio

# UEFI (OVMF)
qemu-system-x86_64 -machine q35 -bios /usr/share/qemu/OVMF.fd \
                   -cdrom build/AxolynOS.iso -boot d -m 512 \
                   -display none -serial stdio
```

La imagen cruda `build/axolynos.img` también arranca en BIOS con
`-drive format=raw,file=build/axolynos.img`.

## Puesta en marcha rapida (Ubuntu en WSL2)

```bash
sudo bash scripts/install_ubuntu_wsl.sh   # dependencias (gcc, nasm, qemu...)
bash scripts/build.sh                     # compila todo
bash scripts/run.sh                       # arranca en QEMU
bash scripts/test.sh                      # bateria de pruebas
```

Compilar en el disco de Windows (`/mnt/c`) es lento: para Developing
rapido copia el proyecto al sistema de ficheros de Linux
(`cp -r . ~/projects/AxolynOS`).

## Documentacion

| Manual | Contenido |
|---|---|
| [manuales/00-indice.md](manuales/00-indice.md) | Indice y mapa del proyecto |
| [manuales/01-arquitectura.md](manuales/01-arquitectura.md) | Vista general del sistema y mapa de memoria |
| [manuales/02-build-system.md](manuales/02-build-system.md) | Como se compila cada pieza |
| [manuales/03-bootloader.md](manuales/03-bootloader.md) | El arranque: BIOS, modo protegido, modo largo |
| [manuales/04-kernel-microkernel.md](manuales/04-kernel-microkernel.md) | Subsistemas del kernel |
| [manuales/05-modulos.md](manuales/05-modulos.md) | **Como añadir un modulo al kernel** |
| [manuales/06-syscalls.md](manuales/06-syscalls.md) | Tabla de llamadas al sistema |
| [manuales/07-apis.md](manuales/07-apis.md) | La biblioteca de usuario |
| [manuales/08-crear-aplicaciones.md](manuales/08-crear-aplicaciones.md) | **Como crear una aplicacion** (C++ y Axolyn) |
| [manuales/09-lenguaje-axolyn.md](manuales/09-lenguaje-axolyn.md) | Referencia del lenguaje Axolyn |
| [manuales/10-bytecode-vm.md](manuales/10-bytecode-vm.md) | Bytecode AXB y la maquina virtual |
| [manuales/11-compilador.md](manuales/11-compilador.md) | El compilador `axc` por dentro |
| [manuales/12-depuracion.md](manuales/12-depuracion.md) | Depuracion con GDB/QEMU |

## Estado

Experimental. El kernel arranca en BIOS/Legacy y en UEFI, carga modulos,
expone syscalls a aplicaciones nativas en ring 3 y ejecuta programas del
lenguaje Axolyn dentro de la VM del kernel.

Probado con QEMU (SeaBIOS y OVMF):

* arranque BIOS desde `build/AxolynOS.iso` y desde `build/axolynos.img`;
* arranque UEFI desde la misma ISO (FAT + `EFI/BOOT/BOOTX64.EFI`);
* shell interactiva por consola serie;
* `run hello`, `run sysinfo`, `run ls /` y `libs` con aplicaciones `.axe`;
* reutilizacion de la ranura de user mode y liberacion de hilos al terminar.
