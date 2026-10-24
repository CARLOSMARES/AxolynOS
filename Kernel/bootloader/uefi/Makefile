ARCH     := x86_64
TARGET   := BOOTX64.EFI

EFIINC   := /usr/include/efi
EFIINCS  := -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol

LIB      := /usr/lib
EFILIB   := $(LIB)

CFLAGS   := -O2 -fpic -fshort-wchar -fno-stack-protector -mno-red-zone -Wall -Wextra -DGNU_EFI_USE_MS_ABI
LDFLAGS  := -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB)
LIBS     := -lefi -lgnuefi

ifeq ($(wildcard $(EFILIB)/elf_$(ARCH)_efi.lds),)
  EFILIB   := /usr/lib/x86_64-linux-gnu
  LIB      := /usr/lib/x86_64-linux-gnu
  LDFLAGS  := -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB)
endif

CRT0 := $(EFILIB)/crt0-efi-$(ARCH).o

all: $(TARGET)

boot.o: boot.c bootinfo.h elf.h
	gcc $(CFLAGS) $(EFIINCS) -c $< -o $@

$(TARGET): boot.o
	ld $(LDFLAGS) $(CRT0) $< -o $@ $(LIBS)
	strip --strip-unneeded $@ 2>/dev/null || true

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
