# AxolynOS

AxolynOS es un sistema operativo experimental x86_64 escrito desde cero en ensamblador x86 y C++ freestanding. Soporta arranque dual BIOS/Legacy y UEFI desde una misma ISO híbrida.

## Características

- **Arranque**: BIOS (stage1/stage2 + AXDSK) y UEFI (BOOTX64.EFI). Funciona en QEMU y VirtualBox.
- **Núcleo**: PMM/VMM, heap, GDT/IDT/PIC/PIT, planificador, hilos, syscalls, IPC.
- **Sistemas de archivos**:
  - VFS sobre initrd AxFS (solo lectura)
  - AxFS-D en disco con superbloque, bitmap, formato, montaje, creación/borrado de archivos/directorios
- **Discos**: Driver ATA PIO LBA28 (maestro/esclavo, filtrado ATAPI). Soporte AHCI (stub).
- **Instalador**: `install [n]` instala el sistema arrancable + initrd en disco y formatea AxFS-D. Copia **todo** el initrd (aplicaciones, librerías, ayuda, configuración) al volumen AxFS-D, de modo que `dir` en el disco instalado muestra el mismo árbol que el medio de instalación. Genera `/LEEME.TXT`.
- **Usuarios**: primera cuenta en el primer arranque (usuario + contraseña), login posterior y base de datos persistente en `/etc/passwd`. `logout`, `login`, `adduser`, `whoami`, `users`.
- **Señales**: `SIGINT` (Ctrl+C), `SIGTERM`, `SIGQUIT`, `SIGKILL`, etc. El proceso en primer plano recibe Ctrl+C; `AX_SIG_DFL` termina el proceso, `AX_SIG_IGN` lo ignora y un manejador propio interrumpe la lectura con `AX_EINTR`.
- **Shell estilo MS-DOS**: `dir`, `cd`, `pwd`, `type`, `copy`, `ren`, `del`, `mkdir`, `rmdir`, `cls/clear`, `ver`, `date`, `time`, `echo`, `help`, `disks`, `vol`, `format`, `mount`, `install`, `gc`, `run`, `ax`, `reboot`, `poweroff`, `shutdown`, `halt`, `ifconfig`, `dhcp`, `ping`, `nslookup`, `wget`, `netstat`.
- **Listado `dir` estilo MS-DOS**: nombres 8.3 en mayúsculas, columna de marca (`X` ejecutable, `F` fichero, `D` directorio), `<DIR>`, tamaño con separador de miles, fecha/hora del RTC, cabecera de volumen/directorio y totales de archivos, directorios y espacio libre.
- **Editores**: `edit` (tipo Notepad), `vi`/`vim`/`nvim` con modos normal/insert/comando. Guardado en AxFS-D, deshacer, portapapeles, búsqueda, números de línea.
- **Red**: e1000 (sondeo), ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP.
- **Recolector de basura**: `gc` con recolección al cerrar aplicaciones/procesos.
- **Apps nativas**: `.axe`/`.elf` en `/apps/` y bytecode Axolyn (`.axb`/`.ax`).
- **Formatos propios**: `.axe` (ejecutables), `.axl` (librerías), `.axh` (ayuda), `.axmod` (módulos).

## Compilar

Requiere herramientas Linux/WSL: `gcc`, `g++`, `nasm`, `ld`, `make`, `objcopy`, `xorriso`, `python3`. Para UEFI se usan `gnu-efi`.

```bash
# Compilar todo (kernel, initrd, imagen de disco, ISO BIOS+UEFI)
bash scripts/build.sh all

# Objetivos individuales
bash scripts/build.sh kernel
bash scripts/build.sh image
bash scripts/build.sh iso
```

Los artefactos se generan en `build/`:
- `build/AxolynOS.iso` - ISO híbrida BIOS/UEFI
- `build/axolynos.img` - Imagen de disco de arranque
- `build/axolyn.elf`, `build/initrd.axfs`

## Ejecutar con QEMU

### Desde ISO (BIOS)
```bash
qemu-system-x86_64 \
  -cdrom build/AxolynOS.iso \
  -boot d \
  -m 512M \
  -serial stdio
```

### Con red
```bash
qemu-system-x86_64 \
  -cdrom build/AxolynOS.iso \
  -boot d \
  -m 512M \
  -serial stdio \
  -netdev user,id=n0 \
  -device e1000,netdev=n0
```

### UEFI
```bash
qemu-system-x86_64 \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -cdrom build/AxolynOS.iso \
  -boot d \
  -m 512M \
  -serial stdio
```

## Instalar en disco

La forma rápida: un solo script que borra `build/`, compila todo, crea el
disco virtual y arranca QEMU desde la ISO con el disco conectado.

```bash
bash scripts/install_qemu.sh              # 256 MiB, con ventana
bash scripts/install_qemu.sh --headless   # serie en la terminal
bash scripts/install_qemu.sh --size 512   # disco de 512 MiB
```

Dentro de AxolynOS crea tu cuenta en el primer arranque y ejecuta:

```text
disks
install 0
poweroff
```

Después arranca el disco instalado:

```bash
bash scripts/boot_installed.sh
bash scripts/boot_installed.sh --headless
```

La instalación copia el kernel, el initrd y **todos** los ficheros del
initrd al volumen AxFS-D. En el sistema instalado `dir` muestra las
aplicaciones en `C:\` y en `C:\APPS`, y los ficheros se pueden editar con
`edit`, `vi`, `vim` o `nvim`.

Instalación manual equivalente:

```bash
truncate -s 256M build/testdisk.img
qemu-system-x86_64 \
  -cdrom build/AxolynOS.iso \
  -boot d -m 512M -serial stdio \
  -drive format=raw,file=build/testdisk.img,if=ide,index=0,media=disk
```

```text
install 0
poweroff
```

```bash
qemu-system-x86_64 \
  -m 512M -serial stdio \
  -drive format=raw,file=build/testdisk.img,if=ide,index=0,media=disk
```

## Primer arranque y usuarios

En el primer arranque (ISO o disco recién instalado) el sistema pide crear
una cuenta:

```text
Usuario: ana
Contrasena: ******
Repite la contrasena: ******
```

La cuenta se guarda en `/etc/passwd` del volumen AxFS-D. En los arranques
siguientes aparece la pantalla de login. `logout` vuelve a pedir usuario y
contraseña; `adduser <usuario> <clave> [admin]` crea cuentas nuevas si el
usuario actual es administrador. La cuenta `root`/`root` existe como
recuperación de emergencia.

## Señales y Ctrl+C

Mientras una aplicación se ejecuta en primer plano, Ctrl+C envía `SIGINT`
al proceso:

- Acción por defecto (`AX_SIG_DFL`): el proceso termina (código 130) y la
  shell recupera el prompt.
- `AX_SIG_IGN`: la señal se descarta.
- Manejador propio: la lectura de consola (`AX_SYS_READ`/`AX_SYS_GETS`)
  devuelve `AX_EINTR`; la aplicación consulta la señal con
  `ax_proc_sigpending()`.

Las syscalls asociadas son `AX_SYS_SIGNAL` (33), `AX_SYS_SIGSEND` (34) y
`AX_SYS_SIGPENDING` (35).

## Uso rápido

- `help` - Ayuda
- `dir` / `dir /apps` - Listado estilo MS-DOS (X = ejecutable, F = archivo, D = directorio)
- `run calc` / `calc` - Ejecutar aplicaciones nativas en primer plano
- `edit archivo.txt` - Editor tipo Notepad (Ctrl+S guardar, Ctrl+Q salir)
- `vi archivo.txt` - Editor vi/vim/nvim
- `logout` / `login usuario clave` / `adduser usuario clave` - Sesiones
- `format 0` / `mount 0` / `install 0` - Gestión de discos AxFS-D
- `dhcp && ping 10.0.2.2 && wget http://ejemplo.com/` - Red
- `gc` - Recolector de basura
- `poweroff` / `shutdown` / `halt` - Apagar
- `reboot` - Reiniciar

## Estructura

```
AxolynOS/
├── Kernel/      # Núcleo, bootloaders, drivers
├── apis/        # APIs
├── apps/        # Aplicaciones Axolyn y herramientas
├── compiladores/# Compiladores
├── manuales/    # Documentación
├── scripts/     # Scripts de build
└── build/       # Artefactos generados
```

## Notas

- Todo el sistema está escrito en ensamblador y C++.
- Comentarios y documentación en español.
- Los ejecutables se marcan con `X` en `dir` (estilo MS-DOS).
- El instalador copia el kernel + initrd completo al disco, por lo que todas las aplicaciones y ficheros están disponibles tras instalar (`dir` en `C:\` ya no muestra solo `LEEME.TXT`).
- AxFS-D no guarda fecha por fichero: `dir` usa la fecha/hora actual del RTC, igual que el listado de un volumen recién copiado.
- La ISO es híbrida BIOS+UEFI, lista para usar con QEMU y VirtualBox.
