#!/usr/bin/env bash
#
# AxolynOS - Bateria de pruebas.
#
#   bash scripts/test.sh            # todo
#   bash scripts/test.sh compiler   # solo las pruebas del compilador Axolyn
#   bash scripts/test.sh kernel     # solo compilacion del kernel
#   bash scripts/test.sh boot       # prueba de arranque en QEMU
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; YEL='\033[0;33m'; NC='\033[0m'
step() { echo -e "${CYN}==>${NC} $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; FAILED=1; }
warn() { echo -e "${YEL}[warn]${NC} $*"; }

FAILED=0
TARGET="${1:-all}"

# ---------------------------------------------------------------------------
test_compiler() {
  step "Compilador Axolyn"
  if [ ! -x "$BUILD/axc" ]; then
    fail "build/axc no existe (ejecuta scripts/build.sh compiler)"
    return
  fi
  if [ -x "$ROOT/compiladores/axc/tests/run_tests.sh" ]; then
    if bash "$ROOT/compiladores/axc/tests/run_tests.sh"; then
      ok "pruebas del compilador"
    else
      fail "pruebas del compilador"
    fi
  else
    warn "no hay compiladores/axc/tests/run_tests.sh"
  fi

  # Prueba de humo: compilar y ejecutar un programa sencillo
  local tmp; tmp="$(mktemp -d)"
  cat > "$tmp/hola.ax" <<'EOF'
import console;
fn main() {
    console.write("hola desde Axolyn\n");
}
EOF
  if "$BUILD/axc" "$tmp/hola.ax" -o "$tmp/hola.axb" && [ -x "$BUILD/axrun" ]; then
    local out; out="$("$BUILD/axrun" "$tmp/hola.axb" 2>&1 || true)"
    if echo "$out" | grep -q "hola desde Axolyn"; then
      ok "compilacion y ejecucion de un programa Axolyn"
    else
      fail "la salida no coincide: $out"
    fi
  else
    fail "no se pudo compilar/ejecutar el programa de prueba"
  fi
  rm -rf "$tmp"
}

# ---------------------------------------------------------------------------
test_kernel() {
  step "Compilacion del kernel"
  if bash "$ROOT/scripts/build.sh" kernel >/tmp/axos_kernel_build.log 2>&1; then
    ok "el kernel compila y enlaza"
  else
    fail "fallo al compilar el kernel (ver /tmp/axos_kernel_build.log)"
    tail -30 /tmp/axos_kernel_build.log || true
  fi

  step "Compilacion de la libreria de usuario y las apps"
  if bash "$ROOT/scripts/build.sh" lib apps >/tmp/axos_apps_build.log 2>&1; then
    ok "libaxos y las aplicaciones compilan"
  else
    fail "fallo al compilar las aplicaciones (ver /tmp/axos_apps_build.log)"
    tail -30 /tmp/axos_apps_build.log || true
  fi
}

# ---------------------------------------------------------------------------
test_boot() {
  step "Prueba de arranque en QEMU"
  command -v qemu-system-x86_64 >/dev/null 2>&1 || { warn "QEMU no instalado"; return; }
  if [ ! -f "$BUILD/axolynos.img" ]; then
    fail "no existe build/axolynos.img"
    return
  fi
  local log="$BUILD/serial.log"
  rm -f "$log"
  timeout 25 qemu-system-x86_64 \
      -drive "format=raw,file=$BUILD/axolynos.img" \
      -m 512 -display none -no-reboot \
      -serial "file:$log" >/dev/null 2>&1 || true

  if [ -s "$log" ]; then
    echo "----- ultimas lineas del log serie -----"
    tail -40 "$log"
    echo "----------------------------------------"
    if grep -q "AxolynOS" "$log"; then
      ok "el kernel arranco y se anuncio en la consola serie"
    else
      fail "no se encontro el banner de AxolynOS en el log"
    fi
  else
    fail "el log serie esta vacio: el sistema no arranco"
  fi
}

case "$TARGET" in
  all)      test_compiler; test_kernel; test_boot ;;
  compiler) test_compiler ;;
  kernel)   test_kernel ;;
  boot)     test_boot ;;
  *)        echo "objetivo desconocido: $TARGET"; exit 1 ;;
esac

echo
if [ "$FAILED" -eq 0 ]; then
  ok "TODAS LAS PRUEBAS PASARON"
else
  fail "HAY PRUEBAS FALLIDAS"
  exit 1
fi
