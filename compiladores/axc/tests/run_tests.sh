#!/usr/bin/env bash
#
# AxolynOS - Pruebas del compilador Axolyn (axc) y del runner de host (axrun).
#
#   bash compiladores/axc/tests/run_tests.sh
#
# Para cada programa de tests/ se comprueba que:
#   1. 'axc' lo compila sin errores,
#   2. 'axrun' lo ejecuta y la salida coincide con la esperada (.expected),
#   3. los programas de tests/errors/ NO compilan (el compilador falla).
#
set -uo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
TESTS="$DIR"
BUILD="$ROOT/build"
AXC="$BUILD/axc"
AXRUN="$BUILD/axrun"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'; NC='\033[0m'
step() { echo -e "${CYN}==>${NC} $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
bad()  { echo -e "${RED}[FAIL]${NC} $*"; FAILED=$((FAILED + 1)); }
warn() { echo -e "${YEL}[warn]${NC} $*"; }

FAILED=0
PASSED=0

# ---------------------------------------------------------------------------
# Construye axc y axrun si no estan.
# ---------------------------------------------------------------------------
build_tools() {
  if [ -x "$AXC" ] && [ -x "$AXRUN" ]; then return 0; fi
  command -v g++ >/dev/null 2>&1 || { warn "g++ no disponible: no se puede construir"; return 1; }
  step "Construyendo axc y axrun"
  mkdir -p "$BUILD"
  local objs=() src obj
  for src in "$ROOT"/compiladores/axc/src/*.cpp; do
    [ -e "$src" ] || continue
    case "$(basename "$src")" in
      axrun.cpp) continue ;;
    esac
    obj="$BUILD/obj/axc/$(basename "$src" .cpp).o"
    mkdir -p "$(dirname "$obj")"
    g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
        -I "$ROOT/compiladores/axc/include" -I "$ROOT/Kernel/vm" \
        -c "$src" -o "$obj" || return 1
    objs+=("$obj")
  done
  g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
      -I "$ROOT/compiladores/axc/include" -I "$ROOT/Kernel/vm" \
      "${objs[@]}" -o "$AXC" || return 1
  g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
      -I "$ROOT/Kernel/vm" \
      "$ROOT/Kernel/vm/axvm.cpp" "$ROOT/compiladores/axc/src/axrun.cpp" \
      -o "$AXRUN" || return 1
  ok "axc y axrun construidos"
  return 0
}

build_tools || { echo "no se puede continuar sin el compilador"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---------------------------------------------------------------------------
# 1. Programas que deben funcionar
# ---------------------------------------------------------------------------
step "Programas correctos"

for src in "$TESTS"/*.ax; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .ax)"
  expected="$TESTS/$name.expected"
  bin="$TMP/$name.axb"
  out="$TMP/$name.out"

  if [ ! -f "$expected" ]; then
    bad "$name: falta el fichero esperado '$expected'"
    continue
  fi

  if ! "$AXC" "$src" -o "$bin" >"$TMP/$name.compile" 2>&1; then
    bad "$name: axc no pudo compilar el programa"
    sed 's/^/        /' "$TMP/$name.compile"
    continue
  fi

  if ! "$AXRUN" "$bin" >"$out" 2>"$TMP/$name.err"; then
    bad "$name: axrun termino con error"
    sed 's/^/        /' "$TMP/$name.err"
    continue
  fi

  if diff -u "$expected" "$out" >"$TMP/$name.diff" 2>&1; then
    ok "$name"
    PASSED=$((PASSED + 1))
  else
    bad "$name: la salida no coincide con la esperada"
    sed 's/^/        /' "$TMP/$name.diff"
  fi
done

# ---------------------------------------------------------------------------
# 2. Programas que NO deben compilar
# ---------------------------------------------------------------------------
step "Programas con errores (deben fallar)"

if [ -d "$TESTS/errors" ]; then
  for src in "$TESTS"/errors/*.ax; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .ax)"

    if "$AXC" "$src" -o "$TMP/error.axb" >"$TMP/$name.compile" 2>&1; then
      bad "errors/$name: axc deberia haber fallado y no lo hizo"
    else
      if grep -qE ':[0-9]+:[0-9]+:' "$TMP/$name.compile"; then
        ok "errors/$name ($(grep -cE ':[0-9]+:[0-9]+:' "$TMP/$name.compile") diagnostico(s))"
        PASSED=$((PASSED + 1))
      else
        bad "errors/$name: fallo sin indicar linea y columna"
        sed 's/^/        /' "$TMP/$name.compile"
      fi
    fi
  done
fi

# ---------------------------------------------------------------------------
# Resumen
# ---------------------------------------------------------------------------
echo
if [ "$FAILED" -eq 0 ]; then
  echo -e "${GRN}${PASSED} prueba(s) superadas, 0 fallidas${NC}"
  echo "TODOS LOS TESTS OK"
  exit 0
fi

echo -e "${RED}${PASSED} prueba(s) superadas, ${FAILED} fallida(s)${NC}"
echo "HAY TESTS FALLIDOS"
exit 1
