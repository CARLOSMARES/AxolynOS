#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
UEFI_DIR="$ROOT/Kernel/bootloader/uefi"

echo "[UEFI] Compilando cargador UEFI (BOOTX64.EFI)..."

if [ ! -d "$UEFI_DIR" ]; then
  echo "[ERR] Falta $UEFI_DIR/boot.c"
  exit 1
fi

if ! command -v gcc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1; then
  echo "[ERR] Faltan gcc/ld"
  exit 1
fi

if [ ! -f /usr/lib/elf_x86_64_efi.lds ] && [ ! -f /usr/lib/x86_64-linux-gnu/elf_x86_64_efi.lds ]; then
  echo "[ERR] Falta gnu-efi. Instala: sudo apt install gnu-efi"
  exit 1
fi

make -C "$UEFI_DIR" clean >/dev/null 2>&1 || true
make -C "$UEFI_DIR"

mkdir -p "$BUILD/efi/EFI/BOOT" "$BUILD/efi/EFI/AXOLYNOS"

cp -f "$UEFI_DIR/BOOTX64.EFI" "$BUILD/efi/EFI/BOOT/BOOTX64.EFI"
[ -f "$BUILD/axolyn.elf" ] && cp -f "$BUILD/axolyn.elf" "$BUILD/efi/EFI/AXOLYNOS/axolyn.elf"
[ -f "$BUILD/initrd.axfs" ] && cp -f "$BUILD/initrd.axfs" "$BUILD/efi/EFI/AXOLYNOS/initrd.axfs"

echo "[OK] UEFI listo: $BUILD/efi/EFI/BOOT/BOOTX64.EFI"
