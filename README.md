# AxolynOS

AxolynOS es un sistema operativo experimental x86_64 escrito desde cero en
ensamblador x86 y C++ freestanding. Arranca por BIOS/Legacy y por UEFI desde
la misma ISO híbrida.

## Características

- **Arranque**: BIOS (stage1/stage2 + AXDSK) y UEFI (BOOTX64.EFI).
- **Núcleo**: PMM/VMM, heap, GDT/IDT/PIC/PIT, planificador, hilos, syscalls, IPC.
- **Sistemas de archivos**: VFS sobre initrd AxFS (en RAM) y AxFS-D en disco.
- **Discos**: driver ATA PIO LBA28 y soporte AHCI.
- **Instalador**: `install [n]` graba kernel, cargador e initrd en el disco y
  formatea AxFS-D copiando todo el árbol de aplicaciones.
- **Usuarios**: cuenta en el primer arranque, login, `adduser`, `logout`,
  base de datos cifrada (PBKDF2-SHA256 + ChaCha20).
- **Shell estilo MS-DOS**: `dir`, `cd`, `pwd`, `type`, `copy`, `ren`, `del`,
  `mkdir`, `rmdir`, `cls`, `ver`, `date`, `time`, `echo`, `help`, `disks`,
  `vol`, `format`, `mount`, `install`, `gc`, `run`, `ax`, `reboot`,
  `poweroff`, `ifconfig`, `dhcp`, `ping`, `nslookup`, `wget`, `netstat`,
  `ls`, `whoami`, `users`, `man`, `apps`, `libs`, `mods`, `services`,
  `service start|stop <nombre>`, `mem`, `heap`, `uptime`, `threads`, `vm`,
  `edit`, `vi`, `vim`, `nvim`.
- **Servicios**: gestor de servicios con arranque automático
  (`power`, `pci`, `disk`, `audio`, `video`, `users`, `signals`, `axfs`,
  `userdb`, `registry`, `install`, `gc`, `network`…).
- **Módulos del kernel**: `.axmod` con `_ax_module_entry(AxModuleContext*)`.
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
- `dir` / `dir /apps` / `dir /modules` — listado estilo MS-DOS (X ejecutable, F
  fichero, D dir)
- `cd <ruta>` / `pwd` — cambiar y mostrar directorio actual
- `ls` — alias de `dir`
- `run calc` o `calc` — ejecutar aplicaciones
- `edit archivo.txt` / `vi archivo.txt` — editores
- `services` — listar servicios del sistema con estado
- `service start <nombre>` / `service stop <nombre>` — gestionar un servicio
- `mods` — listar módulos cargados
- `format 0` / `mount 0` / `install 0` — gestión de discos
- `dhcp && ping 10.0.2.2 && wget http://ejemplo.com/` — red
- `poweroff` / `reboot` — apagar o reiniciar
- `date` / `time` — fecha y hora del RTC del hardware
- `sysinfo` — información del sistema (incluye hora Unix desde el RTC)

## Como agregar una pieza nueva del sistema

### 1. Agregar un modulo del kernel (`.axmod`)

Los módulos se cargan desde el initrd (`build/initrd.axfs`) en
`ax_module_autoload()`. El proceso completo esta documentado en
`manuales/05-modulos.md`.

1. Crear el fuente en `Kernel/modules/mi_modulo/mi_modulo.cpp` con el punto
   de entrada:

```cpp
extern "C" int _ax_module_entry(AxModuleContext* ctx)
{
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;
    }
    ctx->print("[mi_modulo] cargado\n");
    ctx->register_syscall(120, mi_syscall);
    ctx->publish("mi_modulo.hola", (void*)mi_syscall);
    return 0;
}
```

2. Las llamadas al sistema del modulo se registran con numeros **>= 100**
   (el kernel ocupa 0..27, ver `apis/include/axos/abi.h`).

3. Compilar y regenerar el initrd:

```bash
make modules   # build/modules/mi_modulo.{elf,axmod}
make iso       # regenera build/initrd.axfs e ISO
```

4. Verificar en el arranque:

```text
[mi_modulo] cargado
axolyn:/> mods
```

### 2. Agregar un servicio

Los servicios se registran en el gestor en `Kernel/src/service.cpp`
(`ax_service_init()`), con `auto_start` para que arranquen solos con
`ax_service_autostart()` al final del arranque.

1. Anadir el wrapper de inicializacion, por ejemplo:

```cpp
static void svc_miservicio(void) { ax_miservicio_init(); }
```

2. Registrarlo:

```cpp
ax_service_register("miservicio", "descripcion del servicio",
                    true, svc_miservicio, svc_miservicio_stop);
```

3. El shell lo lista con `services` y lo controla con
   `service start|stop <nombre>`.

### 3. Agregar una aplicacion nueva

Hay tres formas de que una aplicacion sea ejecutable:

1. **Nativa (user mode, ring 3)** — `apps/native/<nombre>.cpp`: implementa
   `int main(int argc, char** argv)` usando la libreria `libaxos`
   (`#include "axos/axos.hpp"`). El Makefile detecta automaticamente los
   `.cpp` de `apps/native/` y genera `.elf` + `.axe`. Se ejecuta con
   `run <nombre>` / `<nombre>` / `ax <nombre>`.

2. **Bytecode Axolyn** — `apps/axolyn/<nombre>.ax`, compilable con el
   compilador `build/axc`. Se genera `.axb` y se ejecuta con `ax <nombre>`.

3. **Biblioteca de usuario** — `apis/src/<nombre>.cpp`, empaquetada en
   `libaxos.axl`, consumida con `#include "axos/<nombre>.hpp"`.

La guia completa para crear aplicaciones esta en
`manuales/08-crear-aplicaciones.md` y la API en `manuales/07-apis.md`.

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

### Manuales

| # | Manual | Descripción |
|---|---|---|
| 00 | [Indice](manuales/00-indice.md) | Directorio y resumen general |
| 01 | [Arquitectura del sistema](manuales/01-arquitectura.md) | Vision general, mapa de memoria y subsistemas |
| 02 | [Sistema de compilacion](manuales/02-build-system.md) | Make y CMake, objetivos y artefactos |
| 03 | [El bootloader](manuales/03-bootloader.md) | Stage1, stage2 y arranque BIOS/UEFI |
| 04 | [El microkernel](manuales/04-kernel-microkernel.md) | Nucleo, planificador, memoria y hilos |
| 05 | [Modulos del kernel](manuales/05-modulos.md) | Crear, cargar y publicar modulos `.axmod` |
| 06 | [Llamadas al sistema](manuales/06-syscalls.md) | API de syscalls y ejemplos de uso |
| 07 | [APIs de usuario](manuales/07-apis.md) | Uso de `libaxos` para aplicaciones |
| 08 | [Crear aplicaciones](manuales/08-crear-aplicaciones.md) | Aplicaciones nativas `.cpp` y bytecode `.ax` |
| 09 | [Lenguaje Axolyn](manuales/09-lenguaje-axolyn.md) | Sintaxis y ejemplos del lenguaje propio |
| 10 | [Bytecode y maquina virtual](manuales/10-bytecode-vm.md) | VM interna y ejecucion de bytecode |
| 11 | [El compilador axc](manuales/11-compilador.md) | Compilador propio `axc` y flujo de trabajo |
| 12 | [Depuracion](manuales/12-depuracion.md) | Herramientas y técnicas de debug |
| 13 | [Servicios](manuales/13-servicios.md) | Gestor de servicios del sistema, registro y autostart |
```
