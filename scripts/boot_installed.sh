#!/usr/bin/env bash
#
# AxolynOS - Arranca el disco instalado con scripts/install_qemu.sh.
#
#   bash scripts/boot_installed.sh              # con ventana
#   bash scripts/boot_installed.sh --headless   # serie en la terminal
#
# La primera vez pedira crear el usuario y la contrasena; a partir de ahi
# mostrara la pantalla de login y despues el prompt.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT/build/axolynos-disk.img"
MODE=gfx

CYN='\033[0;36m'; RED='\033[0;31m'; NC='\033[0m'
die() { echo -e "${RED}[fail]${NC} $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --headless) MODE=headless; shift ;;
    --gfx)      MODE=gfx; shift ;;
    --img)      DISK="$2"; shift 2 ;;
    -h|--help)  sed -n '2,12p' "$0"; exit 0 ;;
    *) die "opcion desconocida: $1" ;;
  esac
done

command -v qemu-system-x86_64 >/dev/null 2>&1 || \
  die "QEMU no esta instalado"

[ -f "$DISK" ] || \
  die "no existe $DISK. Ejecuta primero: bash scripts/install_qemu.sh"

echo -e "${CYN}[axos]${NC} arrancando $DISK"
echo "      Para salir de QEMU: Ctrl+A y luego X"

QEMU_ARGS=(
  -m 512
  -cpu qemu64
  -no-reboot
  -rtc base=localtime
  -drive "format=raw,file=$DISK,if=ide,index=0,media=disk"
)

if [ "$MODE" = headless ]; then
  QEMU_ARGS+=(-display none -serial stdio)
else
  QEMU_ARGS+=(-serial stdio)
fi

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
