#!/usr/bin/env bash
#
# AxolynOS - Arranca el sistema en QEMU.
#
#   bash scripts/run.sh              # modo grafico + serie en la terminal
#   bash scripts/run.sh --headless   # sin ventana, serie por consola
#   bash scripts/run.sh --debug      # arranca QEMU con GDB en el 1234
#   bash scripts/run.sh --mem 1024   # 1 GiB de RAM
#   bash scripts/run.sh -- <args>    # argumentos extra para QEMU
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
IMG="$BUILD/axolynos.img"

CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

MODE=gfx
MEM=512
DEBUG=0
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --headless) MODE=headless; shift ;;
    --gfx)      MODE=gfx; shift ;;
    --debug)    DEBUG=1; shift ;;
    --mem)      MEM="$2"; shift 2 ;;
    --img)      IMG="$2"; shift 2 ;;
    --)         shift; EXTRA=("$@"); break ;;
    -h|--help)  sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "opcion desconocida: $1" >&2; exit 1 ;;
  esac
done

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
  echo -e "${RED}[fail]${NC} QEMU no esta instalado. Ejecuta: sudo bash scripts/install_ubuntu_wsl.sh" >&2
  exit 1
}
[ -f "$IMG" ] || {
  echo -e "${RED}[fail]${NC} no existe $IMG. Ejecuta primero: bash scripts/build.sh" >&2
  exit 1
}

QEMU_ARGS=(
  -drive "format=raw,file=$IMG"
  -m "$MEM"
  -cpu qemu64
  -no-reboot
)

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

echo -e "${CYN}[axos]${NC} arrancando $IMG (${MEM} MiB)..."
echo -e "${CYN}[axos]${NC} para salir: Ctrl+A y luego X"
exec qemu-system-x86_64 "${QEMU_ARGS[@]}" "${EXTRA[@]}"
