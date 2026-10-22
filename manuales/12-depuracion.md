# Depuracion

## 1. Depurar el kernel con GDB y QEMU

```bash
bash scripts/build.sh kernel
bash scripts/run.sh --debug
```

QEMU queda detenido esperando GDB en `127.0.0.1:1234`. En otra terminal:

```gdb
(gdb) symbol-file build/axolyn.elf
(gdb) target remote :1234
(gdb) break ax_panic
(gdb) break ax_syscall_dispatch
(gdb) continue
```

Comandos utiles:

| GDB | Para que sirve |
|---|---|
| `x/16i $rip` | desensambla la instruccion actual |
| `info registers` | volcado de registros |
| `x/8gx $rsp` | pila actual |
| `p/x ax_kernel_stack_top` | pila del kernel activa |
| `p/x *(uint64_t*)$rdi` | primer argumento |
| `bt` | traza (el kernel no tiene simbolos de stack por defecto) |

En QEMU, `Ctrl+A X` termina la maquina; `Ctrl+A C` abre la consola de
monitor de QEMU (util para `info registers` y `xp`).

### Parches en caliente

```bash
# recompilar el kernel y reiniciar
bash scripts/build.sh kernel
```

### Volcado de memoria con QEMU

```bash
qemu-system-x86_64 -drive format=raw,file=build/axolynos.img \
    -m 512 -display none -serial stdio -d guest_errors -D build/qemu.log
```

## 2. Depurar el compilador Axolyn

```bash
build/axc apps/axolyn/hello.ax --dump-ast       # arbol sintactico
build/axc apps/axolyn/hello.ax --dump-bytecode   # bytecode generado
build/axrun build/apps/hello.axb                 # ejecuta el bytecode
```

El runner de host (`axrun`) es la herramienta mas rapida para depurar el
lenguaje: no hace falta arrancar el sistema.

Si el programa falla, `axrun` imprime el mensaje de error de la VM
(instruccion, linea y fichero) y devuelve un codigo distinto de cero.

## 3. Depurar una aplicacion nativa

Las aplicaciones nativas se ejecutan en ring 3, asi que se pueden
depurar igual que el kernel, con la diferencia de que hay dos pilas.

```gdb
(gdb) break ax_syscall_entry
(gdb) info threads
```

El simbolo `ax_syscall_entry` es el punto de entrada de todas las
llamadas al sistema: es el mejor punto para ver que esta pasando entre
la aplicacion y el kernel.

Truco: como el kernel es de un solo anillo compartido, un `break` en una
funcion del kernel se dispara tanto desde el kernel como desde una
aplicacion.

## 4. Registro del sistema

El kernel escribe en la consola VGA **y** en el puerto serie COM1. La
salida por serie es la mas comoda para depurar:

```bash
bash scripts/run.sh --headless
```

Para guardar el log a fichero:

```bash
qemu-system-x86_64 -drive format=raw,file=build/axolynos.img \
    -m 512 -display none -no-reboot -serial file:build/serial.log
```

## 5. Panicos

Cuando el kernel detecta un error llama a `ax_panic()`, que imprime:

1. El fichero y la linea.
2. El mensaje.
3. Un volcado de los registros (si el panic viene de una excepcion).

Ejemplo:

```
*** PANIC: Kernel/src/vmm.cpp:142: asercion fallida: (flags & AX_PAGE_PRESENT)
    rax=0000000000000000 rbx=... rip=...
```

El sistema se detiene: es preferible a seguir con memoria corrupta.

### Errores frecuentes y su causa

| Panic tipico | Causa probable |
|---|---|
| `#GP` con `rip` en el codigo del kernel | falta el bit de usuario (U/S) en una pagina |
| `#PF` con `cr2` alto (`0xFFFF...`) | se toco la ventana del kernel desde user mode |
| `#PF` durante `ax_vmm_map` | las tablas de pagina no estan mapeadas todavia |
| panic en `ax_pmm_alloc_pages` | se pidio mas memoria de la que hay |
| panic en `ax_module_load` | cabecera `.axmod` incorrecta (comprueba el magic) |

## 6. Comprobar el estado del sistema

Desde el shell del kernel:

```
mem        informacion de memoria
heap       uso del heap
threads    hilos y estados
mods       modulos cargados
apps       ficheros del initrd
vm         maquinas virtuales
uptime     tiempo desde el arranque
```

## 7. Prueba automatica del arranque

```bash
bash scripts/test.sh boot
```

Arranca QEMU con el log serie en un fichero, espera 25 segundos y
comprueba que aparece el banner de AxolynOS. Es la forma mas rapida de
detectar que una modificación ha roto el arranque.

## 8. Instrumentar el codigo

El kernel tiene una funcion de impresion formateada:

```cpp
ax_printf("cargados %d modulos en %llu bytes\n", count, bytes);
```

Formatos soportados: `%s`, `%d`, `%i`, `%u`, `%x`, `%X`, `%p`, `%c`, `%%`,
`%lld`, `%llu`.

Para depurar el planificador:

```cpp
ax_printf("hilo %s -> %s\n", actual->name, siguiente->name);
```
