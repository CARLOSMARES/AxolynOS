#!/usr/bin/env bash
#
# AxolynOS - Arranca el sistema en QEMU.
#
#   bash scripts/run.sh              # modo grafico + serie en la terminal
#   bash scripts/run.sh --headless   # sin ventana, serie por consola
#   bash scripts/run.sh --uefi       # arranque UEFI (necesita OVMF)
#   bash scripts/run.sh --iso        # arranca desde build/AxolynOS.iso
#   bash scripts/run.sh --debug      # arranca QEMU con GDB en el 1234
#   bash scripts/run.sh --mem 1024   # 1 GiB de RAM
#   bash scripts/run.sh -- <args>    # argumentos extra para QEMU
#
# Para salir del modo --headless: Ctrl+A y luego X.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
IMG="$BUILD/axolynos.img"
ISO="$BUILD/AxolynOS.iso"

CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; YEL='\033[0;33m'; NC='\033[0m'

MODE=gfx
MEM=512
DEBUG=0
UEFI=0
FROM_ISO=0
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --headless) MODE=headless; shift ;;
    --gfx)      MODE=gfx; shift ;;
    --uefi)     UEFI=1; shift ;;
    --iso)      FROM_ISO=1; shift ;;
    --debug)    DEBUG=1; shift ;;
    --mem)      MEM="$2"; shift 2 ;;
    --img)      IMG="$2"; shift 2 ;;
    --)         shift; EXTRA=("$@"); break ;;
    -h|--help)  sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "opcion desconocida: $1" >&2; exit 1 ;;
  esac
done

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
  echo -e "${RED}[fail]${NC} QEMU no esta instalado. Ejecuta: sudo bash scripts/install_ubuntu_wsl.sh" >&2
  exit 1
}

# ---------------------------------------------------------------------------
# Localizar el firmware UEFI (OVMF).  En Ubuntu el paquete ovmf instala
# OVMF_CODE*.fd en /usr/share/OVMF; /usr/share/qemu/OVMF.fd es la imagen
# unificada que acepta -bios directamente.
# ---------------------------------------------------------------------------
find_ovmf() {
  local candidates=(
    /usr/share/qemu/OVMF.fd
    /usr/share/ovmf/OVMF.fd
    /usr/share/OVMF/OVMF_CODE.fd
  )
  local f
  for f in "${candidates[@]}"; do
    if [ -f "$f" ]; then
      echo "$f"
      return 0
    fi
  done
  return 1
}

QEMU_ARGS=(-m "$MEM" -cpu qemu64 -no-reboot)

if [ "$FROM_ISO" -eq 1 ]; then
  [ -f "$ISO" ] || {
    echo -e "${RED}[fail]${NC} no existe $ISO. Ejecuta: bash scripts/build.sh iso" >&2
    exit 1
  }
  QEMU_ARGS+=(-cdrom "$ISO" -boot d)
  echo -e "${CYN}[axos]${NC} arrancando $ISO (${MEM} MiB)..."
else
  [ -f "$IMG" ] || {
    echo -e "${RED}[fail]${NC} no existe $IMG. Ejecuta primero: bash scripts/build.sh" >&2
    exit 1
  }
  QEMU_ARGS+=(-drive "format=raw,file=$IMG")
  echo -e "${CYN}[axos]${NC} arrancando $IMG (${MEM} MiB)..."
fi

if [ "$UEFI" -eq 1 ]; then
  OVMF="$(find_ovmf)" || {
    echo -e "${RED}[fail]${NC} no se encuentra OVMF. Instala el firmware:" >&2
    echo -e "       sudo apt install ovmf" >&2
    echo -e "       (rutas habituales: /usr/share/OVMF/OVMF_CODE_4M.fd, /usr/share/qemu/OVMF.fd)" >&2
    exit 1
  }
  echo -e "${CYN}[axos]${NC} firmware UEFI: $OVMF"
  QEMU_ARGS+=(-machine q35 -bios "$OVMF")
fi

case "$MODE" in
  headless) QEMU_ARGS+=(-display none -serial stdio) ;;
  gfx)      QEMU_ARGS+=(-serial stdio) ;;
esac

if [ "$DEBUG" -eq 1 ]; then
  QEMU_ARGS+=(-s -S)
  echo -e "${CYN}[axos]${NC} QEMU detenido esperando GDB en 127.0.0.1:1234"
  echo -e "${CYN}[axos]${NC}   gdb -ex 'target remote :1234' -ex 'symbol-file build/axolyn.elf'"
  exec qemu-system-x86_64 "${QEMU_ARGS[@]}" "${EXTRA[@]}"
fi

echo -e "${CYN}[axos]${NC} para salir: Ctrl+A y luego X"
exec qemu-system-x86_64 "${QEMU_ARGS[@]}" "${EXTRA[@]}"
