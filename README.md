# AxolynOS

AxolynOS es un sistema operativo experimental x86_64 escrito desde cero en
ensamblador x86 y C++ freestanding. Arranca por BIOS/Legacy y por UEFI desde
la misma ISO híbrida.

## Características

- **Arranque**: BIOS (stage1/stage2 + AXDSK) y UEFI (BOOTX64.EFI).
- **Núcleo**: PMM/VMM, heap, GDT/IDT/PIC/PIT, planificador, hilos, syscalls, IPC.
- **Sistemas de archivos**: VFS sobre initrd AxFS (solo lectura) y AxFS-D en
  disco (superbloque, bitmap, formato, montaje, creación/borrado).
- **Discos**: driver ATA PIO LBA28 y soporte AHCI.
- **Instalador**: `install [n]` graba kernel, cargador e initrd en el disco y
  formatea AxFS-D copiando todo el árbol de aplicaciones.
- **Usuarios**: cuenta en el primer arranque, login, `adduser`, `logout`,
  base de datos cifrada (PBKDF2-SHA256 + ChaCha20).
- **Shell estilo MS-DOS**: `dir`, `cd`, `pwd`, `type`, `copy`, `ren`, `del`,
  `mkdir`, `rmdir`, `cls`, `ver`, `date`, `time`, `echo`, `help`, `disks`,
  `vol`, `format`, `mount`, `install`, `gc`, `run`, `ax`, `reboot`,
  `poweroff`, `ifconfig`, `dhcp`, `ping`, `nslookup`, `wget`, `netstat`,
  `ls`, `whoami`, `users`, `man`, `apps`, `libs`, `mods`, `mem`, `heap`,
  `uptime`, `threads`, `vm`, `edit`, `vi`, `vim`, `nvim`.
- **Editores**: `edit` (tipo Notepad) y `vi`/`vim`/`nvim`.
- **Red**: e1000, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP.
- **Formatos propios**: `.axe`, `.axl`, `.axh`, `.axb`, `.axmod`.

## Requisitos

Linux o WSL con:

```bash
sudo apt install build-essential nasm xorriso dosfstools mtools gnu-efi python3 cmake
```

## Compilar con Make

```bash
make            # compila todo y genera build/AxolynOS.iso
make -j$(nproc) # en paralelo
make help       # lista de objetivos
```

Objetivos individuales:

```bash
make bootloader   # build/stage1.bin, build/stage2.bin (NASM)
make tools        # build/tools/* (mkdisk, mkinitramfs, mkmod, ...)
make compiler     # build/axc, build/axrun
make kernel       # build/axolyn.elf
make modules      # build/modules/*.axmod
make lib          # build/lib/libaxos.a, libaxos.axl
make apps         # build/apps/*.elf, *.axe, *.axb
make initrd       # build/initrd.axfs
make image        # build/axolynos.img
make uefi         # build/efi/EFI/BOOT/BOOTX64.EFI
make iso          # build/AxolynOS.iso
make clean        # borra build/
```

## Compilar con CMake

```bash
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build -j$(nproc)
```

Los artefactos quedan en `cmake-build/build/` con la misma estructura que
`build/`.

## Artefactos

```
build/
  AxolynOS.iso                 ISO híbrida BIOS + UEFI (live/instalación)
  axolynos.img                 imagen de disco arrancable (BIOS)
  axolyn.elf                   kernel
  axolyn.elf.map               mapa de enlazado
  initrd.axfs                  initramfs AXFS
  stage1.bin, stage2.bin       bootloader BIOS
  efi/EFI/BOOT/BOOTX64.EFI     cargador UEFI
  apps/                        .elf, .axe y .axb
  modules/                     .axmod
  lib/                         libaxos.a y libaxos.axl
  tools/                       herramientas de host
```

## Ejecutar en QEMU

Arrancar la ISO (BIOS) con la consola serie en la terminal:

```bash
qemu-system-x86_64 -m 512 -cpu qemu64 -no-reboot \
  -cdrom build/AxolynOS.iso -boot d -display none -serial stdio
```

O con ventana (la salida también se duplica a la serie):

```bash
qemu-system-x86_64 -m 512 -cpu qemu64 -cdrom build/AxolynOS.iso -boot d -serial stdio
```

Atajos:

```bash
make run            # ISO con ventana
make run-headless   # ISO sin ventana, serie en la terminal
```

Arranque UEFI (requiere OVMF):

```bash
qemu-system-x86_64 -m 512 -machine q35 \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/AxolynOS.iso -boot d -serial stdio
```

### Importante sobre `-cdrom`

`-cdrom` solo acepta una **ISO**. Para arrancar la imagen de disco hay que
usar `-drive`:

```bash
qemu-system-x86_64 -m 512 -serial stdio \
  -drive format=raw,file=build/axolynos.img
```

## Instalar en disco

1. Crea un disco virgen y arranca la ISO con ese disco conectado:

```bash
make run-install
```

Equivalente manual:

```bash
truncate -s 256M build/install-disk.img
qemu-system-x86_64 -m 512 -serial stdio \
  -cdrom build/AxolynOS.iso -boot d \
  -drive format=raw,file=build/install-disk.img,if=ide,index=0,media=disk
```

2. Dentro de AxolynOS crea tu cuenta y ejecuta:

```text
disks
install 0
poweroff
```

3. Arranca el disco instalado:

```bash
make run-disk
```

Equivalente manual:

```bash
qemu-system-x86_64 -m 512 -serial stdio \
  -drive format=raw,file=build/install-disk.img,if=ide,index=0,media=disk
```

En el sistema instalado se monta AxFS-D y `dir` muestra las aplicaciones en
`C:\` y en `C:\APPS`.

## Primer arranque y usuarios

```text
Usuario: ana
Contrasena: ******
Repite la contrasena: ******
```

La cuenta `root`/`root` existe como recuperación de emergencia. `logout`
vuelve a pedir credenciales y `adduser <usuario> <clave> [admin]` crea cuentas
nuevas si el usuario actual es administrador.

## Uso rápido del shell

- `help` — ayuda
- `dir` / `dir /apps` — listado estilo MS-DOS (X ejecutable, F fichero, D dir)
- `run calc` o `calc` — ejecutar aplicaciones
- `edit archivo.txt` / `vi archivo.txt` — editores
- `format 0` / `mount 0` / `install 0` — gestión de discos
- `dhcp && ping 10.0.2.2 && wget http://ejemplo.com/` — red
- `poweroff` / `reboot` — apagar o reiniciar

## Estructura del repositorio

```
AxolynOS/
├── Kernel/           núcleo, bootloaders, drivers, módulos
├── apis/             API de usuario (libaxos)
├── apps/             aplicaciones nativas (.cpp) y Axolyn (.ax)
├── compiladores/axc/ compilador Axolyn
├── manuales/         documentación
├── tools/            herramientas de build (C++ y elf2efi.py)
├── Makefile          build principal
├── CMakeLists.txt    build alternativo con CMake
└── build/            artefactos generados (no versionado)
```

## Notas

- El proyecto no usa scripts de build: todo lo coordina `Makefile` o `CMake`.
- Los `.S` se ensamblan con `as --64`; los `.asm` del bootloader con
  `nasm -f bin`.
- La ISO es híbrida: El Torito BIOS sobre `axolynos.img` y partición EFI FAT
  con `BOOTX64.EFI`.
