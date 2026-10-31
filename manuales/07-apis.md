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

### Ejemplo: Usar syscall wrappers basicos

```cpp
#include <axos/syscall.h>

// Escribir "Hola" en stdout (fd=1)
void ejemplo_write() {
    long n = ax_syscall3(AX_SYS_WRITE, 1, (long)"Hola\n", 5);
    // n tendra el numero de bytes escritos (5)
}

// Leer de stdin
void ejemplo_read() {
    char buf[64];
    long n = ax_syscall3(AX_SYS_READ, 0, (long)buf, 64);
    // buf ahora tiene lo leido, n tiene la longitud
}
```

## 2. La fachada C++

```cpp
#include "axos/axos.hpp"

// Salida a consola
axos::print("sin salto de linea");
axos::println("con salto de linea");
axos::print_int(-1234);
```

### Ejemplo: Aplicacion minima con fachada C++

```cpp
#include <axos/axos.hpp>

int main(int argc, char** argv) {
    axos::println("hola desde mi_app");
    
    for (int i = 1; i < argc; ++i) {
        axos::print("arg: ");
        axos::println(argv[i]);
    }
    
    return 0;
}
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

### Ejemplo: Usar File con RAII

```cpp
void leer_fichero() {
    // Abrir con RAII
    axos::File file("/apps/hello.axb", AX_O_RDONLY);
    
    if (!file.ok()) {
        sdk_print("No se pudo abrir el archivo\n");
        return;
    }
    
    // Leer automaticamente al destruir (o manualmente)
    char buffer[256];
    long n = file.read(buffer, sizeof(buffer));
    sdk_print("Leidos %ld bytes\n", n);
    // El destructor de file cerrara el fd automaticamente
}
```

### Procesos

```cpp
axos::Process p = axos::Process::spawn("/apps/calc.elf");
long status = p.wait();
axos::println("codigo de salida: " + axos::to_string(status));
```

`axos::Process` tambien permite `exit_code()` y `kill()`.

### Ejemplo: Gestionar procesos

```cpp
void ejemplo_procesos() {
    // Lanzar una aplicacion
    axos::Process p = axos::Process::spawn("/apps/calc.elf");
    
    // Esperar a que termine
    long status = p.wait();
    sdk_print("La aplicacion termino con codigo: %ld\n", status);
    
    // Matar un proceso por PID
    // axos::Process::kill(pid);
}
```

### Senales

Los wrappers de bajo nivel viven en `axos/process.h`:

```c
#include "axos/process.h"

long ax_proc_signal(long sig, unsigned long accion);
long ax_proc_sigsend(long pid, long sig);
long ax_proc_sigpending(long sig);
```

### Ejemplo: Manejo de senales

```cpp
void ejemplo_senales() {
    // Ignorar Ctrl+C durante una seccion critica
    ax_proc_signal(AX_SIGINT, AX_SIG_IGN);
    
    // Resto del codigo...
    
    // Restaurar handler por defecto
    ax_proc_signal(AX_SIGINT, AX_SIG_DFL);
    
    // Enviar SIGTERM a otro proceso
    // ax_proc_sigsend(pid_otro, AX_SIGTERM);
    
    // Consultar si llego SIGINT
    if (ax_proc_sigpending(AX_SIGINT)) {
        sdk_print("Se recibio SIGINT\n");
    }
}
```

### Comportamiento de Ctrl+C:

* `AX_SIG_DFL` (por defecto): el proceso termina con codigo 130 y el shell
  recupera el prompt.
* `AX_SIG_IGN`: la senal se descarta.
* Manejador propio: no se ejecuta en ring 0.  La lectura de consola
  (`ax_console_read_line`, `axos::gets`, `AX_SYS_READ`/`AX_SYS_GETS`)
  devuelve `AX_EINTR` y la aplicacion debe consultar la senal con
  `ax_proc_sigpending()` y decidir si limpia y continua o sale.

```cpp
char linea[128];
long n = ax_console_read_line(linea, sizeof(linea) - 1);
if (n == AX_EINTR && ax_proc_sigpending(AX_SIGINT)) {
    sdk_print("\ninterrumpido\n");
}
```

`ax_proc_signal()` y `ax_proc_sigsend()` devuelven `0` o un error
negativo; `ax_proc_sigpending(0)` devuelve la mascara completa de senales
(bits 0..31 = senales 1..32) y con `sig > 0` devuelve 1/0.

### Ordenes relacionadas:

`logout`, `login <usuario> <clave>`, `adduser <usuario> <clave> [admin]`,
`whoami` y `users`. La cuenta `root`/`root` se mantiene como recuperacion.
Las aplicaciones de usuario no reciben credenciales; el control de acceso
se aplica en el shell y en las operaciones de disco.

### Ejemplo: Estado de sesion

```cpp
void check_session() {
    // En el primer arranque, si no existe /etc/passwd,
    // el shell pedira crear una cuenta
    
    // En arranques posteriores, pedira usuario y contraseña
    // Las aplicaciones no ven las credenciales directamente
}
```

### Modulos

```cpp
axos::Module::load("axmem");              // se carga por nombre

AxModInfo buffer[16];
long n = axos::Module::list(buffer, 16);  // los modulos cargados
```

### Ejemplo: Trabajar con modulos

```cpp
void list_modulos() {
    AxModInfo buffer[16];
    long count = axos::Module::list(buffer, 16);
    
    for (int i = 0; i < count; ++i) {
        sdk_print("Modulo: %s, tamano: %u\n",
                  buffer[i].name, buffer[i].size);
    }
}
```

### Memoria y sistema

```cpp
axos::MemInfo  mem  = axos::mem::info();
axos::HeapInfo heap = axos::mem::heap();
axos::SysInfo  sys  = axos::system::info();
```

### Ejemplo: Informacion de memoria

```cpp
void ejemplo_memoria() {
    axos::MemInfo  mem  = axos::mem::info();
    axos::HeapInfo heap = axos::mem::heap();
    axos::SysInfo  sys  = axos::system::info();
    
    sdk_print("RAM total: %u MiB\n", mem.total_phys / (1024*1024));
    sdk_print("Heap libre: %u bytes\n", heap.free);
    sdk_print("CPU hilos: %u\n", sys.cpu_count);
}
```

### IPC

```cpp
axos::IpcMessage msg;
msg.port = 7;
msg.data = "hola";
axos::ipc::send(msg);

axos::ipc::recv(7, buffer, sizeof(buffer));
```

### Ejemplo: IPC entre procesos

```cpp
void ejemplo_ipc() {
    // En el proceso A
    axos::IpcMessage msg;
    msg.port = 42;
    msg.data = "Hola desde A";
    axos::ipc::send(msg);
    
    // En el proceso B (receptor)
    char buffer[128];
    axos::ipc::recv(42, buffer, sizeof(buffer));
    sdk_print("Recibi: %s\n", buffer);
}
```

### Maquina virtual

```cpp
long pid  = axos::Vm::run("/apps/hello.axb");
axos::VmInfo info = axos::Vm::status(pid);
```

### Ejemplo: Ejecutar aplicacion Axolyn

```cpp
void ejemplo_vm() {
    // Ejecutar una aplicacion Axolyn por medio de la VM
    long pid = axos::Vm::run("/apps/mi_app.axb");
    
    // Consultar su estado
    axos::VmInfo info = axos::Vm::status(pid);
    sdk_print("VM pid: %ld, estado: %d\n", pid, info.status);
}
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

### Ejemplo: Estructura de main

```cpp
// El punto de entrada debe ser este prototipo
extern "C" int main(int argc, char** argv) {
    // Tu codigo aqui
    axos::println("Aplicacion iniciada");
    return 0;  // codigo de salida
}
```

## 4. Reglas para escribir sobre libaxos

* Nada de STL, excepciones ni RTTI: el codigo se compila freestanding.
* No hay `malloc`: usa `axos::heap()` si necesitas memoria dinamica, o
  memoria estatica.
* No hay `printf`: `axos::print`/`axos::println` cubren el caso basico y
  `axos::print_int` los numeros.
* Las funciones que reciben cadenas de user mode no las retienen: el
  kernel copia los datos.
* Devuelve `long` con el convenio de errores (negativo = `-errno`).

### Ejemplo: Codigo compatible con freestanding

```cpp
void ejemplo_freestanding() {
    // No usar: #include <iostream>, std::cout, std::string
    // Usar en su lugar:
    axos::println("texto sin STL");
    axos::print_int(1234);
}
```

## 5. Enlazado de una aplicacion

```bash
g++ -std=c++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -mno-red-zone -fPIE -I apis/include -c mi_app.cpp -o mi_app.o

g++ -fPIE -pie -nostdlib -static -Wl,-T,apis/linker/userspace.ld \
    -o mi_app.elf mi_app.o build/lib/libaxos.a
```

El script `scripts/build.sh apps` hace exactamente esto para todas las
aplicaciones de `apps/native/`.

### Ejemplo: Build completo de una aplicacion

```bash
# 1. Compilar objeto
g++ -std=c++17 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -mno-red-zone -fPIE -I apis/include -c mi_app.cpp -o mi_app.o

# 2. Enlazar con libaxos
g++ -fPIE -pie -nostdlib -static -Wl,-T,apis/linker/userspace.ld \
    -o mi_app.elf mi_app.o build/lib/libaxos.a

# 3. Empaquetar como .axe
build/tools/mkaxe mi_app.elf mi_app.axe mi_app

# 4. Probar en host
build/axrun mi_app.axe

# 5. Subir a la imagen
bash scripts/build.sh initrd image
```

## 6. Aplicaciones nativas (.axe)

Las aplicaciones se guardan en el initrd (imagen AXFS en memoria) con la
ruta `/apps/<nombre>.axe`. El kernel las busca ahi cuando se ejecutan.
Tambien acepta `.elf` sueltos por compatibilidad.

### Ejemplo: Estructura .axe

```
.axe  ax_axe_header (68 bytes)  +  ELF64 PIE
      magic "AXE1", version, tipo, flags, tamano, crc32,
      entry/load hints, nombre, arquitectura
```

### Ejemplo: Herramientas para .axe

```bash
# Empaquetar un ELF como .axe
build/tools/mkaxe <elf> <axe> <nombre>

# Ejemplo
build/tools/mkaxe build/apps/calc.elf build/apps/calc.axe calc
```

## 7. Formato `.axe` y biblioteca `.axl`

Los dos formatos propios usan una cabecera fija y un CRC32 para detectar
paquetes corruptos **antes** de cargarlos:

```
.axe  ax_axe_header (68 bytes)  +  ELF64 PIE
      magic "AXE1", version, tipo, flags, tamano, crc32,
      entry/load hints, nombre, arquitectura

.axl  ax_axl_header (60 bytes)  +  archivo ar
      magic "AXL1", version, nombre, tamano, crc32
```

Las definiciones estan en `Kernel/include/ax_pkg.h` y las herramientas
en `scripts/tools/`:

| Herramienta | Uso |
|---|---|
| `build/tools/mkaxe <elf> <axe> <nombre>` | empaqueta un ELF como `.axe` |
| `build/tools/mkaxl <lib.a> <lib.axl> <nombre>` | empaqueta un `ar` como `.axl` |
| `build/tools/axl2a <lib.axl> <lib.a>` | extrae el `ar` de un `.axl` |

`build.sh apps` hace el ciclo completo: genera `build/lib/libaxos.axl`,
extrae su contenido con `axl2a` y enlaza cada aplicacion **contra la
biblioteca extraida**. Despues crea el `.axe` con `mkaxe`. Asi el formato
se valida en cada compilacion.

### Ejemplo: Listado de bibliotecas

```text
libs
  lib/libaxos.axl  libaxos  v1  miembros=12  bytes=170092  crc=A67ED594
```

## 8. Aplicaciones Axolyn (bytecode)

### Ejemplo: Aplicacion Axolyn basica

```axolyn
import console;

fn main() {
    console.write("hola desde Axolyn\n");
}
```

### Compilar y ejecutar:

```bash
# Compilar a bytecode
build/axc apps/axolyn/hello.ax -o build/apps/hello.axb

# Ejecutar en el host (pruebas rapidas)
build/axrun build/apps/hello.axb

# Arrancar dentro del SO
bash scripts/build.sh initrd image
bash scripts/run.sh
# Desde el shell: ax /apps/hello.axb
```

### Ejemplo: Aplicacion con variables y condiciones

```axolyn
import console;

const MAX: int = 100;

fn main() {
    var contador: int = 0;
    
    while (contador < 10) {
        console.print_int(contador);
        console.write("\n");
        contador = contador + 1;
    }
    
    if (contador > 5) {
        console.print("muchos\n");
    } else {
        console.print("pocos\n");
    }
}
```

---

**Siguiente manual**: [08-crear-aplicaciones](08-crear-aplicaciones.md)