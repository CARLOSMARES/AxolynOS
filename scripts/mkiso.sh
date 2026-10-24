#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
IMG="$BUILD/axolynos.img"
ISO="$BUILD/AxolynOS.iso"
STAGE1="$BUILD/stage1.bin"
EFI_DIR="$BUILD/efi"
EFIBOOT_IMG="$BUILD/efiboot.img"

echo "[ISO] Generando ISO híbrida BIOS + UEFI (x86_64)..."

if [ ! -f "$IMG" ]; then
  echo "[ERR] No existe $IMG. Ejecuta: bash scripts/build.sh"
  exit 1
fi
if [ ! -f "$STAGE1" ]; then
  echo "[ERR] No existe $STAGE1. Ejecuta: bash scripts/build.sh"
  exit 1
fi

command -v xorriso >/dev/null 2>&1 || { echo "[ERR] Falta xorriso: sudo apt install xorriso"; exit 1; }
command -v mformat >/dev/null 2>&1 || command -v mkfs.vfat >/dev/null 2>&1 || { echo "[ERR] Falta dosfstools/mtools"; exit 1; }
command -v mcopy >/dev/null 2>&1 || true

# Preparar UEFI si existe
bash "$ROOT/scripts/build_uefi.sh" >/dev/null 2>&1 || true

rm -f "$ISO" "$EFIBOOT_IMG"

# Crear imagen EFI FAT (1.44MB)
dd if=/dev/zero of="$EFIBOOT_IMG" bs=1K count=2880 >/dev/null 2>&1
mkfs.vfat -F 12 -n EFIBOOT "$EFIBOOT_IMG" >/dev/null 2>&1

mmd -i "$EFIBOOT_IMG" ::/EFI >/dev/null 2>&1 || true
mmd -i "$EFIBOOT_IMG" ::/EFI/BOOT >/dev/null 2>&1 || true
mmd -i "$EFIBOOT_IMG" ::/EFI/AXOLYNOS >/dev/null 2>&1 || true

if [ -f "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/ >/dev/null 2>&1
  echo "[ISO] BOOTX64.EFI añadido"
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/AXOLYNOS/axolyn.elf" ::/EFI/AXOLYNOS/ >/dev/null 2>&1
fi
if [ -f "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" ]; then
  mcopy -i "$EFIBOOT_IMG" "$EFI_DIR/EFI/AXOLYNOS/initrd.axfs" ::/EFI/AXOLYNOS/ >/dev/null 2>&1
fi

# Generar ISO híbrida BIOS + UEFI
xorriso -as mkisofs \
  -iso-level 3 \
  -full-iso9660-filenames \
  -joliet \
  -rational-rock \
  -volid "AXOLYNOS" \
  -appid "AxolynOS Hybrid BIOS+UEFI" \
  -publisher "AxolynOS Project" \
  -preparer "AxolynOS Build" \
  -isohybrid-mbr "$STAGE1" \
  -partition_offset 16 \
  -eltorito-boot axolynos.img \
  -no-emul-boot \
  -boot-load-size 4 \
  -boot-info-table \
  -eltorito-alt-boot \
  -e efiboot.img \
  -no-emul-boot \
  -isohybrid-gpt-basdat \
  -append_partition 2 0xEF "$EFIBOOT_IMG" \
  -o "$ISO" \
  "$IMG" "$EFIBOOT_IMG"

echo "[OK] Generado: $ISO"
echo "[OK] Tamaño: $(du -h "$ISO" | awk '{print $1}')"
echo "[OK] Soporta BIOS/Legacy + UEFI x86_64"
