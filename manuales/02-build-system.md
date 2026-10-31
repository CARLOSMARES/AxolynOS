# Sistema de compilación

AxolynOS se compila con **Make** (principal) o **CMake** (alternativo). No hay
scripts de build intermedios: las reglas invocan directamente
`gcc`, `g++`, `as`, `ld`, `ar`, `nasm`, `python3`, `mkfs.vfat`, `mcopy` y
`xorriso`.

## 1. Make

```bash
make                # todo: ISO híbrida BIOS + UEFI
make -j$(nproc)     # en paralelo
make kernel         # solo build/axolyn.elf
make image          # solo build/axolynos.img
make iso            # solo build/AxolynOS.iso
make clean          # borra build/
make help           # lista completa de objetivos
```

Objetivos: `all`, `bootloader`, `tools`, `compiler`, `kernel`, `modules`,
`lib`, `apps`, `initrd`, `image`, `uefi`, `iso`, `run`, `run-headless`,
`run-install`, `run-disk`, `clean`, `help`.

## 2. CMake

```bash
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build -j$(nproc)
```

Los artefactos se generan en `cmake-build/build/` con la misma estructura que
`build/`.

## 3. Toolchains

| Pieza | Herramienta | Salida |
|---|---|---|
| Bootloader BIOS | `nasm -f bin` | `build/stage1.bin`, `build/stage2.bin` |
| Kernel | `g++ -ffreestanding` + `as --64` + `ld` | `build/axolyn.elf` |
| Módulos | igual que el kernel + `module.ld` | `build/modules/*.axmod` |
| Apps nativas | `g++ -fPIE -pie -nostdlib` | `build/apps/*.elf` y `*.axe` |
| Compilador `axc` | `g++` (host, con STL) | `build/axc`, `build/axrun` |
| Herramientas | `g++` (host) | `build/tools/*` |
| ISO | `xorriso` + `mkfs.vfat` + `mtools` | `build/AxolynOS.iso` |

### 3.1 Banderas del kernel

```
-std=gnu++17 -O2 -g
-ffreestanding -nostdlib -fno-exceptions -fno-rtti
-mno-red-zone -mcmodel=kernel -fno-pic -fno-pie
-fno-stack-protector -fno-asynchronous-unwind-tables
-I Kernel/include -I Kernel/vm -I apis/include
```

Enlazado:

```
ld -T Kernel/linker/kernel.ld -z max-page-size=0x1000 --no-dynamic-linker
```

`Kernel/linker/kernel.ld` coloca `.text` en `0xFFFFFFFF80200000` y usa `AT()`
para que las cabeceras ELF contengan la dirección física `0x200000`, que es la
que usa el bootloader para copiar el kernel a memoria.

### 3.2 Banderas de user mode

```
-std=gnu++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti
-mno-red-zone -fPIE -fno-stack-protector
```

Enlazado:

```
g++ -fPIE -pie -nostdlib -Wl,--build-id=none -Wl,--no-dynamic-linker \
    -Wl,-T,apis/linker/userspace.ld -Wl,--undefined=_start
```

Las aplicaciones se enlazan como ELF `ET_DYN` (PIE) para que el cargador del
kernel pueda colocarlas en cualquier ranura de 16 MiB aplicando las
relocalizaciones `R_X86_64_RELATIVE`.

## 4. Herramientas de host (`tools/`)

| Herramienta | Función |
|---|---|
| `tools/mkinitramfs.cpp` | empaqueta un directorio en una imagen AXFS |
| `tools/mkdisk.cpp` | construye la imagen de disco con cabecera AXDSK |
| `tools/mkmod.cpp` | convierte un ELF de módulo en `.axmod` |
| `tools/mkaxe.cpp` | empaqueta un ELF como `.axe` |
| `tools/mkaxl.cpp` | empaqueta un archivo `ar` como `.axl` |
| `tools/axl2a.cpp` | extrae el archivo `ar` de un `.axl` |
| `tools/elf2efi.py` | convierte el ELF de gnu-efi en PE32+ `BOOTX64.EFI` |

El `Makefile` y `CMakeLists.txt` compilan automáticamente todos los `.cpp` de
`tools/`.

## 5. Pipeline

```
Kernel/src/*.cpp + *.S + Kernel/vm/*.cpp
        |  g++ / as --64
        v
   build/obj/kernel/*.o
        |  ld -T kernel.ld
        v
   build/axolyn.elf ─────────────────────────┐
                                             |
Kernel/modules/*/  ──> ld + mkmod ──> .axmod ─┤
                                             |
apis/src/*.cpp  ──> ar ──> libaxos.a ──> mkaxl ──> libaxos.axl
                                             |
apps/native/*.cpp ──> g++ -pie ──> .elf ──> mkaxe ──> .axe
apps/axolyn/*.ax  ──> axc ──> .axb ──────────┤
                                             |
                        mkinitramfs ──> build/initrd.axfs
                                             |
stage1.bin + stage2.bin + axolyn.elf + initrd ──> mkdisk ──> axolynos.img
                                             |
BOOTX64.EFI + partición FAT + El Torito ─────┴──> AxolynOS.iso
```

## 6. Añadir un fichero nuevo al kernel

1. Crea el `.cpp` (o `.S`) en `Kernel/src/`.
2. Añade los prototipos necesarios en `Kernel/include/`.
3. No hay que tocar el build: el `Makefile` y CMake recogen todos los
   `Kernel/src/*.cpp` y `Kernel/src/*.S` automáticamente.
4. Si el subsistema debe arrancar, llama a su `init` desde `kmain()` en
   `Kernel/src/kernel.cpp`.

## 7. Depuración

```bash
readelf -h build/axolyn.elf
readelf -l build/axolyn.elf
nm build/axolyn.elf | head -40
less build/axolyn.elf.map

# recompilar un solo fichero
g++ -std=gnu++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
    -I Kernel/include -I Kernel/vm -I apis/include \
    -c Kernel/src/kernel.cpp -o /tmp/kernel.o
```

## 8. Problemas frecuentes

| Síntoma | Causa | Solución |
|---|---|---|
| `undefined reference to __udivdi3` | división de 64 bits sin libgcc | evita `/` y `%` de 64 bits en el kernel |
| `relocation truncated to fit` | dirección fuera de rango | usa `-mcmodel=kernel` |
| `nasm: unrecognized directive [org]` | ensamblar un `.asm` con `-f elf64` | el bootloader se ensambla con `nasm -f bin` |
| QEMU no muestra nada con `-nographic` | el firmware escribe en VGA | usa también `-serial stdio`; el kernel duplica la salida a COM1 |
| `Could not open ...iso` | la ISO no se generó | ejecuta `make iso` y comprueba `ls build/AxolynOS.iso` |
| `-cdrom build/axolynos.img` falla | `-cdrom` solo acepta ISO | usa `-drive format=raw,file=build/axolynos.img` |
