#!/usr/bin/env bash
#
# AxolynOS - Prueba de arranque del bootloader.
#
#   bash Kernel/bootloader/tests/run_test.sh
#
# Construye el kernel de prueba, genera una imagen AXDSK con las herramientas
# de host y la arranca en QEMU en modo headless, enseñando la salida del
# puerto serie.  Es la forma mas rapida de comprobar que el bootloader lee el
# disco, detecta la memoria, carga el kernel y salta a modo largo.
#
# La imagen que se genera es build/test.img (NO es build/axolynos.img, que es
# la del kernel real).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="$ROOT/build"
TESTS="$ROOT/Kernel/bootloader/tests"

CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${CYN}==>${NC} $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
die()  { echo -e "${RED}[fail]${NC} $*" >&2; exit 1; }

for tool in nasm ld g++ qemu-system-x86_64; do
    command -v "$tool" >/dev/null 2>&1 || die "falta '$tool'"
done

mkdir -p "$BUILD"

step "Bootloader"
nasm -f bin "$ROOT/Kernel/bootloader/stage1.asm" -o "$BUILD/stage1.bin"
nasm -f bin "$ROOT/Kernel/bootloader/stage2.asm" -o "$BUILD/stage2.bin"
s2=$(stat -c%s "$BUILD/stage2.bin")
[ "$(stat -c%s "$BUILD/stage1.bin")" -eq 512 ] || die "stage1.bin no son 512 bytes"
[ "$s2" -le 32256 ] || die "stage2.bin no cabe en 63 sectores"
ok "stage1.bin (512 B) y stage2.bin ($s2 B)"

step "Herramientas de host"
g++ -std=c++17 -O2 -Wall -Wextra -I "$ROOT/Kernel/include" -I "$ROOT/apis/include" \
    "$ROOT/scripts/tools/mkdisk.cpp" -o "$BUILD/tools/mkdisk"
g++ -std=c++17 -O2 -Wall -Wextra -I "$ROOT/Kernel/include" -I "$ROOT/apis/include" \
    "$ROOT/scripts/tools/mkinitramfs.cpp" -o "$BUILD/tools/mkinitramfs"
ok "mkdisk y mkinitramfs"

step "Kernel de prueba"
nasm -f elf64 "$TESTS/testkernel.asm" -o "$BUILD/testkernel.o"
ld -T "$TESTS/testkernel.ld" -z max-page-size=0x1000 --no-dynamic-linker \
   -o "$BUILD/testkernel.elf" "$BUILD/testkernel.o"
ok "build/testkernel.elf ($(stat -c%s "$BUILD/testkernel.elf") bytes)"

step "Initramfs"
rm -rf "$BUILD/disk/testroot"
mkdir -p "$BUILD/disk/testroot/etc"
printf 'AxolynOS - kernel de prueba\n' > "$BUILD/disk/testroot/etc/motd"
"$BUILD/tools/mkinitramfs" "$BUILD/disk/testroot" "$BUILD/test-initrd.axfs" >/dev/null
ok "build/test-initrd.axfs"

step "Imagen de disco"
"$BUILD/tools/mkdisk" --stage1 "$BUILD/stage1.bin" --stage2 "$BUILD/stage2.bin" \
    --kernel "$BUILD/testkernel.elf" --initrd "$BUILD/test-initrd.axfs" \
    --out "$BUILD/test.img"
ok "build/test.img"

step "Arranque en QEMU (Ctrl+A y luego X para salir)"
set +e
timeout 30 qemu-system-x86_64 \
    -drive "format=raw,file=$BUILD/test.img" \
    -m 512 -cpu qemu64 -no-reboot -display none -serial stdio
rc=$?
set -e
if [ "$rc" -eq 124 ]; then
    ok "QEMU agotó el tiempo de espera (normal: el kernel de prueba se queda parado)"
else
    echo -e "${GRN}[ ok ]${NC} QEMU terminó con codigo $rc"
fi
