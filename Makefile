# AxolynOS - Makefile principal
#
# Compila el sistema operativo completo sin scripts intermedios:
# bootloader BIOS (NASM), kernel C++ freestanding, modulos, biblioteca de
# usuario, aplicaciones, initramfs, imagen de disco e ISO hibrida BIOS+UEFI.
#
#   make            compila todo y genera build/AxolynOS.iso
#   make kernel     solo build/axolyn.elf
#   make iso        ISO hibrida BIOS + UEFI
#   make run        arranca la ISO en QEMU
#   make run-disk   arranca el disco instalado
#   make clean      borra build/
#
# Requiere: gcc, g++, as, ld, ar, nasm, python3, xorriso, dosfstools y mtools.
# Para UEFI: gnu-efi.

SHELL := /bin/bash
.DEFAULT_GOAL := all

ROOT  := $(CURDIR)
BUILD := $(ROOT)/build
OBJ   := $(BUILD)/obj
TOOLS := $(BUILD)/tools

CC       ?= gcc
CXX      ?= g++
AS       ?= as
LD       ?= ld
AR       ?= ar
NASM     ?= nasm
PYTHON   ?= python3
XORRISO  ?= xorriso
QEMU     ?= qemu-system-x86_64

# --------------------------------------------------------------------------
# Banderas
# --------------------------------------------------------------------------
KERNEL_CXXFLAGS := -std=gnu++17 -O2 -g \
	-ffreestanding -nostdlib -fno-exceptions -fno-rtti \
	-mno-red-zone -mcmodel=kernel -fno-pic -fno-pie \
	-fno-stack-protector -fno-asynchronous-unwind-tables \
	-Wall -Wextra -Wno-unused-parameter \
	-I $(ROOT)/Kernel/include -I $(ROOT)/Kernel/vm -I $(ROOT)/apis/include

KERNEL_LDFLAGS := -T $(ROOT)/Kernel/linker/kernel.ld \
	-z max-page-size=0x1000 --no-dynamic-linker

USER_CXXFLAGS := -std=gnu++17 -O2 -g \
	-ffreestanding -nostdlib -fno-exceptions -fno-rtti \
	-mno-red-zone -fPIE -fno-stack-protector \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter \
	-I $(ROOT)/apis/include

HOST_CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

USER_LDFLAGS := -fPIE -pie -nostdlib -Wl,--build-id=none \
	-Wl,--no-dynamic-linker -Wl,-T,$(ROOT)/apis/linker/userspace.ld \
	-Wl,--undefined=_start

# --------------------------------------------------------------------------
# Fuentes y objetos
# --------------------------------------------------------------------------
KERNEL_CXX_SRCS := $(wildcard Kernel/src/*.cpp) $(wildcard Kernel/vm/*.cpp)
KERNEL_ASM_SRCS := $(wildcard Kernel/src/*.S)
KERNEL_OBJS := $(patsubst %.cpp,$(OBJ)/kernel/%.o,$(KERNEL_CXX_SRCS)) \
               $(patsubst %.S,$(OBJ)/kernel/%.o,$(KERNEL_ASM_SRCS))

TOOL_SRCS := $(wildcard tools/*.cpp)
TOOL_BINS := $(patsubst tools/%.cpp,$(TOOLS)/%,$(TOOL_SRCS))

AXC_SRCS := $(filter-out compiladores/axc/src/axrun.cpp,$(wildcard compiladores/axc/src/*.cpp))
AXC_OBJS := $(patsubst %.cpp,$(OBJ)/axc/%.o,$(AXC_SRCS))

LIB_CXX_SRCS := $(wildcard apis/src/*.cpp)
LIB_ASM_SRCS := $(wildcard apis/src/*.S)
LIB_OBJS := $(patsubst %.cpp,$(OBJ)/lib/%.o,$(LIB_CXX_SRCS)) \
            $(patsubst %.S,$(OBJ)/lib/%.o,$(LIB_ASM_SRCS))

APP_SRCS := $(wildcard apps/native/*.cpp)
APP_NAMES := $(patsubst apps/native/%.cpp,%,$(APP_SRCS))
APP_ELFS := $(patsubst %,apps/%.elf,$(APP_NAMES))
APP_ELFS := $(addprefix $(BUILD)/,$(APP_ELFS))
APP_AXES := $(APP_ELFS:.elf=.axe)
APP_AXB := $(patsubst apps/axolyn/%.ax,$(BUILD)/apps/%.axb,$(wildcard apps/axolyn/*.ax))

MODULE_NAMES := $(notdir $(wildcard Kernel/modules/*))

# Bases de carga de los modulos (una ranura de 1 MiB por modulo).
MODULE_BASE_0 := 0xFFFFFFFF82200000
MODULE_BASE_1 := 0xFFFFFFFF82300000
MODULE_BASE_2 := 0xFFFFFFFF82400000
MODULE_BASE_3 := 0xFFFFFFFF82500000
MODULE_BASE_4 := 0xFFFFFFFF82600000
MODULE_BASE_5 := 0xFFFFFFFF82700000
MODULE_BASE_6 := 0xFFFFFFFF82800000
MODULE_BASE_7 := 0xFFFFFFFF82900000

MODULE_ELF :=
MODULE_AXMOD :=
mod_slot := 0

define MODULE_RULES
MODULE_ELF += $(BUILD)/modules/$(1).elf
MODULE_AXMOD += $(BUILD)/modules/$(1).axmod

$(BUILD)/modules/$(1).elf: $$(patsubst Kernel/modules/$(1)/%.cpp,$$(OBJ)/mod/$(1)/%.o,$$(wildcard Kernel/modules/$(1)/*.cpp))
	@mkdir -p $$(BUILD)/modules
	$$(LD) -T $$(ROOT)/Kernel/linker/module.ld -defsym AX_MOD_BASE=$$(MODULE_BASE_$(2)) \
		-z max-page-size=0x1000 --no-dynamic-linker -o $$@ $$^

$(BUILD)/modules/$(1).axmod: $(BUILD)/modules/$(1).elf $$(TOOLS)/mkmod
	$$(TOOLS)/mkmod $$< $$@ $(1) $(2)
endef

$(foreach m,$(MODULE_NAMES),$(eval $(call MODULE_RULES,$(m),$(mod_slot)))$(eval mod_slot := $(shell expr $(mod_slot) + 1)))

# --------------------------------------------------------------------------
# Objetivos de alto nivel
# --------------------------------------------------------------------------
.PHONY: all bootloader tools compiler kernel modules lib apps initrd image uefi iso \
        clean run run-headless run-install run-disk disk help

all: iso

bootloader: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
tools:      $(TOOL_BINS)
compiler:   $(BUILD)/axc $(BUILD)/axrun
kernel:     $(BUILD)/axolyn.elf
modules:    $(MODULE_AXMOD)
lib:        $(BUILD)/lib/libaxos.a $(BUILD)/lib/libaxos.axl
apps:       lib compiler $(APP_ELFS) $(APP_AXES) $(APP_AXB)
initrd:     $(BUILD)/initrd.axfs
image:      $(BUILD)/axolynos.img
uefi:       $(BUILD)/efi/EFI/BOOT/BOOTX64.EFI
iso:        $(BUILD)/AxolynOS.iso

# --------------------------------------------------------------------------
# 1. Bootloader BIOS (NASM, binario plano)
# --------------------------------------------------------------------------
$(BUILD)/stage1.bin: Kernel/bootloader/stage1.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin $< -o $@
	@test $$(stat -c%s $@) -eq 512 || { echo "stage1.bin debe ocupar 512 bytes"; exit 1; }

$(BUILD)/stage2.bin: Kernel/bootloader/stage2.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin $< -o $@
	@test $$(stat -c%s $@) -le 32256 || { echo "stage2.bin no cabe en 63 sectores"; exit 1; }

# --------------------------------------------------------------------------
# 2. Herramientas de host (mkdisk, mkinitramfs, mkmod, mkaxe, mkaxl, axl2a)
# --------------------------------------------------------------------------
$(TOOLS)/%: tools/%.cpp
	@mkdir -p $(TOOLS)
	$(CXX) $(HOST_CXXFLAGS) -I $(ROOT)/Kernel/include -I $(ROOT)/apis/include $< -o $@

# --------------------------------------------------------------------------
# 3. Compilador Axolyn (axc) y runner de host (axrun)
# --------------------------------------------------------------------------
$(OBJ)/axc/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(HOST_CXXFLAGS) -I $(ROOT)/compiladores/axc/include -I $(ROOT)/Kernel/vm -c $< -o $@

$(BUILD)/axc: $(AXC_OBJS)
	$(CXX) $(HOST_CXXFLAGS) -I $(ROOT)/compiladores/axc/include -I $(ROOT)/Kernel/vm $^ -o $@

$(BUILD)/axrun: Kernel/vm/axvm.cpp compiladores/axc/src/axrun.cpp
	$(CXX) $(HOST_CXXFLAGS) -I $(ROOT)/Kernel/vm -I $(ROOT)/compiladores/axc/include $^ -o $@

# --------------------------------------------------------------------------
# 4. Kernel (C++ freestanding + GAS)
# --------------------------------------------------------------------------
$(OBJ)/kernel/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(OBJ)/kernel/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) --64 -o $@ $<

$(BUILD)/axolyn.elf: $(KERNEL_OBJS) Kernel/linker/kernel.ld
	@mkdir -p $(BUILD)
	$(LD) $(KERNEL_LDFLAGS) -Map $(BUILD)/axolyn.elf.map -o $@ $(KERNEL_OBJS)
	@echo "[ok] $@ ($$(stat -c%s $@) bytes)"

# --------------------------------------------------------------------------
# 5. Modulos del kernel (.axmod)
# --------------------------------------------------------------------------
$(OBJ)/mod/%.o: Kernel/modules/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(OBJ)/mod/%.o: Kernel/modules/%.S
	@mkdir -p $(dir $@)
	$(AS) --64 -o $@ $<

# --------------------------------------------------------------------------
# 6. Biblioteca de usuario (libaxos.a + libaxos.axl)
# --------------------------------------------------------------------------
$(OBJ)/lib/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(OBJ)/lib/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) --64 -o $@ $<

$(BUILD)/lib/libaxos.a: $(LIB_OBJS)
	@mkdir -p $(BUILD)/lib
	$(AR) rcs $@ $^

$(BUILD)/lib/libaxos.axl: $(BUILD)/lib/libaxos.a $(TOOLS)/mkaxl
	$(TOOLS)/mkaxl $< $@ libaxos

# --------------------------------------------------------------------------
# 7. Aplicaciones nativas (.elf + .axe) y bytecode (.axb)
# --------------------------------------------------------------------------
$(OBJ)/apps/%_main.o: apps/native/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/apps/%.elf: $(OBJ)/apps/%_main.o $(BUILD)/lib/libaxos.a
	@mkdir -p $(BUILD)/apps
	$(CXX) $(USER_LDFLAGS) -o $@ $< $(BUILD)/lib/libaxos.a

$(BUILD)/apps/%.axe: $(BUILD)/apps/%.elf $(TOOLS)/mkaxe
	$(TOOLS)/mkaxe $< $@ $*

$(BUILD)/apps/%.axb: apps/axolyn/%.ax $(BUILD)/axc
	@mkdir -p $(BUILD)/apps
	$(BUILD)/axc $< -o $@

# --------------------------------------------------------------------------
# 8. Initramfs (AXFS)
# --------------------------------------------------------------------------
$(BUILD)/initrd.axfs: $(APP_ELFS) $(APP_AXES) $(APP_AXB) $(MODULE_AXMOD) $(BUILD)/lib/libaxos.axl $(TOOLS)/mkinitramfs $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/axolyn.elf
	@rm -rf $(BUILD)/disk/root
	@mkdir -p $(BUILD)/disk/root/apps $(BUILD)/disk/root/modules \
	          $(BUILD)/disk/root/etc $(BUILD)/disk/root/lib \
	          $(BUILD)/disk/root/help $(BUILD)/disk/root/install
	@cp -f $(BUILD)/apps/*.elf $(BUILD)/disk/root/apps/ 2>/dev/null || true
	@cp -f $(BUILD)/apps/*.axb $(BUILD)/disk/root/apps/ 2>/dev/null || true
	@cp -f $(BUILD)/apps/*.axb $(BUILD)/disk/root/ 2>/dev/null || true
	@cp -f $(BUILD)/apps/*.axe $(BUILD)/disk/root/ 2>/dev/null || true
	@cp -f $(BUILD)/apps/*.axe $(BUILD)/disk/root/apps/ 2>/dev/null || true
	@cp -f $(BUILD)/lib/*.axl $(BUILD)/disk/root/lib/ 2>/dev/null || true
	@cp -f apps/help/*.axh $(BUILD)/disk/root/help/ 2>/dev/null || true
	@cp -f $(BUILD)/modules/*.axmod $(BUILD)/disk/root/modules/ 2>/dev/null || true
	@cp -f apps/axolyn/*.ax $(BUILD)/disk/root/apps/ 2>/dev/null || true
	@cp -f $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/axolyn.elf $(BUILD)/disk/root/install/
	@printf 'AxolynOS - sistema experimental de microkernel.\nEscribe "help" para ver los comandos disponibles.\n' > $(BUILD)/disk/root/etc/motd
	$(TOOLS)/mkinitramfs $(BUILD)/disk/root $@
	@echo "[ok] $@ ($$(stat -c%s $@) bytes)"

# --------------------------------------------------------------------------
# 9. Imagen de disco arrancable (BIOS)
# --------------------------------------------------------------------------
$(BUILD)/axolynos.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/axolyn.elf $(BUILD)/initrd.axfs $(TOOLS)/mkdisk
	$(TOOLS)/mkdisk --stage1 $(BUILD)/stage1.bin --stage2 $(BUILD)/stage2.bin \
		--kernel $(BUILD)/axolyn.elf --initrd $(BUILD)/initrd.axfs \
		--out $@
	@echo "[ok] $@ ($$(stat -c%s $@) bytes)"

# --------------------------------------------------------------------------
# 10. Cargador UEFI (BOOTX64.EFI)
# --------------------------------------------------------------------------
$(BUILD)/efi/EFI/BOOT/BOOTX64.EFI: Kernel/bootloader/uefi/boot.c Kernel/bootloader/uefi/bootinfo.h Kernel/bootloader/uefi/elf.h tools/elf2efi.py $(BUILD)/axolyn.elf $(BUILD)/initrd.axfs
	$(MAKE) -C $(ROOT)/Kernel/bootloader/uefi
	@mkdir -p $(BUILD)/efi/EFI/BOOT $(BUILD)/efi/EFI/AXOLYNOS
	@cp -f $(ROOT)/Kernel/bootloader/uefi/BOOTX64.EFI $(BUILD)/efi/EFI/BOOT/BOOTX64.EFI
	@cp -f $(BUILD)/axolyn.elf $(BUILD)/efi/EFI/AXOLYNOS/axolyn.elf
	@cp -f $(BUILD)/initrd.axfs $(BUILD)/efi/EFI/AXOLYNOS/initrd.axfs
	@echo "[ok] $@"

# --------------------------------------------------------------------------
# 11. ISO hibrida BIOS + UEFI
# --------------------------------------------------------------------------
$(BUILD)/AxolynOS.iso: $(BUILD)/axolynos.img $(BUILD)/efi/EFI/BOOT/BOOTX64.EFI
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/EFI/BOOT $(BUILD)/iso/EFI/AXOLYNOS $(BUILD)/iso/boot
	@cp -f $(BUILD)/axolynos.img $(BUILD)/iso/axolynos.img
	@cp -f $(BUILD)/efi/EFI/BOOT/BOOTX64.EFI $(BUILD)/iso/EFI/BOOT/BOOTX64.EFI
	@cp -f $(BUILD)/efi/EFI/AXOLYNOS/axolyn.elf $(BUILD)/iso/EFI/AXOLYNOS/axolyn.elf
	@cp -f $(BUILD)/efi/EFI/AXOLYNOS/initrd.axfs $(BUILD)/iso/EFI/AXOLYNOS/initrd.axfs
	@rm -f $(BUILD)/efiboot.img
	@dd if=/dev/zero of=$(BUILD)/efiboot.img bs=1K count=4096 status=none
	@mkfs.vfat -F 12 -n AXEFI $(BUILD)/efiboot.img >/dev/null
	@mmd -i $(BUILD)/efiboot.img ::/EFI
	@mmd -i $(BUILD)/efiboot.img ::/EFI/BOOT
	@mmd -i $(BUILD)/efiboot.img ::/EFI/AXOLYNOS
	@mcopy -i $(BUILD)/efiboot.img $(BUILD)/efi/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/
	@mcopy -i $(BUILD)/efiboot.img $(BUILD)/efi/EFI/AXOLYNOS/axolyn.elf ::/EFI/AXOLYNOS/
	@mcopy -i $(BUILD)/efiboot.img $(BUILD)/efi/EFI/AXOLYNOS/initrd.axfs ::/EFI/AXOLYNOS/
	@cp -f $(BUILD)/efiboot.img $(BUILD)/iso/boot/efiboot.img
	@rm -f $@
	$(XORRISO) -as mkisofs -iso-level 3 -full-iso9660-filenames -joliet \
		-rational-rock -volid AXOLYNOS -appid "AxolynOS Hybrid BIOS+UEFI" \
		-publisher "AxolynOS Project" -preparer "AxolynOS Build" \
		-eltorito-boot axolynos.img -hard-disk-boot \
		-eltorito-alt-boot -e boot/efiboot.img -no-emul-boot \
		-isohybrid-gpt-basdat -append_partition 2 0xEF $(BUILD)/efiboot.img \
		-o $@ $(BUILD)/iso
	@echo "[ok] $@ ($$(du -h $@ | cut -f1)) BIOS + UEFI"

# --------------------------------------------------------------------------
# Ejecucion en QEMU
# --------------------------------------------------------------------------
QEMU_COMMON := -m 512 -cpu qemu64 -no-reboot
DISK := $(BUILD)/install-disk.img

run: $(BUILD)/AxolynOS.iso
	$(QEMU) $(QEMU_COMMON) -cdrom $(BUILD)/AxolynOS.iso -boot d -serial stdio

run-headless: $(BUILD)/AxolynOS.iso
	$(QEMU) $(QEMU_COMMON) -cdrom $(BUILD)/AxolynOS.iso -boot d -display none -serial stdio

# Arranca la ISO con un disco virgen conectado: dentro, crea tu usuario,
# ejecuta "disks" e "install 0", y apaga con "poweroff".
$(DISK):
	@mkdir -p $(BUILD)
	truncate -s 256M $@

# Crea (si falta) el disco virgen de instalacion.
disk: $(DISK)

run-install: $(BUILD)/AxolynOS.iso $(DISK)
	$(QEMU) $(QEMU_COMMON) -cdrom $(BUILD)/AxolynOS.iso -boot d -serial stdio \
		-drive format=raw,file=$(DISK),if=ide,index=0,media=disk

# Arranca el disco donde se instalo el sistema.
run-disk: $(DISK)
	$(QEMU) $(QEMU_COMMON) -serial stdio \
		-drive format=raw,file=$(DISK),if=ide,index=0,media=disk

# --------------------------------------------------------------------------
# Limpieza y ayuda
# --------------------------------------------------------------------------
clean:
	rm -rf $(BUILD)
	@echo "build/ borrado"

help:
	@echo "AxolynOS - objetivos disponibles:"
	@echo "  all            compila todo y genera la ISO (por defecto)"
	@echo "  bootloader     stage1.bin + stage2.bin (NASM)"
	@echo "  tools          herramientas de host (mkdisk, mkinitramfs, ...)"
	@echo "  compiler       axc + axrun"
	@echo "  kernel         build/axolyn.elf"
	@echo "  modules        build/modules/*.axmod"
	@echo "  lib            build/lib/libaxos.a y .axl"
	@echo "  apps           aplicaciones nativas y bytecode"
	@echo "  initrd         build/initrd.axfs"
	@echo "  image          build/axolynos.img"
	@echo "  uefi           BOOTX64.EFI"
	@echo "  iso            build/AxolynOS.iso (BIOS + UEFI)"
	@echo "  run            arranca la ISO en QEMU"
	@echo "  run-headless   arranca la ISO sin ventana (serie en la terminal)"
	@echo "  run-install    ISO + disco virgen para instalar"
	@echo "  run-disk       arranca el disco ya instalado"
	@echo "  disk           crea build/install-disk.img (256 MiB)"
	@echo "  clean          borra build/"
