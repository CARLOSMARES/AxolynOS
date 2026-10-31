# El bootloader

AxolynOS arranca con un bootloader propio escrito en **NASM (sintaxis
Intel)**, en dos etapas, como es habitual en el mundo real.

```
Kernel/bootloader/stage1.asm    sector MBR de 512 bytes
Kernel/bootloader/stage2.asm    cargador: modo protegido -> modo largo
```

## 1. Stage 1 (512 bytes)

Es el sector que lee la BIOS. Su trabajo es mínimo:

1. Instalar las interrupciones (INT 13h, INT 10h) y guardar la geometria
   del disco con `INT 13h AH=08h`.
2. Sondear las extensiones LBA con `INT 13h AH=41h` (CF=0 y BX=0xAA55).
3. Cargar `stage2` (LBA 1, `stage2_sectors` sectores) en `0x8000`:
   * si hay LBA, con `INT 13h AH=42h`;
   * si no, traduciendo a CHS con `INT 13h AH=02h`.
4. Pasar a `stage2` en `SI` el modo de lectura (1 = LBA, 0 = CHS) y la
   geometria.
5. Saltar a `0:0x8000`.

**Restricciones**: debe ocupar **exactamente 512 bytes** y terminar con la
firma `0x55 0xAA` en el offset `0x1FE`. El script de compilacion lo
comprueba:

```bash
[ "$(stat -c%s build/stage1.bin)" -eq 512 ] || echo "stage1 mal dimensionado"
```

### Ejemplo: Compilar stage1

```bash
# Desde la raiz del proyecto
make bootloader
# O directamente:
nasm -f bin Kernel/bootloader/stage1.asm -o build/stage1.bin
```

### Verificar la firma:

```bash
xxd build/stage1.bin | head -4
# Debe verse la firma 55 4AAA al final
```

## 2. Stage 2

Ocupa como máximo 63 sectores (LBA 1..63) y hace todo el trabajo real:

| Paso | Qué hace |
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

### Lecturas condicionales

Las lecturas de kernel e initrd usan `read_sectors_staged`, que elige en
cada momento entre `AH=42h` (LBA) y `AH=02h` (CHS). Es imprescindible en
SeaBIOS cuando la ISO se arranca como CD-ROM: alli el "disco" emulado solo
acepta las llamadas antiguas (`AH <= 0x16`). Si `AH=42h` falla, el cargador
se pasa a CHS para el resto de la carga sin abortar.

### Ejemplo: Compilar stage2

```bash
nasm -f bin Kernel/bootloader/stage2.asm -o build/stage2.bin
```

### Verificar tamaño:

```bash
[ "$(stat -c%s build/stage2.bin)" -le 4096 ] || echo "stage2 demasiado grande"
# stage2 maximo: 63 sectores * 512 bytes = 32256 bytes
```

## 3. Formato de disco (AXDSK)

```
LBA 0        stage1 (512 bytes)
LBA 1..63    stage2
LBA 64       cabecera del disco (1 sector)
             kernel.elf (kernel_sectors sectores)
             initrd.axfs (initrd_sectors sectores)
```

### Cabecera del disco (offsets fijos, todo little-endian):

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

### Ejemplo: Generar la imagen de disco

```bash
build/tools/mkdisk --stage1 build/stage1.bin --stage2 build/stage2.bin \
    --kernel build/axolyn.elf --initrd build/initrd.axfs --out build/axolynos.img
```

### Verificar la imagen:

```bash
ls -lh build/axolynos.img
# Deberia tener un tamaño alrededor de 1.44MB (1474560 bytes)
```

## 4. La ISO hibrida (BIOS + UEFI)

`scripts/mkiso.sh` genera una unica ISO que arranca en las dos modalidades:

* **BIOS/Legacy**: El Torito en modo "hard disk" sobre
  `build/axolynos.img`. El BIOS emula un disco y ejecuta el MBR de la
  imagen (stage1). Es la ruta descrita en las secciones anteriores.
* **UEFI x86-64**: una imagen FAT con `EFI/BOOT/BOOTX64.EFI` (el cargador
  UEFI), `EFI/AXOLYNOS/axolyn.elf` e `initrd.axfs`, publicada como
  particion EFI (El Torito + tabla GPT).

### Estructura de la ISO:

```
build/AxolynOS.iso
├── El Torito  -> build/axolynos.img       (BIOS)
└── particion EFI
    └── EFI/
        ├── BOOT/BOOTX64.EFI
        └── AXOLYNOS/{axolyn.elf, initrd.axfs}
```

### Ejemplo: Generar la ISO

```bash
bash scripts/build.sh iso
# O manualmente:
build/tools/mkiso.sh -o build/AxolynOS.iso
```

### Probar ambas rutas:

```bash
# BIOS (ruta tradicional)
bash scripts/run.sh --headless

# UEFI
qemu-system-x86_64 -machine q35 -bios /usr/share/qemu/OVMF.fd \
    -cdrom build/AxolynOS.iso -boot d -m 512 \
    -display none -serial stdio
```

## 5. El kernel recibe la informacion

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

### Ejemplo: Estructura AxBootInfo en memoria

```bash
# En GDB, después de arrancar:
info p AxBootInfo
# Deberia mostrar los campos cargados por el bootloader
```

## 6. Probando el arranque

### BIOS:

```bash
bash scripts/build.sh bootloader
xxd build/stage1.bin | head -4          # debe verse la firma al final
bash scripts/run.sh          # log serie en la terminal
```

### BIOS headless (solo serie):

```bash
bash scripts/run.sh --headless
```

### UEFI:

```bash
bash scripts/build.sh iso
qemu-system-x86_64 -machine q35 -bios /usr/share/qemu/OVMF.fd \
    -cdrom build/AxolynOS.iso -boot d -m 512 \
    -display none -serial stdio
```

### Si el sistema no arranca:

* Si el log serie está vacío: el problema está antes del kernel
  (stage1/stage2).
* Si aparece el banner del kernel pero luego se reinicia: el problema
  está en el paso a modo largo o en las tablas de pagina.

---

**Siguiente manual**: [04-el microkernel](04-kernel-microkernel.md)