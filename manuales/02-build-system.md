# Sistema de compilacion

Todo se compila desde `scripts/build.sh` (o con el `Makefile` de la raiz,
que es una fachada del script). No hay ningun sistema de build externo:
solo `gcc`, `as`, `ld`, `nasm` y `ar`.

## 1. Comandos

```bash
bash scripts/build.sh               # todo: bootloader, tools, axc, kernel, modulos, apps, initrd, imagen
bash scripts/build.sh bootloader    # stage1.bin + stage2.bin (NASM)
bash scripts/build.sh tools         # mkinitramfs, mkdisk, mkmod, mkaxe, mkaxl, axl2a
bash scripts/build.sh compiler      # axc (compilador) y axrun (runner de host)
bash scripts/build.sh kernel        # build/axolyn.elf
bash scripts/build.sh modules       # build/modules/*.axmod
bash scripts/build.sh lib           # build/lib/libaxos.a y build/lib/libaxos.axl
bash scripts/build.sh apps          # build/apps/*.elf, *.axe y *.axb
bash scripts/build.sh initrd        # build/initrd.axfs
bash scripts/build.sh image         # build/axolynos.img
bash scripts/build.sh uefi          # build/efi/EFI/BOOT/BOOTX64.EFI
bash scripts/build.sh iso           # build/AxolynOS.iso (BIOS + UEFI)
bash scripts/build.sh clean         # borra build/
```

Equivalentes con `make`: `make`, `make kernel`, `make run`, `make test`,
`make install`, `make clean`.

## 2. Las cuatro toolchains

| Pieza | Compilador | Salida |
|---|---|---|
| Bootloader | `nasm -f bin` | `build/stage1.bin`, `build/stage2.bin` |
| Kernel | `g++ -ffreestanding ...` + `as --64` | `build/axolyn.elf` |
| Modulos | igual que el kernel + `ld -T module.ld` | `build/modules/*.axmod` |
| Apps nativas | `g++ -fPIE -pie -nostdlib` | `build/apps/*.elf` y `*.axe` |
| Compilador `axc` | `g++` (host, con STL) | `build/axc`, `build/axrun` |
| Herramientas | `g++` (host) | `build/tools/*` |

### 2.1 Banderas del kernel

```
-std=gnu++17 -O2 -g
-ffreestanding -nostdlib -fno-exceptions -fno-rtti
-mno-red-zone -mcmodel=kernel -fno-pic -fno-pie
-fno-stack-protector -fno-asynchronous-unwind-tables
-I Kernel/include -I apis/include
```

* `-mcmodel=kernel`: el kernel vive en `0xFFFFFFFF80200000`, dentro del
  rango de -2 GiB, asi que el modelo de codificacion "kernel" permite
  direccionamiento de 32 bits con signo.
* `-mno-red-zone`: las interrupciones pueden sobrescribir la zona roja
  de la pila.
* `-fno-pic -fno-pie`: el kernel se enlaza en una direccion fija.

Enlazado:

```
ld -T Kernel/linker/kernel.ld -z max-page-size=0x1000 --no-dynamic-linker
```

El script `kernel.ld` coloca `.text` en `0xFFFFFFFF80200000` y usa `AT()`
para que las cabeceras ELF contengan la direccion **fisica** (`0x200000`),
que es lo que usa el bootloader para copiar la imagen.

### 2.2 Banderas de user mode

```
-std=gnu++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti
-mno-red-zone -fPIE
```

Enlazado:

```
g++ -fPIE -pie -nostdlib -Wl,--no-dynamic-linker \
    -Wl,-T,apis/linker/userspace.ld -Wl,--undefined=_start
```

Las aplicaciones se enlazan como **ELF ET_DYN (PIE)** para que el cargador
del kernel pueda colocarlas en cualquier ranura (`AX_USER_VIRT_BASE +
ranura * 16 MiB`) aplicando las relocaciones `R_X86_64_RELATIVE`.

No se usa `-static` (produce `ET_EXEC` y el cargador lo rechaza) ni
`-Wl,--dynamic-linker` (el kernel carga el ELF directamente). El enlace se
hace contra `build/obj/lib/libaxos.from_axl.a`, extraido del `.axl` con
`axl2a`.

## 3. Herramientas de host

| Herramienta | Funcion |
|---|---|
| `scripts/tools/mkinitramfs.cpp` | empaqueta un directorio en una imagen AXFS |
| `scripts/tools/mkdisk.cpp` | construye la imagen de disco con la cabecera AXDSK |
| `scripts/tools/mkmod.cpp` | convierte un ELF de modulo en un `.axmod` plano |
| `scripts/tools/mkaxe.cpp` | empaqueta un ELF como ejecutable `.axe` |
| `scripts/tools/mkaxl.cpp` | empaqueta un archivo `ar` como biblioteca `.axl` |
| `scripts/tools/axl2a.cpp` | extrae el archivo `ar` de un `.axl` |

Se compilan automaticamente porque `build.sh` recorre
`scripts/tools/*.cpp`.

## 4. El pipeline completo

```
Kernel/src/*.cpp + *.S
        |  g++/as
        v
   build/obj/kernel/*.o
        |  ld -T kernel.ld
        v
   build/axolyn.elf  ──────────────┐
                                    |
Kernel/modules/*/  ──> mkmod ──> .axmod
                                    |
apis/src/*.cpp    ──> ar ──> libaxos.a ──> mkaxl ──> libaxos.axl
                                    |                    |
                                    |         axl2a (extrae el ar)
                                    |                    v
apps/native/*.cpp ──> g++ -pie ──> .elf ──> mkaxe ──> .axe
apps/axolyn/*.ax  ──> axc    ──> .axb
                                    |
                    mkinitramfs ──> build/initrd.axfs
                                    |
   build/stage1.bin + stage2.bin ──> mkdisk ──> build/axolynos.img
                                    |
                    BOOTX64.EFI + FAT ─┐
                                       v
                            build/AxolynOS.iso (BIOS + UEFI)
```

## 5. Anadir un fichero nuevo al kernel

1. Crea el `.cpp` (o `.S`) en `Kernel/src/`.
2. Si necesita una API nueva, **añade el prototipo al header**
   correspondiente de `Kernel/include/` (los headers son los contratos
   entre subsistemas).
3. No hace falta tocar el script de compilacion: `build.sh` recoge
   automaticamente todos los `.cpp` y `.S` de `Kernel/src/`.
4. Si el subsistema debe arrancar, llama a su `init` desde
   `ax_kernel_init()` (en `Kernel/src/kernel.cpp`).

Para el ensamblador: el fichero debe terminar en `.S` (preprocesado por
`as`) o `.s` (no preprocesado).

## 6. Depuracion de la compilacion

```bash
# ver los simbolos del kernel
readelf -h build/axolyn.elf
readelf -l build/axolyn.elf          # segmentos y direcciones fisicas
nm build/axolyn.elf | head -40

# recompilar un solo fichero
g++ -std=gnu++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
    -I Kernel/include -I apis/include -c Kernel/src/kernel.cpp -o /tmp/k.o

# ver el mapa de enlazado
less build/axolyn.elf.map
```

## 7. Problemas frecuentes

| Sintoma | Causa | Solucion |
|---|---|---|
| `undefined reference to __udivdi3` | division de 64 bits sin libgcc | compila con `-lgcc` al enlazar o evita `/` en 64 bits |
| `relocation truncated to fit` | direccion fuera de rango | usa `-mcmodel=kernel` y no references absolutas |
| `cannot find -lgcc` | falta la toolchain | `sudo apt install build-essential` |
| El kernel no arranca | revisa `build/axolyn.elf.map` y la imagen | `qemu-system-x86_64 -drive format=raw,file=build/axolynos.img -serial stdio -display none` |
