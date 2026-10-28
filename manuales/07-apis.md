# APIs de usuario (libaxos)

`libaxos` es la biblioteca que usan las aplicaciones nativas para hablar
con el kernel. Vive en `apis/` y se compila como `build/lib/libaxos.a`
(archivo `ar` de toda la vida) y como `build/lib/libaxos.axl` (formato
propio: cabecera `AXL1` + el mismo archivo `ar` + CRC32).

```
apis/include/axos/abi.h        contrato (numeros de syscall, estructuras)
apis/include/axos/syscall.h    wrappers en linea de la llamada al sistema
apis/include/axos/axos.hpp     fachada C++ (namespace axos)
apis/src/*.cpp                 implementacion
apis/src/start.S               punto de entrada de user mode (_start)
```

## 1. El nivel mas bajo: `ax_syscall`

```cpp
#include "axos/syscall.h"

long ax_syscall0(long nr);
long ax_syscall1(long nr, long a0);
long ax_syscall2(long nr, long a0, long a1);
long ax_syscall3(long nr, long a0, long a1, long a2);
long ax_syscall4(long nr, long a0, long a1, long a2, long a3);
long ax_syscall5(long nr, long a0, long a1, long a2, long a3, long a4);
long ax_syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
```

Cada una ejecuta `int 0x80` conservando todos los registros salvo `RAX`.

```cpp
long n = ax_syscall3(AX_SYS_WRITE, 1, (long)buffer, length);
```

## 2. La fachada C++

```cpp
#include "axos/axos.hpp"

axos::print("sin salto de linea");
axos::println("con salto de linea");
axos::print_int(-1234);
```

### Ficheros

`axos::fs::open` devuelve el descriptor; `axos::File` es la version con RAII:

```cpp
long fd = axos::fs::open("/apps/hello.axb", AX_O_RDONLY);

axos::File file("/apps/hello.axb", AX_O_RDONLY);
if (file.ok()) {
    char buffer[256];
    long n = file.read(buffer, sizeof(buffer));
    file.close();                 // tambien lo hace el destructor
}
```

`axos::File` usa RAII: cierra el descriptor al destruirse (move semantica,
sin copias, de modo que nunca hay dos objetos que intenten cerrar el mismo
fd).

### Procesos

```cpp
axos::Process p = axos::Process::spawn("/apps/calc.elf");
long status = p.wait();
axos::println("codigo de salida: " + axos::to_string(status));
```

`axos::Process` tambien permite `exit_code()` y `kill()`.

### Senales

Los wrappers de bajo nivel viven en `axos/process.h`:

```c
#include "axos/process.h"

long ax_proc_signal(long sig, unsigned long accion);
long ax_proc_sigsend(long pid, long sig);
long ax_proc_sigpending(long sig);
```

```c
/* Ignorar Ctrl+C durante una seccion critica. */
ax_proc_signal(AX_SIGINT, AX_SIG_IGN);

/* Instalar un manejador propio (ver aviso mas abajo). */
ax_proc_signal(AX_SIGINT, (unsigned long)mi_handler);

/* Enviar SIGTERM a otro proceso. */
ax_proc_sigsend(pid, AX_SIGTERM);

/* Consultar si llego SIGINT. */
if (ax_proc_sigpending(AX_SIGINT)) {
    /* ... */
}
```

Comportamiento de Ctrl+C:

* `AX_SIG_DFL` (por defecto): el proceso termina con codigo 130 y el shell
  recupera el prompt.
* `AX_SIG_IGN`: la senal se descarta.
* Manejador propio: no se ejecuta en ring 0.  La lectura de consola
  (`ax_console_read_line`, `axos::gets`, `AX_SYS_READ`/`AX_SYS_GETS`)
  devuelve `AX_EINTR` y la aplicacion debe consultar la senal con
  `ax_proc_sigpending()` y decidir si limpia y continua o sale.

```c
char linea[128];
long n = ax_console_read_line(linea, sizeof(linea) - 1);
if (n == AX_EINTR && ax_proc_sigpending(AX_SIGINT)) {
    axos::println("\ninterrumpido");
}
```

`ax_proc_signal()` y `ax_proc_sigsend()` devuelven `0` o un error
negativo; `ax_proc_sigpending(0)` devuelve la mascara completa de senales
(bits 0..31 = senales 1..32) y con `sig > 0` devuelve 1/0.

### Sesiones y primer arranque

El login no es una API de `libaxos`: lo gestiona el shell en el hilo
principal (con teclado y temporizador activos).  En el primer arranque,
si no existe `/etc/passwd`, el shell pide crear una cuenta y la guarda en
el volumen AxFS-D.  En arranques posteriores pide usuario y contraseña.

Ordenes relacionadas: `logout`, `login <usuario> <clave>`,
`adduser <usuario> <clave> [admin]`, `whoami` y `users`.  La cuenta
`root`/`root` se mantiene como recuperacion.  Las aplicaciones de usuario
no reciben credenciales; el control de acceso se aplica en el shell y en
las operaciones de disco.

### Modulos

```cpp
axos::Module::load("axmem");              // se carga por nombre

AxModInfo buffer[16];
long n = axos::Module::list(buffer, 16);  // los modulos cargados
```

### Memoria y sistema

```cpp
axos::MemInfo  mem  = axos::mem::info();
axos::HeapInfo heap = axos::mem::heap();
axos::SysInfo  sys  = axos::system::info();
```

### IPC

```cpp
axos::IpcMessage msg;
msg.port = 7;
msg.data = "hola";
axos::ipc::send(msg);

axos::ipc::recv(7, buffer, sizeof(buffer));
```

### Maquina virtual

```cpp
long pid  = axos::Vm::run("/apps/hello.axb");
axos::VmInfo info = axos::Vm::status(pid);
```

## 3. El arranque de una aplicacion

`apis/src/start.S` define `_start`, el simbolo de entrada del ELF:

```asm
_start:
    mov  rax, [rsp]        # argc
    mov  rbx, [rsp+8]      # argv
    and  rsp, -16
    call axos_runtime_start
    hlt
```

`axos_runtime_start` (en `apis/src/`) llama a `main(argc, argv)` y, al
volver, llama a `AX_SYS_EXIT` con el codigo de retorno.

## 4. Reglas para escribir sobre libaxos

* Nada de STL, excepciones ni RTTI: el codigo se compila freestanding.
* No hay `malloc`: usa `axos::heap()` si necesitas memoria dinamica, o
  memoria estatica.
* No hay `printf`: `axos::print`/`axos::println` cubren el caso basico y
  `axos::print_int` los numeros.
* Las funciones que reciben cadenas de user mode no las retienen: el
  kernel copia los datos.
* Devuelve `long` con el convenio de errores (negativo = `-errno`).

## 5. Enlazado de una aplicacion

```bash
g++ -std=c++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -mno-red-zone -fPIE -I apis/include -c mi_app.cpp -o mi_app.o

g++ -fPIE -pie -nostdlib -static -Wl,-T,apis/linker/userspace.ld \
    -o mi_app.elf mi_app.o build/lib/libaxos.a
```

El script `scripts/build.sh apps` hace exactamente esto para todas las
aplicaciones de `apps/native/`.
