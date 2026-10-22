#!/usr/bin/env bash
#
# AxolynOS - Limpieza de los artefactos de compilacion.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rm -rf "$ROOT/build"
echo "[ ok ] build/ borrado"
