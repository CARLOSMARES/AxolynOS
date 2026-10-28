#!/usr/bin/env bash
#
# AxolynOS - Instalacion limpia en QEMU.
#
#   1. Borra build/ por completo.
#   2. Compila kernel, aplicaciones, initrd, imagen BIOS y ISO hibrida.
#   3. Crea un disco duro virtual vacio (build/axolynos-disk.img).
#   4. Arranca QEMU desde la ISO con ese disco conectado.
#
# Dentro de AxolynOS:
#
#   disks                 (opcional) comprueba que el disco es el 0
#   install 0             instala el sistema y copia TODO el initrd a AxFS-D
#   poweroff              apaga la maquina
#
# Despues arranca el disco instalado con:
#
#   bash scripts/boot_installed.sh
#
# Uso:
#   bash scripts/install_qemu.sh                 # 256 MiB de disco, con ventana
#   bash scripts/install_qemu.sh --headless      # serie en la terminal
#   bash scripts/install_qemu.sh --size 512      # disco de 512 MiB
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
DISK="$BUILD/axolynos-disk.img"
ISO="$BUILD/AxolynOS.iso"
SIZE_MB=256
MODE=gfx

CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${CYN}==>${NC} $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
die()  { echo -e "${RED}[fail]${NC} $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --headless) MODE=headless; shift ;;
    --gfx)      MODE=gfx; shift ;;
    --size)     SIZE_MB="$2"; shift 2 ;;
    -h|--help)  sed -n '2,28p' "$0"; exit 0 ;;
    *) die "opcion desconocida: $1" ;;
  esac
done

command -v qemu-system-x86_64 >/dev/null 2>&1 || \
  die "QEMU no esta instalado (sudo bash scripts/install_ubuntu_wsl.sh)"

step "Borrando build/ y compilando todo"
rm -rf "$BUILD"
bash "$ROOT/scripts/build.sh" all

[ -f "$ISO" ] || die "no se genero $ISO"

step "Creando disco virtual de ${SIZE_MB} MiB"
mkdir -p "$BUILD"
if command -v qemu-img >/dev/null 2>&1; then
  qemu-img create -f raw "$DISK" "${SIZE_MB}M" >/dev/null
else
  truncate -s "${SIZE_MB}M" "$DISK"
fi
ok "$DISK"

step "Arrancando QEMU (ISO + disco). Instala con: install 0"
echo "      Para salir de QEMU: Ctrl+A y luego X"
echo

QEMU_ARGS=(
  -m 512
  -cpu qemu64
  -no-reboot
  -rtc base=localtime
  -cdrom "$ISO"
  -drive "format=raw,file=$DISK,if=ide,index=0,media=disk"
  -boot d
)

if [ "$MODE" = headless ]; then
  QEMU_ARGS+=(-display none -serial stdio)
else
  QEMU_ARGS+=(-serial stdio)
fi

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
