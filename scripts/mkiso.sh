#!/usr/bin/env bash
#
# AxolynOS - Genera una ISO híbrida arrancable por BIOS/Legacy y por UEFI.
#
#   BIOS/Legacy: El Torito en modo "hard disk" sobre build/axolynos.img.
#                El BIOS emula un disco y ejecuta el MBR de la imagen
#                (stage1), que lee stage2 y el kernel con int 13h.
#
#   UEFI x86_64: imagen FAT con EFI/BOOT/BOOTX64.EFI, publicada como
#                particion EFI (El Torito + particion GPT).
#
# Uso:
#   bash scripts/mkiso.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
IMG="$BUILD/axolynos.img"
ISO="$BUILD/AxolynOS.iso"
EFI_DIR="$BUILD/efi"
EFIBOOT_IMG="$BUILD/efiboot.img"
STAGE="$BUILD/iso"

echo "[ISO] Generando ISO híbrida BIOS + UEFI (x86_64)..."

if [ ! -f "$IMG" ]; then
  echo "[ERR] No existe $IMG. Ejecuta: bash scripts/build.sh"
  exit 1
fi

command -v xorriso >/dev/null 2>&1 || { echo "[ERR] Falta xorriso: sudo apt install xorriso"; exit 1; }
command -v mkfs.vfat >/dev/null 2>&1 || { echo "[ERR] Falta dosfstools: sudo apt install dosfstools"; exit 1; }
command -v mmd >/dev/null 2>&1 || { echo "[ERR] Falta mtools: sudo apt install mtools"; exit 1; }

# Aseguramos que el cargador UEFI está compilado (si es posible).
bash "$ROOT/scripts/build_uefi.sh" >/dev/null 2>&1 || true

if [ ! -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
  echo "[WARN] No hay BOOTX64.EFI: la ISO solo arrancará por BIOS."
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/EFI/BOOT" "$STAGE/EFI/AXOLYNOS" "$STAGE/boot"
cp -f "$IMG" "$STAGE/axolynos.img"

if [ -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
  cp -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" "$STAGE/EFI/BOOT/BOOTX64.EFI"
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" ]; then
  cp -f "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" "$STAGE/EFI/AXOLYNOS/axolyn.elf"
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" ]; then
  cp -f "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" "$STAGE/EFI/AXOLYNOS/initrd.axfs"
fi

# ---------------------------------------------------------------------------
# Imagen FAT de la partición EFI.  Debe caber el cargador, el kernel (~400 KiB)
# y el initrd (~700 KiB): 4 MiB en FAT16 es suficiente y lo entiende OVMF.
# ---------------------------------------------------------------------------
rm -f "$EFIBOOT_IMG"
dd if=/dev/zero of="$EFIBOOT_IMG" bs=1K count=4096 >/dev/null 2>&1
mkfs.vfat -F 12 -n AXEFI "$EFIBOOT_IMG" >/dev/null 2>&1
mmd -i "$EFIBOOT_IMG" ::/EFI >/dev/null 2>&1 || true
mmd -i "$EFIBOOT_IMG" ::/EFI/BOOT >/dev/null 2>&1 || true
mmd -i "$EFIBOOT_IMG" ::/EFI/AXOLYNOS >/dev/null 2>&1 || true

if [ -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/ >/dev/null 2>&1
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" ::/EFI/AXOLYNOS/ >/dev/null 2>&1
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" ::/EFI/AXOLYNOS/ >/dev/null 2>&1
fi

cp -f "$EFIBOOT_IMG" "$STAGE/boot/efiboot.img"

# ---------------------------------------------------------------------------
# ISO híbrida
# ---------------------------------------------------------------------------
rm -f "$ISO"

XORRISO_ARGS=(
  -as mkisofs
  -iso-level 3
  -full-iso9660-filenames
  -joliet
  -rational-rock
  -volid AXOLYNOS
  -appid "AxolynOS Hybrid BIOS+UEFI"
  -publisher "AxolynOS Project"
  -preparer "AxolynOS Build"
  # --- BIOS/Legacy: la imagen de disco completa como disco emulado ---
  -eltorito-boot axolynos.img
  -hard-disk-boot
)

if [ -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
  XORRISO_ARGS+=(
    # --- UEFI x86_64: imagen FAT como particion EFI ---
    -eltorito-alt-boot
    -e boot/efiboot.img
    -no-emul-boot
    -isohybrid-gpt-basdat
    -append_partition 2 0xEF "$EFIBOOT_IMG"
  )
fi

XORRISO_ARGS+=(-o "$ISO" "$STAGE")

xorriso "${XORRISO_ARGS[@]}"

echo "[OK] Generado: $ISO"
echo "[OK] Tamaño: $(du -h "$ISO" | awk '{print $1}')"
echo "[OK] Soporta BIOS/Legacy + UEFI x86_64"
