# Llamadas al sistema (syscalls)

Las syscalls son la unica forma que tiene una aplicacion en ring 3 de
pedir algo al kernel. El contrato vive en `apis/include/axos/abi.h`, que
es compartido por el kernel (`Kernel/src/syscall.cpp`) y por la libreria
de usuario (`apis/`).

## 1. Mecanismo

```
user mode                          ring 0
---------                          ------
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

### Convencion de registros

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

### Valores de retorno

* `>= 0`: exito (el significado depende de la llamada).
* `< 0`: error. El valor es uno de los codigos `AX_E*` de
  `apis/include/axos/abi.h`, que ya estan definidos como negativos
  (`AX_ENOENT = -1`, `AX_EINVAL = -3`, ...).

## 2. Tabla completa

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

Los numeros **100 en adelante** estan reservados para los modulos del
kernel (ver [05-modulos.md](05-modulos.md)).

## 3. Codigos de error

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

## 4. Ficheros

Los descriptores `0`, `1` y `2` son la entrada, salida y error estandar
(la consola). Los demas se abren con `AX_SYS_OPEN` sobre el initrd, que
es de **solo lectura**:

```c
int fd = ax_syscall2(AX_SYS_OPEN, (uint64_t)"/apps/hello.axb", AX_O_RDONLY);
char buf[64];
int n = ax_syscall3(AX_SYS_READ, fd, (uint64_t)buf, 64);
ax_syscall1(AX_SYS_CLOSE, fd);
```

## 5. Procesos

```c
char* argv[] = { "calc", "2+2", 0 };
int pid = ax_syscall3(AX_SYS_SPAWN, (uint64_t)"/apps/calc.elf", (uint64_t)argv, 2);
int status = ax_syscall1(AX_SYS_WAIT, pid);
```

La aplicacion hijo corre en su propio hilo con su propia pila de usuario;
`AX_SYS_WAIT` bloquea al padre hasta que el hijo termina.

## 6. Como anadir una syscall nueva

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
