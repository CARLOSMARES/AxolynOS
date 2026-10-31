# Makefile ejemplo para aplicaciones SDK
# Colocar en la raiz del proyecto de aplicacion

CC = gcc
CFLAGS = -O2 -ffreestanding -nostdlib -I$(shell pwd)/apis/include
LDFLAGS = -L$(shell pwd)/build -laxos

TARGET = mi_aplicacion
SOURCES = mi_aplicacion.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	@echo "Ejecutar en QEMU: qemu-system-x86_64 -cdrom build/AxolynOS.iso -boot d -serial stdio"