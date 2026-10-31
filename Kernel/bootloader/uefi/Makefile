ARCH     := x86_64
TARGET   := BOOTX64.EFI
SO       := BOOTX64.so

EFIINC   := /usr/include/efi
EFIINCS  := -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol

LIB      := /usr/lib
EFILIB   := $(LIB)

# OJO: NO definir GNU_EFI_USE_MS_ABI.  La libgnuefi del sistema esta
# compilada en modo SysV: su _entry llama a efi_main con los argumentos en
# RDI/RSI.  Si activamos MS ABI, efi_main los leeria de RCX/RDX (basura) y
# HandleProtocol(image, ...) fallaria con EFI_INVALID_PARAMETER.
CFLAGS   := -O2 -fpic -fshort-wchar -fno-stack-protector -mno-red-zone -Wall -Wextra
LDFLAGS  := -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB)
LIBS     := -lefi -lgnuefi

ifeq ($(wildcard $(EFILIB)/elf_$(ARCH)_efi.lds),)
  EFILIB   := /usr/lib/x86_64-linux-gnu
  LIB      := /usr/lib/x86_64-linux-gnu
  LDFLAGS  := -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB)
endif

CRT0 := $(EFILIB)/crt0-efi-$(ARCH).o

#
# El firmware UEFI NO ejecuta ELF: lo que se copia en la particion EFI tiene
# que ser un ejecutable PE32+ con subsistema EFI (10).  Los pasos son:
#   1. ld con el script elf_x86_64_efi.lds y crt0-efi -> objeto compartido ELF.
#   2. scripts/elf2efi.py -> PE32+ que entiende OVMF/VirtualBox.  El binutils
#      de Ubuntu no trae el objetivo "efi-app-x86_64", de ahi el conversor.
#
all: $(TARGET)

boot.o: boot.c bootinfo.h elf.h
	gcc $(CFLAGS) $(EFIINCS) -c $< -o $@

$(SO): boot.o
	ld $(LDFLAGS) $(CRT0) $< -o $@ $(LIBS)

$(TARGET): $(SO)
	python3 $(CURDIR)/../../../tools/elf2efi.py $< $@
	@file $@ || true

clean:
	rm -f *.o $(SO) $(TARGET)

.PHONY: all clean
