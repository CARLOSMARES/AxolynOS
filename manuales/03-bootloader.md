# El bootloader

AxolynOS arranca con un bootloader propio escrito en **NASM (sintaxis
Intel)**, en dos etapas, como es habitual en el mundo real.

```
Kernel/bootloader/stage1.asm    sector MBR de 512 bytes
Kernel/bootloader/stage2.asm    cargador: modo protegido -> modo largo
```

## 1. Stage 1 (512 bytes)

Es el sector que lee la BIOS. Su trabajo es minimo:

1. Instalar las interrupciones (INT 13h, INT 10h) y guardar la geometria
   del disco con `INT 13h AH=08h`.
2. Leer la geometria en CHS y preparar la LBA.
3. Cargar `stage2` (LBA 1, `stage2_sectors` sectores) en `0x8000` con
   `INT 13h AH=42h` (extensions LBA).
4. Saltar a `0:0x8000`.

Restricciones: debe ocupar **exactamente 512 bytes** y terminar con la
firma `0x55 0xAA` en el offset `0x1FE`. El script de compilacion lo
comprueba:

```bash
[ "$(stat -c%s build/stage1.bin)" -eq 512 ] || echo "stage1 mal dimensionado"
```

## 2. Stage 2

Ocupa como mucho 63 sectores (LBA 1..63) y hace todo el trabajo real:

| Paso | Que hace |
|---|---|
| 1 | Habilita **A20** (`INT 15h AX=2401h`) |
| 2 | Construye una GDT y pasa a **modo protegido de 32 bits** (`CR0.PE` + salto lejano) |
| 3 | Obtiene el mapa de memoria con `INT 15h AX=E820h` (hasta 32 entradas) en `0x9000` |
| 4 | Lee la cabecera AXDSK del LBA 64 |
| 5 | Carga `kernel.elf` en `0x200000` interpretando sus cabeceras de programa |
| 6 | Carga el initrd en `0x1F00000` |
| 7 | Construye las tablas de pagina de 4 niveles y activa el **modo largo** |
| 8 | Rellena `AxBootInfo` en `0x1000` |
| 9 | Salta a `e_entry` del kernel pasando en `RDI` la direccion de `AxBootInfo` |

### 2.1 Tablas de pagina

```
PML4  0x70000
  [0]   -> PDPT_low  0x71000   identidad 0..4 GiB (paginas de 2 MiB)
  [511] -> PDPT_high 0x72000
                [0] -> PD_high 0x73000
                         [16] -> PT 0x74000..   kernel 0xFFFFFFFF80200000 -> 0x200000
```

El kernel tiene **dos** PDPT distintas porque la identidad y el kernel
alto comparten el indice 0 de PDPT.

El mapa bajo se marca como supervisor (`U/S = 0`): es la tabla que usa el
proceso durante el arranque. El kernel la reconstruye despues con los
permisos correctos (user mode).

### 2.2 Modo largo

```
CR4.PAE = 1, CR4.PSE = 1
EFER.LME = 1        (MSR 0xC0000080)
CR3 = PML4
CR0.PG = 1, CR0.PE = 1
```

A partir de aqui el codigo se ejecuta en 64 bits con la pila en el modelo
de segmentos de 64 bits (base 0, limite 2^64-1).

## 3. Formato de disco (AXDSK)

```
LBA 0        stage1 (512 bytes)
LBA 1..63    stage2
LBA 64       cabecera del disco (1 sector)
             kernel.elf (kernel_sectors sectores)
             initrd.axfs (initrd_sectors sectores)
```

Cabecera (offsets fijos, todo little-endian):

| Offset | Tipo | Contenido |
|---|---|---|
| `0x00` | `char[8]` | `"AXDSK001"` |
| `0x08` | `u32` | version (1) |
| `0x0C` | `u32` | sectores de stage2 |
| `0x10` | `u64` | LBA de stage2 (1) |
| `0x18` | `u64` | LBA del kernel |
| `0x20` | `u64` | sectores del kernel |
| `0x28` | `u64` | LBA del initrd |
| `0x30` | `u64` | sectores del initrd |
| `0x38` | `u64` | direccion fisica del kernel (`0x200000`) |
| `0x40` | `u64` | direccion virtual del kernel (`0xFFFFFFFF80200000`) |
| `0x48` | `u64` | direccion fisica del initrd (`0x1F00000`) |
| `0x50` | `u64` | direccion de `AxBootInfo` (`0x1000`) |
| `0x58` | `u64` | direccion de la tabla E820 (`0x9000`) |
| `0x60` | `u64` | tamano de la memoria |

La imagen la genera `scripts/tools/mkdisk.cpp`:

```bash
build/tools/mkdisk --stage1 build/stage1.bin --stage2 build/stage2.bin \
    --kernel build/axolyn.elf --initrd build/initrd.axfs --out build/axolynos.img
```

## 4. El kernel recibe la informacion

`AxBootInfo` (`apis/include/axos/abi.h`):

```c
typedef struct {
    ax_u64 magic;        /* AX_BOOTINFO_MAGIC */
    ax_u64 kernel_phys;
    ax_u64 kernel_virt;
    ax_u64 initrd_phys;
    ax_u64 initrd_size;
    ax_u64 mem_size;
    ax_u64 e820_entries; /* numero de entradas E820 */
    ax_u64 e820_ptr;     /* puntero a la tabla   */
    ax_u64 cmdline;
} AxBootInfo;
```

`_start` (en `Kernel/src/boot.S`) la copia a una variable del kernel y
la pasa a `kmain()`.

## 5. Probando el arranque

```bash
bash scripts/build.sh bootloader
xxd build/stage1.bin | head -4          # debe verse la firma al final
bash scripts/run.sh --headless          # log serie en la terminal
```

Si el sistema no arranca, el log serie estara vacio: eso significa que
el problema esta antes del kernel (stage1/stage2). Si aparece el banner
del kernel pero luego se reinicia, el problema esta en el paso a modo
largo o en las tablas de pagina.
