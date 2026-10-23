#!/usr/bin/env bash
#
# AxolynOS - Instalador de dependencias para Ubuntu en WSL2.
#
# Uso:
#   sudo bash scripts/install_ubuntu_wsl.sh          # instala todo
#   bash scripts/install_ubuntu_wsl.sh --check       # solo comprueba
#   bash scripts/install_ubuntu_wsl.sh --no-qemu     # omite QEMU
#
# El script es idempotente: se puede volver a ejecutar sin romper nada.
set -euo pipefail

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'; NC='\033[0m'

log()  { echo -e "${CYN}[axos]${NC} $*"; }
ok()   { echo -e "${GRN}[ ok ]${NC} $*"; }
warn() { echo -e "${YEL}[warn]${NC} $*"; }
die()  { echo -e "${RED}[fail]${NC} $*" >&2; exit 1; }

CHECK_ONLY=0
INSTALL_QEMU=1
for arg in "$@"; do
  case "$arg" in
    --check)     CHECK_ONLY=1 ;;
    --no-qemu)   INSTALL_QEMU=0 ;;
    -h|--help)   sed -n '2,12p' "$0"; exit 0 ;;
    *) die "opcion desconocida: $arg" ;;
  esac
done

# ---------------------------------------------------------------------------
# 1. Comprobacion del entorno
# ---------------------------------------------------------------------------
log "Comprobando el entorno..."

if [ -r /etc/os-release ]; then
  . /etc/os-release
  log "Distribucion: ${PRETTY_NAME:-desconocida}"
else
  warn "No se pudo leer /etc/os-release"
fi

# ¿Estamos en WSL?
if grep -qi microsoft /proc/version 2>/dev/null; then
  IS_WSL=1
  ok "WSL detectado ($(uname -r))"
  WSL_DISTRO="${WSL_DISTRO_NAME:-Ubuntu}"
else
  IS_WSL=0
  log "No estamos en WSL: se asume una instalacion Linux nativa"
fi

if [ "$(uname -m)" != "x86_64" ]; then
  die "AxolynOS necesita una maquina x86_64 (este host es $(uname -m))"
fi

# ¿Root o con sudo?
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    die "Se necesita root o sudo para instalar paquetes"
  fi
fi

need_cmd() {
  if command -v "$1" >/dev/null 2>&1; then
    ok "$1 -> $(command -v "$1")"
  else
    warn "$1 NO ENCONTRADO"
    return 1
  fi
}

MISSING=0
for c in gcc g++ ld as nasm make git python3 objcopy readelf; do
  need_cmd "$c" || MISSING=1
done
if command -v qemu-system-x86_64 >/dev/null 2>&1; then
  ok "qemu-system-x86_64 -> $(command -v qemu-system-x86_64)"
else
  warn "qemu-system-x86_64 NO ENCONTRADO"
  MISSING=1
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
  [ "$MISSING" -eq 0 ] && ok "Todas las dependencias estan presentes" || warn "Faltan dependencias: ejecuta este script sin --check"
  exit 0
fi

# ---------------------------------------------------------------------------
# 2. Paquetes del sistema
# ---------------------------------------------------------------------------
PKGS_CORE="build-essential binutils nasm make git curl pkg-config python3 file"
PKGS_EXTRA="gdb binutils-multiarch xz-utils cpio rsync"
PKGS_QEMU="qemu-system-x86 qemu-utils"

log "Actualizando indices de apt..."
$SUDO apt-get update -qq

install_pkgs() {
  local list="$1"
  # `env` hace falta cuando SUDO esta vacio (ya somos root): si no, bash
  # interpreta DEBIAN_FRONTEND=... como el nombre del comando.
  # shellcheck disable=SC2086
  $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $list >/dev/null 2>&1 \
    || $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y $list
}

log "Instalando toolchain (gcc, nasm, make, binutils)..."
install_pkgs "$PKGS_CORE"
ok "Toolchain instalada"

log "Instalando utilidades de desarrollo..."
install_pkgs "$PKGS_EXTRA" || warn "Algunas utilidades opcionales no se pudieron instalar"
ok "Utilidades instaladas"

if [ "$INSTALL_QEMU" -eq 1 ]; then
  log "Instalando QEMU para emular x86_64..."
  install_pkgs "$PKGS_QEMU"
  ok "QEMU instalado ($(qemu-system-x86_64 --version | head -1))"
fi

# ---------------------------------------------------------------------------
# 3. Ajustes recomendados para WSL2
# ---------------------------------------------------------------------------
if [ "$IS_WSL" -eq 1 ]; then
  log "Ajustando la configuracion de WSL2..."

  # /etc/wsl.conf: el volumen de metadatos acelera el I/O sobre /mnt/c y
  # systemd permite que los servicios de usuario funcionen.
  #
  # IMPORTANTE: no se sobrescribe el fichero. Se conserva todo lo que el
  # usuario ya tuviera configurado y solo se anaden las claves que falten
  # dentro de un bloque gestionado por este script.
  WSL_CONF=/etc/wsl.conf
  if [ -f "$WSL_CONF" ]; then
    cp "$WSL_CONF" "$WSL_CONF.bak.$(date +%s)" 2>/dev/null || true
  fi

  # Valores actuales (si existen) para no pisarlos.
  cur_automount_enabled="$(grep -E '^\s*enabled\s*=' "$WSL_CONF" 2>/dev/null | head -1 | sed 's/.*=\s*//' || true)"
  cur_automount_options="$(grep -E '^\s*options\s*=' "$WSL_CONF" 2>/dev/null | head -1 | sed 's/.*=\s*//' || true)"
  cur_systemd="$(grep -E '^\s*systemd\s*=' "$WSL_CONF" 2>/dev/null | head -1 | sed 's/.*=\s*//' || true)"

  # Solo se escriben las claves ausentes. `grep -q` evita duplicarlas si el
  # script se ejecuta varias veces (idempotencia).
  managed=""
  if ! grep -q '^\[automount\]' "$WSL_CONF" 2>/dev/null; then
    managed+=$'[automount]\n'
    managed+="enabled=${cur_automount_enabled:-true}"$'\n'
    managed+="options=\"${cur_automount_options:-metadata,umask=22,fmask=11}\""$'\n'
    managed+='mountFsTab=true'$'\n'
  fi
  if ! grep -q '^\[boot\]' "$WSL_CONF" 2>/dev/null; then
    managed+=$'\n[boot]\n'
    managed+="systemd=${cur_systemd:-true}"$'\n'
  fi

  if [ -n "$managed" ]; then
    printf '%s' "$managed" | $SUDO tee -a "$WSL_CONF" >/dev/null
    ok "/etc/wsl.conf ampliado (requiere 'wsl --shutdown' para aplicarse)"
  else
    ok "/etc/wsl.conf ya estaba configurado; no se modifica"
  fi

  # Compilar sobre /mnt/c es lento: avisamos del proyecto en el disco de Linux
  LINUX_HOME="$HOME/projects/AxolynOS"
  if [ ! -d "$LINUX_HOME" ]; then
    log "Sugerencia: para compilaciones rapidas copia el proyecto a $LINUX_HOME"
    log "  cp -r \"$(pwd)\" \"$LINUX_HOME\""
  fi
fi

# ---------------------------------------------------------------------------
# 4. Comprobacion final
# ---------------------------------------------------------------------------
log "Comprobacion final..."
MISSING=0
for c in gcc g++ ld as nasm make; do need_cmd "$c" || MISSING=1; done
command -v qemu-system-x86_64 >/dev/null 2>&1 || { [ "$INSTALL_QEMU" -eq 0 ] || MISSING=1; }

echo
if [ "$MISSING" -eq 0 ]; then
  ok "AxolynOS esta listo para compilarse."
  echo
  echo "  Siguientes pasos:"
  echo "    cd $(pwd)"
  echo "    bash scripts/build.sh          # compila kernel, apps, compilador e imagen"
  echo "    bash scripts/run.sh            # arranca en QEMU"
  echo "    bash scripts/test.sh           # bateria de pruebas"
  echo
else
  warn "Hay dependencias sin instalar. Revisa los avisos anteriores."
  exit 1
fi
