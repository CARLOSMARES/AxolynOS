#!/usr/bin/env bash
#
# AxolynOS - Construccion de la biblioteca de usuario (libaxos) y de las
# aplicaciones nativas de user mode.
#
# Uso (desde la raiz del repositorio, dentro de WSL):
#
#     bash scripts/build_user.sh            # libaxos + todas las apps
#     bash scripts/build_user.sh lib        # solo la biblioteca
#     bash scripts/build_user.sh hello      # solo una aplicacion
#     bash scripts/build_user.sh etexec     # biblioteca ET_EXEC (sin -fPIE)
#
# Toolchain: g++/gcc de WSL (Linux), sin libc, sin excepciones y sin RTTI.
#
# NOTA sobre los directorios de salida: el arbol build/ lo comparten varios
# agentes (kernel, bootloader...), asi que aqui se usa build/user/ para no
# pisar los objetos de los demas.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD="$ROOT/build/user"
OBJ="$BUILD/objs"
APPOBJ="$BUILD/objs/apps"
OUT="$BUILD/apps"
LIB="$BUILD/libaxos.a"

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
AR="${AR:-ar}"

# Flags comunes de compilacion (freestanding: sin STL ni libc del host).
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -ffreestanding -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -I apis/include"

# Las aplicaciones se enlazan como PIE (ELF64 ET_DYN), por lo que los
# objetos deben ser position independent: se anade -fPIE.
PIE="-fPIE"

# Enlazado: PIE estatico sin libc, con el script de user mode.
LDFLAGS="-static-pie -nostdlib -Wl,--build-id=none -Wl,-T,apis/linker/userspace.ld"

LIBSRC=(apis/src/console.cpp apis/src/process.cpp apis/src/fs.cpp apis/src/ipc.cpp \
        apis/src/module.cpp apis/src/vm.cpp apis/src/mem.cpp apis/src/time.cpp \
        apis/src/string.cpp apis/src/runtime.cpp)

APPS=(hello meminfo ls cat calc sysinfo)

mkdir -p "$OBJ" "$APPOBJ" "$OUT"

# --------------------------------------------------------------------
# 1. Biblioteca de usuario
# --------------------------------------------------------------------
build_lib() {
    local extra="${1:-$PIE}"
    local objs=()

    echo "== libaxos =="
    for src in "${LIBSRC[@]}"; do
        local obj="$OBJ/$(basename "$src" .cpp).o"
        echo "  CXX $src $extra"
        $CXX $CXXFLAGS $extra -c "$src" -o "$obj"
        objs+=("$obj")
    done
    echo "  AS  apis/src/start.S"
    $CC -c apis/src/start.S -o "$OBJ/start.o"
    objs+=("$OBJ/start.o")

    rm -f "$LIB"
    $AR rcs "$LIB" "${objs[@]}"
    echo "  -> $LIB"
}

# --------------------------------------------------------------------
# 2. Aplicaciones nativas
# --------------------------------------------------------------------
build_app() {
    local name="$1"
    local src="apps/native/$name.cpp"
    local obj="$APPOBJ/$name.o"

    echo "  CXX $src $PIE"
    $CXX $CXXFLAGS $PIE -c "$src" -o "$obj"

    echo "  LD  $OUT/$name"
    $CXX $LDFLAGS "$obj" "$LIB" -o "$OUT/$name"
}

build_apps() {
    echo "== aplicaciones nativas =="
    for app in "${APPS[@]}"; do
        build_app "$app"
    done
}

case "${1:-all}" in
    lib)    build_lib ;;
    etexec) build_lib "-fno-pic -fno-pie" ;;
    all)    build_lib; build_apps ;;
    *)
        build_lib
        build_app "$1"
        ;;
esac

echo "Listo."
