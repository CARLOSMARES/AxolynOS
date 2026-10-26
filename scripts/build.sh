#!/usr/bin/env bash
#
# AxolynOS - Script de compilacion completo.
#
#   bash scripts/build.sh              # compila todo
#   bash scripts/build.sh kernel       # solo el kernel
#   bash scripts/build.sh compiler     # solo el compilador Axolyn (axc/axrun)
#   bash scripts/build.sh tools        # solo las herramientas de host
#   bash scripts/build.sh modules      # solo los modulos del kernel
#   bash scripts/build.sh apps         # solo las aplicaciones
#   bash scripts/build.sh image        # solo la imagen de disco
#   bash scripts/build.sh clean        # borra build/
#
# Todo se compila en build/ y el resultado final es build/axolynos.img
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
OBJ="$BUILD/obj"
TOOLS="$BUILD/tools"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'; BLD='\033[1m'; NC='\033[0m'
step() { echo -e "${CYN}==>${NC} ${BLD}$*${NC}"; }
info() { echo -e "    $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
warn() { echo -e "${YEL}[warn]${NC} $*"; }
die()  { echo -e "${RED}[fail]${NC} $*" >&2; exit 1; }

TARGET="${1:-all}"

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || die "falta '$1'. Ejecuta: sudo bash scripts/install_ubuntu_wsl.sh"; }

KERNEL_CFLAGS=(
  -std=gnu++17 -O2 -g
  -ffreestanding -nostdlib -fno-exceptions -fno-rtti
  -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie
  -fno-stack-protector -fno-asynchronous-unwind-tables
  -Wall -Wextra -Wno-unused-parameter
  -I "$ROOT/Kernel/include" -I "$ROOT/Kernel/vm" -I "$ROOT/apis/include"
)
KERNEL_LDFLAGS=(-T "$ROOT/Kernel/linker/kernel.ld" -z max-page-size=0x1000 --no-dynamic-linker)

USER_CFLAGS=(
  -std=gnu++17 -O2 -g
  -ffreestanding -nostdlib -fno-exceptions -fno-rtti
  -mno-red-zone -fPIE -fno-stack-protector -fno-asynchronous-unwind-tables
  -Wall -Wextra -Wno-unused-parameter
  -I "$ROOT/apis/include"
)

HOST_CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra)

# ---------------------------------------------------------------------------
# Utilidades
# ---------------------------------------------------------------------------
mkdirs() {
  mkdir -p "$BUILD" "$OBJ/kernel" "$OBJ/apps" "$OBJ/lib" "$OBJ/axc" \
           "$TOOLS" "$BUILD/lib" "$BUILD/apps" "$BUILD/modules" "$BUILD/disk"
}

# ---------------------------------------------------------------------------
# 1. Herramientas de host
# ---------------------------------------------------------------------------
build_tools() {
  step "Herramientas de host"
  need g++
  local src
  for src in "$ROOT"/scripts/tools/*.cpp; do
    [ -e "$src" ] || continue
    local name; name="$(basename "$src" .cpp)"
    g++ "${HOST_CXXFLAGS[@]}" -I "$ROOT/Kernel/include" -I "$ROOT/apis/include" \
        "$src" -o "$TOOLS/$name"
    info "$name"
  done
  ok "herramientas en build/tools"
}

# ---------------------------------------------------------------------------
# 2. Compilador Axolyn (axc) y runner de host (axrun)
# ---------------------------------------------------------------------------
build_compiler() {
  step "Compilador Axolyn (axc) y runner (axrun)"
  need g++
  if [ ! -d "$ROOT/compiladores/axc/src" ]; then
    warn "no existe compiladores/axc/src: se omite"
    return 0
  fi
  local objs=() src
  for src in "$ROOT"/compiladores/axc/src/*.cpp; do
    [ -e "$src" ] || continue
    case "$(basename "$src")" in
      axrun.cpp) continue ;;
    esac
    objs+=("$OBJ/axc/$(basename "$src" .cpp).o")
    g++ "${HOST_CXXFLAGS[@]}" -I "$ROOT/compiladores/axc/include" -I "$ROOT/Kernel/vm" \
        -c "$src" -o "${objs[-1]}"
  done
  g++ "${HOST_CXXFLAGS[@]}" -I "$ROOT/compiladores/axc/include" -I "$ROOT/Kernel/vm" \
      "${objs[@]}" -o "$BUILD/axc"

  # axrun reutiliza la VM del kernel (mismo fuente, compilado para host)
  if [ -f "$ROOT/Kernel/vm/axvm.cpp" ]; then
    g++ "${HOST_CXXFLAGS[@]}" -I "$ROOT/Kernel/vm" \
        "$ROOT/Kernel/vm/axvm.cpp" "$ROOT/compiladores/axc/src/axrun.cpp" \
        -o "$BUILD/axrun"
  fi
  ok "build/axc y build/axrun"
}

# ---------------------------------------------------------------------------
# 3. Kernel
# ---------------------------------------------------------------------------
build_kernel() {
  step "Kernel (C++ freestanding + GAS)"
  need g++ ; need as ; need ld
  rm -rf "$OBJ/kernel"; mkdir -p "$OBJ/kernel"

  local src obj
  for src in "$ROOT"/Kernel/src/*.cpp; do
    [ -e "$src" ] || continue
    obj="$OBJ/kernel/$(basename "$src" .cpp).o"
    g++ "${KERNEL_CFLAGS[@]}" -c "$src" -o "$obj"
  done
  # La maquina virtual de bytecode (Kernel/vm/axvm.cpp) es codigo compartido
  # con el runner de host: se compila tambien dentro del kernel.
  for src in "$ROOT"/Kernel/vm/*.cpp; do
    [ -e "$src" ] || continue
    obj="$OBJ/kernel/vm_$(basename "$src" .cpp).o"
    g++ "${KERNEL_CFLAGS[@]}" -c "$src" -o "$obj"
  done
  for src in "$ROOT"/Kernel/src/*.S; do
    [ -e "$src" ] || continue
    obj="$OBJ/kernel/$(basename "$src" .S).o"
    as --64 -o "$obj" "$src"
  done

  ld "${KERNEL_LDFLAGS[@]}" -Map "$BUILD/axolyn.elf.map" -o "$BUILD/axolyn.elf" "$OBJ/kernel"/*.o
  ok "build/axolyn.elf ($(stat -c%s "$BUILD/axolyn.elf") bytes)"
}

# ---------------------------------------------------------------------------
# 4. Modulos del kernel (.axmod)
# ---------------------------------------------------------------------------
build_modules() {
  step "Modulos del kernel"
  if [ ! -d "$ROOT/Kernel/modules" ]; then
    warn "no hay directorio Kernel/modules"; return 0
  fi
  if [ ! -x "$TOOLS/mkmod" ]; then
    warn "falta build/tools/mkmod (se omite el empaquetado)"; return 0
  fi
  local slot=0 dir name elf out
  for dir in "$ROOT"/Kernel/modules/*/; do
    [ -d "$dir" ] || continue
    name="$(basename "$dir")"
    local srcs=()
    for f in "$dir"*.cpp "$dir"*.c "$dir"*.S; do
      [ -e "$f" ] && srcs+=("$f")
    done
    [ ${#srcs[@]} -gt 0 ] || continue

    local objs=() s
    for s in "${srcs[@]}"; do
      local o="$OBJ/kernel/mod_${name}_$(basename "$s" | tr '.' '_').o"
      case "$s" in
        *.S) as --64 -o "$o" "$s" ;;
        *)   g++ "${KERNEL_CFLAGS[@]}" -c "$s" -o "$o" ;;
      esac
      objs+=("$o")
    done

    local base=$(( 0xFFFFFFFF82200000 + slot * 0x100000 ))
    elf="$BUILD/modules/$name.elf"
    ld -T "$ROOT/Kernel/linker/module.ld" -defsym AX_MOD_BASE=$base \
       -z max-page-size=0x1000 --no-dynamic-linker -o "$elf" "${objs[@]}"
    out="$BUILD/modules/$name.axmod"
    "$TOOLS/mkmod" "$elf" "$out" "$name" "$slot"
    info "$(basename "$out") (ranura $slot)"
    slot=$((slot+1))
  done
  ok "$slot modulo(s)"
}

# ---------------------------------------------------------------------------
# 5. Biblioteca de API de usuario (libaxos.a)
# ---------------------------------------------------------------------------
build_lib() {
  step "Biblioteca de usuario (libaxos.a)"
  need g++
  rm -rf "$OBJ/lib"; mkdir -p "$OBJ/lib"
  local src obj objs=()
  for src in "$ROOT"/apis/src/*.cpp; do
    [ -e "$src" ] || continue
    obj="$OBJ/lib/$(basename "$src" .cpp).o"
    g++ "${USER_CFLAGS[@]}" -c "$src" -o "$obj"
    objs+=("$obj")
  done
  for src in "$ROOT"/apis/src/*.S; do
    [ -e "$src" ] || continue
    obj="$OBJ/lib/$(basename "$src" .S).o"
    as --64 -o "$obj" "$src"
    objs+=("$obj")
  done
  [ ${#objs[@]} -eq 0 ] && { warn "apis/src vacio"; return 0; }
  ar rcs "$BUILD/lib/libaxos.a" "${objs[@]}"
  ok "build/lib/libaxos.a"

  # Formato de distribucion .axl (cabecera + archivo ar + CRC32).
  if [ -x "$TOOLS/mkaxl" ]; then
    "$TOOLS/mkaxl" "$BUILD/lib/libaxos.a" "$BUILD/lib/libaxos.axl" libaxos
    ok "build/lib/libaxos.axl"
  else
    warn "falta build/tools/mkaxl: no se genera libaxos.axl"
  fi
}

# ---------------------------------------------------------------------------
# 6. Aplicaciones nativas (ELF PIE) y Axolyn (.axb)
# ---------------------------------------------------------------------------
build_apps() {
  step "Aplicaciones nativas"
  need g++
  rm -rf "$OBJ/apps"; mkdir -p "$OBJ/apps"

  # Enlazamos contra la biblioteca extraida de libaxos.axl.  Asi el .axl no
  # es solo un artefacto de distribucion: es la entrada real del enlazador.
  local lib="$BUILD/lib/libaxos.a"
  if [ -x "$TOOLS/axl2a" ] && [ -f "$BUILD/lib/libaxos.axl" ]; then
    "$TOOLS/axl2a" "$BUILD/lib/libaxos.axl" "$OBJ/lib/libaxos.from_axl.a"
    lib="$OBJ/lib/libaxos.from_axl.a"
    info "enlazando contra libaxos.axl"
  fi
  if [ ! -f "$lib" ]; then
    die "falta $lib (ejecuta antes: bash scripts/build.sh lib)"
  fi

  local src name
  for src in "$ROOT"/apps/native/*.cpp; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .cpp)"
    g++ "${USER_CFLAGS[@]}" -c "$src" -o "$OBJ/apps/${name}_main.o"
    g++ -fPIE -pie -nostdlib -Wl,--build-id=none -Wl,--no-dynamic-linker \
        -Wl,-T,"$ROOT/apis/linker/userspace.ld" \
        -Wl,--undefined=_start \
        -o "$BUILD/apps/$name.elf" "$OBJ/apps/${name}_main.o" "$lib"
    info "$name.elf"

    # Paquete .axe (cabecera + ELF64 + CRC32).
    if [ -x "$TOOLS/mkaxe" ]; then
      "$TOOLS/mkaxe" "$BUILD/apps/$name.elf" "$BUILD/apps/$name.axe" "$name"
    fi
  done
  ok "aplicaciones nativas (.elf + .axe)"

  step "Aplicaciones Axolyn (bytecode)"
  if [ ! -x "$BUILD/axc" ]; then
    warn "el compilador axc no esta construido: se omiten las apps .ax"
    return 0
  fi
  for src in "$ROOT"/apps/axolyn/*.ax; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .ax)"
    "$BUILD/axc" "$src" -o "$BUILD/apps/$name.axb" || warn "fallo compilando $src"
  done
  ok "aplicaciones bytecode"
}

# ---------------------------------------------------------------------------
# 7. Initramfs (AXFS)
# ---------------------------------------------------------------------------
build_initrd() {
  step "Initramfs (AXFS)"
  if [ ! -x "$TOOLS/mkinitramfs" ]; then
    warn "falta build/tools/mkinitramfs"; return 0
  fi
  local stage="$BUILD/disk/root"
  rm -rf "$stage"; mkdir -p "$stage/apps" "$stage/modules" "$stage/etc" "$stage/lib" "$stage/help"
  cp -f "$BUILD/apps"/*.elf "$stage/apps/" 2>/dev/null || true
  cp -f "$BUILD/apps"/*.axb "$stage/apps/" 2>/dev/null || true
  cp -f "$BUILD/apps"/*.axe "$stage/apps/" 2>/dev/null || true
  cp -f "$BUILD/lib"/*.axl "$stage/lib/" 2>/dev/null || true
  cp -f "$ROOT"/apps/help/*.axh "$stage/help/" 2>/dev/null || true
  cp -f "$BUILD/modules"/*.axmod "$stage/modules/" 2>/dev/null || true
  cp -f "$ROOT"/apps/axolyn/*.ax "$stage/apps/" 2>/dev/null || true
  cat > "$stage/etc/motd" <<'EOF'
AxolynOS - sistema experimental de microkernel.
Escribe "help" para ver los comandos disponibles.
EOF
  "$TOOLS/mkinitramfs" "$stage" "$BUILD/initrd.axfs"
  ok "build/initrd.axfs ($(stat -c%s "$BUILD/initrd.axfs") bytes)"
}

# ---------------------------------------------------------------------------
# 8. Imagen de disco
# ---------------------------------------------------------------------------
build_image() {
  step "Imagen de disco"
  if [ ! -x "$TOOLS/mkdisk" ]; then warn "falta build/tools/mkdisk"; return 0; fi
  if [ ! -f "$BUILD/stage1.bin" ] || [ ! -f "$BUILD/stage2.bin" ]; then
    warn "falta el bootloader (build/stage1.bin, build/stage2.bin)"; return 0
  fi
  if [ ! -f "$BUILD/axolyn.elf" ]; then warn "falta build/axolyn.elf"; return 0; fi
  if [ ! -f "$BUILD/initrd.axfs" ]; then warn "falta build/initrd.axfs"; return 0; fi
  "$TOOLS/mkdisk" --stage1 "$BUILD/stage1.bin" --stage2 "$BUILD/stage2.bin" \
      --kernel "$BUILD/axolyn.elf" --initrd "$BUILD/initrd.axfs" \
      --out "$BUILD/axolynos.img"
  ok "build/axolynos.img ($(stat -c%s "$BUILD/axolynos.img") bytes)"
}

build_bootloader() {
  step "Bootloader (NASM)"
  need nasm
  nasm -f bin "$ROOT/Kernel/bootloader/stage1.asm" -o "$BUILD/stage1.bin"
  nasm -f bin "$ROOT/Kernel/bootloader/stage2.asm" -o "$BUILD/stage2.bin"
  local s1 s2
  s1=$(stat -c%s "$BUILD/stage1.bin"); s2=$(stat -c%s "$BUILD/stage2.bin")
  [ "$s1" -eq 512 ] || die "stage1.bin debe ocupar 512 bytes exactos (ocupa $s1)"
  [ "$s2" -le 32256 ] || die "stage2.bin no cabe en 63 sectores (ocupa $s2)"
  ok "stage1.bin (512 B) y stage2.bin ($s2 B)"
}

# ---------------------------------------------------------------------------
# Programa principal
# ---------------------------------------------------------------------------
mkdirs

build_uefi() {
  step "Bootloader UEFI (BOOTX64.EFI)"
  bash "$ROOT/scripts/build_uefi.sh"
}

do_iso() {
  step "Generando imagen ISO híbrida BIOS + UEFI"
  bash "$ROOT/scripts/mkiso.sh"
}

case "$TARGET" in
  all)
    build_bootloader
    build_tools
    build_compiler
    build_kernel
    build_modules
    build_lib
    build_apps
    build_initrd
    build_image
    build_uefi
    do_iso
    ;;
  bootloader) build_bootloader ;;
  tools)      build_tools ;;
  compiler)   build_compiler ;;
  kernel)     build_kernel ;;
  modules)    build_modules ;;
  lib)        build_lib ;;
  apps)       build_lib; build_apps ;;
  initrd)     build_initrd ;;
  image)      build_image ;;
  uefi)       build_uefi ;;
  bios)       build_bootloader; build_image ;;
  iso)
    build_bootloader
    build_tools
    build_compiler
    build_kernel
    build_modules
    build_lib
    build_apps
    build_initrd
    build_image
    build_uefi
    do_iso
    ;;
  clean)      rm -rf "$BUILD"; ok "build/ borrado" ;;
  *)          die "objetivo desconocido: $TARGET (usa: all bootloader tools compiler kernel modules lib apps initrd image iso uefi bios clean)" ;;
esac

echo
ok "AxolynOS: objetivo '$TARGET' completado"
