# AxolynOS - Makefile de conveniencia.
#
# Todo el trabajo real lo hacen los scripts de scripts/; este Makefile
# solo es una fachada comoda.
#
#   make            compila todo (imagen incluida)
#   make kernel     solo el kernel
#   make compiler   solo el compilador Axolyn
#   make apps       solo las aplicaciones
#   make run        arranca en QEMU
#   make test       bateria de pruebas
#   make install    instala las dependencias en Ubuntu/WSL
#   make clean      borra build/

SHELL := /bin/bash
ROOT  := $(shell pwd)

.PHONY: all build bootloader tools compiler kernel modules lib apps initrd image run run-headless test clean install help

all: build

build:
	@bash scripts/build.sh

bootloader:
	@bash scripts/build.sh bootloader

tools:
	@bash scripts/build.sh tools

compiler:
	@bash scripts/build.sh compiler

kernel:
	@bash scripts/build.sh kernel

modules:
	@bash scripts/build.sh modules

lib:
	@bash scripts/build.sh lib

apps:
	@bash scripts/build.sh lib apps

initrd:
	@bash scripts/build.sh initrd

image:
	@bash scripts/build.sh image

run:
	@bash scripts/run.sh

run-headless:
	@bash scripts/run.sh --headless

test:
	@bash scripts/test.sh

clean:
	@bash scripts/build.sh clean

install:
	@sudo bash scripts/install_ubuntu_wsl.sh

help:
	@echo "AxolynOS - objetivos disponibles:"
	@echo "  build       compila kernel, modulos, apps, compilador e imagen"
	@echo "  bootloader  compila stage1/stage2 (NASM)"
	@echo "  tools       compila las herramientas de host"
	@echo "  compiler    compila el compilador Axolyn (axc) y el runner (axrun)"
	@echo "  kernel      compila el kernel"
	@echo "  modules     empaqueta los modulos del kernel (.axmod)"
	@echo "  lib         compila libaxos.a"
	@echo "  apps        compila las aplicaciones nativas y Axolyn"
	@echo "  initrd      genera el initramfs AXFS"
	@echo "  image       genera la imagen de disco"
	@echo "  run         arranca el sistema en QEMU"
	@echo "  test        ejecuta la bateria de pruebas"
	@echo "  install     instala las dependencias (necesita sudo)"
	@echo "  clean       borra build/"
