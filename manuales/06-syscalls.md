# Llamadas al sistema (syscalls)

Las syscalls son la unica forma que tiene una aplicacion en ring 3 de
pedir algo al kernel. El contrato vive en `apis/include/axos/abi.h`, que
es compartido por el kernel (`Kernel/src/syscall.cpp`) y por la libreria
de usuario (`apis/`).

## 1. Mecanismo

```
user mode                          ring 0
--------                          ------
mov rax, AX_SYS_WRITE              int 0x80
mov rdi, 1                         |
mov rsi, buffer                    |  trampolin (isr_stubs.S)
mov rdx, len                       |  guarda registros, cambia a la pila
                                    |  del kernel
                                    v
                                    ax_syscall_entry(nr, args)
                                    |  ax_syscall_dispatch(nr, args)
                                    v
                                    handler de la tabla
                                    |
                                    v
                                    RAX = valor de retorno
```

El vector `0x80` se instala en la IDT con **DPL 3**, que es lo que
permite que el codigo de usuario lo ejecute. El trampolin cambia a la
pila del kernel usando la variable global `ax_kernel_stack_top`, que el
planificador actualiza en cada cambio de contexto.

### Ejemplo: Ejecutar una syscall en assembly

```asm
; Ejemplo: ax_syscall3(AX_SYS_WRITE, 1, buffer, len)
mov rax, AX_SYS_WRITE    ; RAX = numero de syscall
mov rdi, 1               ; RDI = fd (1 = stdout)
mov rsi, buffer          ; RSI = buffer address
mov rdx, len             ; RDX = length
int 0x80                 ; Llamar al kernel
; Resultado: RAX = bytes escritos (o -errno en error)
```

## 2. Convencion de registros

| Registro | Uso |
|---|---|
| `RAX` | numero de syscall (entrada) / valor de retorno (salida) |
| `RDI` | argumento 0 |
| `RSI` | argumento 1 |
| `RDX` | argumento 2 |
| `R10` | argumento 3 |
| `R8` | argumento 4 |
| `R9` | argumento 5 |

`RBX`, `RBP` y `R12`-`R15` se conservan: el trampolin los guarda y los
restaura.

### Ejemplo: Preservar registros en ensamblador

```asm
; Al inicio de handler de syscall
push rbx
push rbp
push r12
push r15
; ... hacer trabajo ...
pop r15
pop r12
pop rbp
pop rbx
; Retornar con valor en RAX
ret
```

## 3. Valores de retorno

* `>= 0`: exito (el significado depende de la llamada).
* `< 0`: error. El valor es uno de los codigos `AX_E*` de
  `apis/include/axos/abi.h`, que ya estan definidos como negativos
  (`AX_ENOENT = -1`, `AX_EINVAL = -3`, ...).

### Ejemplo: Manejo de retorno

```c
int fd = ax_syscall2(AX_SYS_OPEN, (uint64_t)"/apps/hello.axb", AX_O_RDONLY);
if (fd < 0) {
    // Error: fd es negativo, es -errno
    // Puedo convertirlo con: int errno = -fd;
    sdk_print("Error al abrir archivo: %d\n", -fd);
} else {
    // Exito: fd es el descriptor
    char buf[64];
    int n = ax_syscall3(AX_SYS_READ, fd, (uint64_t)buf, 64);
    if (n > 0) {
        // Leidos n bytes
    }
    ax_syscall1(AX_SYS_CLOSE, fd);
}
```

## 4. Tabla completa de syscalls

| Numero | Nombre | Argumentos | Retorno |
|---|---|---|---|
| 0 | `AX_SYS_EXIT` | `code` | no retorna |
| 1 | `AX_SYS_WRITE` | `fd, buf, len` | bytes escritos |
| 2 | `AX_SYS_READ` | `fd, buf, len` | bytes leidos |
| 3 | `AX_SYS_OPEN` | `path, flags` | fd o error |
| 4 | `AX_SYS_CLOSE` | `fd` | 0 o error |
| 5 | `AX_SYS_LSEEK` | `fd, offset, whence` | nueva posicion |
| 6 | `AX_SYS_GETS` | `buf, max` | longitud leida |
| 7 | `AX_SYS_SLEEP` | `ms` | 0 |
| 8 | `AX_SYS_TIME` | - | milisegundos desde el arranque |
| 9 | `AX_SYS_GETPID` | - | pid |
| 10 | `AX_SYS_SPAWN` | `path, argv, argc` | pid del hijo |
| 11 | `AX_SYS_WAIT` | `pid` | codigo de salida |
| 12 | `AX_SYS_IPC_SEND` | `port, buf, len` | 0 o error |
| 13 | `AX_SYS_IPC_RECV` | `port, buf, max` | bytes recibidos |
| 14 | `AX_SYS_MODLIST` | `buf, max` | numero de modulos |
| 15 | `AX_SYS_MODLOAD` | `name` | 0 o error |
| 16 | `AX_SYS_MODUNLOAD` | `index` | 0 o error |
| 17 | `AX_SYS_VMRUN` | `path, argv` | pid de la VM |
| 18 | `AX_SYS_VMSTOP` | `pid` | 0 o error |
| 19 | `AX_SYS_MEMINFO` | `AxMemInfo*` | 0 o error |
| 20 | `AX_SYS_HEAPINFO` | `AxHeapInfo*` | 0 o error |
| 21 | `AX_SYS_REBOOT` | - | no retorna |
| 22 | `AX_SYS_EXITCODE` | `pid` | codigo de salida |
| 23 | `AX_SYS_SYSINFO` | `AxSysInfo*` | 0 o error |
| 24 | `AX_SYS_KILL` | `pid` | 0 o error |
| 25 | `AX_SYS_YIELD` | - | 0 |
| 26 | `AX_SYS_THREADS` | `buf, max` | numero de hilos |
| 27 | `AX_SYS_VMSTATUS` | `AxVmInfo*` | 0 o error |
| 28 | `AX_SYS_POWEROFF` | - | no retorna (apaga la maquina) |
| 29 | `AX_SYS_SUSPEND` | - | 0 o error (suspension ACPI) |
| 30 | `AX_SYS_HALT` | - | no retorna (detiene la CPU) |
| 31 | `AX_SYS_DISKREAD` | `lba, buf, count` | 0 o error |
| 32 | `AX_SYS_DISKWRITE` | `lba, buf, count` | 0 o error |
| 33 | `AX_SYS_SIGNAL` | `sig, accion` | 0 o error |
| 34 | `AX_SYS_SIGSEND` | `pid, sig` | 0 o error |
| 35 | `AX_SYS_SIGPENDING` | `sig` (0 = mascara) | 1/0 o mascara |

### Ejemplo: Usar syscalls basicas

```c
#include <axos/syscall.h>

// Escribir "Hola" en stdout
void write_hello(void) {
    ax_syscall3(AX_SYS_WRITE, 1, (uint64_t)"Hola\n", 5);
}

// Leer de stdin (entrada estandar, fd=0)
void read_stdin(void) {
    char buf[64];
    int n = ax_syscall3(AX_SYS_READ, 0, (uint64_t)buf, 64);
    // buf ahora tiene lo que se leyera (hasta 63 bytes + terminador)
}
```

### Ejemplo: Sleep y Time

```c
// Dormir 1000 ms (1 segundo)
ax_syscall1(AX_SYS_SLEEP, 1000);

// Obtener tiempo desde el arranque
uint64_t uptime = ax_syscall0(AX_SYS_TIME);
// uptime tiene milisegundos desde el arranque del sistema
```

## 5. Codigos de error

| Constante | Valor | Significado |
|---|---|---|
| `AX_OK` | 0 | sin error |
| `AX_ENOENT` | -1 | no existe |
| `AX_EIO` | -2 | error de entrada/salida |
| `AX_EINVAL` | -3 | argumento invalido |
| `AX_ENOSPC` | -4 | sin espacio |
| `AX_EACCES` | -5 | sin permisos |
| `AX_EEXIST` | -6 | ya existe |
| `AX_ENOMEM` | -7 | sin memoria |
| `AX_EBUSY` | -8 | ocupado |
| `AX_EINTR` | -9 | interrumpida |
| `AX_EAGAIN` | -10 | intentelo de nuevo |
| `AX_ENOSYS` | -11 | syscall no implementada |
| `AX_ERANGE` | -12 | fuera de rango |

### Ejemplo: Manejo de errores

```c
int fd = ax_syscall2(AX_SYS_OPEN, (uint64_t"/nonexistent", AX_O_RDONLY);
if (fd < 0) {
    // Manejar error
    int errno = -fd;  // Convertir a errno positivo
    if (errno == AX_ENOENT) {
        sdk_print("El archivo no existe\n");
    } else if (errno == AX_EACCES) {
        sdk_print("No hay permisos\n");
    } else {
        sdk_print("Error desconocido: %d\n", errno);
    }
}
```

## 6. Ficheros

Los descriptores `0`, `1` y `2` son la entrada, salida y error estandar
(la consola). Los demas se abren con `AX_SYS_OPEN` sobre el initrd, que
es de **solo lectura**:

```c
int fd = ax_syscall2(AX_SYS_OPEN, (uint64_t)"/apps/hello.axb", AX_O_RDONLY);
char buf[64];
int n = ax_syscall3(AX_SYS_READ, fd, (uint64_t)buf, 64);
ax_syscall1(AX_SYS_CLOSE, fd);
```

### Ejemplo: Leer archivo del initrd

```c
void leer_archivo(void) {
    int fd = ax_syscall2(AX_SYS_OPEN, (uint64_t)"/apps/hello.axb", AX_O_RDONLY);
    if (fd < 0) {
        sdk_print("No se pudo abrir el archivo\n");
        return;
    }
    
    char buf[128];
    int n = ax_syscall3(AX_SYS_READ, fd, (uint64_t)buf, 128);
    if (n > 0) {
        sdk_print("Contenido: %.*s\n", n, buf);
    }
    
    ax_syscall1(AX_SYS_CLOSE, fd);
}
```

## 7. Procesos

```c
char* argv[] = { "calc", "2+2", 0 };
int pid = ax_syscall3(AX_SYS_SPAWN, (uint64_t)"/apps/calc.elf", 
                     (uint64_t)argv, 2);
int status = ax_syscall1(AX_SYS_WAIT, pid);
```

La aplicacion hijo corre en su propio hilo con su propia pila de usuario;
`AX_SYS_WAIT` bloquea al padre hasta que el hijo termina.

### Ejemplo: Spawn y wait

```c
// Lanzar calculadora
char* args[] = { "calc", "2+2", NULL };
int pid = ax_syscall3(AX_SYS_SPAWN, (uint64_t)"/apps/calc.elf", 
                     (uint64_t)args, 2);

// Esperar a que termine
int status;
ax_syscall1(AX_SYS_WAIT, pid);
// status tiene el codigo de salida (0 si 2+2=4)
```

## 6. Senales

Las senales se implementan con una mascara de bits por hilo
(`AxThread::sig_pending`, `sig_taken` y `sig_handlers`). Las constantes
coinciden con POSIX:

| Senal | Numero |
|---|---|
| `AX_SIGHUP` | 1 |
| `AX_SIGINT` | 2 |
| `AX_SIGQUIT` | 3 |
| `AX_SIGKILL` | 9 |
| `AX_SIGUSR1` / `AX_SIGUSR2` | 10 / 12 |
| `AX_SIGTERM` | 15 |
| `AX_SIGCHLD` / `AX_SIGCONT` / `AX_SIGSTOP` | 17 / 18 / 19 |

### Ejemplo: Ignorar Ctrl+C

```c
// Ignorar Ctrl+C durante una seccion critica
ax_syscall2(AX_SYS_SIGNAL, AX_SIGINT, AX_SIG_IGN);

// Restaurar handler por defecto
ax_syscall2(AX_SYS_SIGNAL, AX_SIGINT, AX_SIG_DFL);
```

### Ejemplo: Manejo de senal en loop

```c
void main_loop(void) {
    for (;;) {
        // Hacer trabajo...
        
        // Verificar si hay senal pendiente
        if (ax_syscall1(AX_SYS_SIGPENDING, AX_SIGINT)) {
            // Ctrl+C recibido
            sdk_print("Interrumpido!\n");
            break;  // Salir del loop
        }
    }
}
```

### Ejemplo: Leer despues de senal

```c
char buf[128];
int n = ax_syscall3(AX_SYS_READ, 0, (uint64_t)buf, 128);
if (n == AX_EINTR) {
    // La lectura fue interrumpida por una senal
    if (ax_syscall1(AX_SYS_SIGPENDING, AX_SIGINT)) {
        // Ctrl+C fue recibido
        sdk_print("Fueron interruptido\n");
    }
}
```

`AX_SYS_SIGPENDING` con `sig = 0` devuelve la mascara completa (bit 0 =
senal 1). Con `sig > 0` devuelve 1 y consume la senal entregada, o 0 si
no estaba pendiente.

Ctrl+C en la consola se traduce en `AX_SIGINT` para el proceso en primer
plano (`ax_signal_set_foreground`). El shell lo fija antes de esperar a
una aplicacion y lo limpia al recuperar el prompt. `AX_SYS_SIGSEND`
permite enviar una senal a otro pid; `SIGKILL` termina el proceso de
inmediato y el resto queda pendiente hasta que vuelva a ejecutarse.

> Las senales se comprueban al entrar y salir de las syscalls y en las
> esperas de consola.  No hay entrega asincrona mientras la aplicacion
> esta ejecutando codigo de usuario.

## 7. Como anadir una syscall nueva

1. Anade el numero a `enum { AX_SYS_* }` en `apis/include/axos/abi.h`
   (al final, para no renumerar las existentes).

2. Implementa el handler en `Kernel/src/syscall.cpp`:

```cpp
static int sys_ax_ejemplo(uint64_t* args, uint64_t* ret)
{
    uint64_t valor = args[0];
    *ret = valor * 2;
    return 0;
}
```

3. Registralo en `ax_syscall_init()`:

```cpp
ax_syscall_register(AX_SYS_EJEMPLO, sys_ax_ejemplo);
```

4. Añade el wrapper en `apis/include/axos/syscall.h` y, si conviene, una
   funcion de alto nivel en `apis/src/`.

Los modulos pueden registrar syscalls en el mismo espacio de numeros
(`ax_syscall_register`) sin tocar el kernel.

### Ejemplo: Anadir una syscall personalizada desde un modulo

```cpp
// En el modulo kernel _ax_module_entry
ctx->register_syscall(120, mi_syscall);

// Despues, desde user mode
long ret = ax_syscall6(120, 42, 0, 0, 0, 0, 0);
// Ret = 84 (42 * 2)
```

---

**Siguiente manual**: [07-APIs-de-usuario](07-apis.md)