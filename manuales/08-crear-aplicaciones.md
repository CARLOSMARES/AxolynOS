# Crear aplicaciones para AxolynOS

Hay dos formas de escribir un programa para el sistema:

| Tipo | Lenguaje | Como se ejecuta | Formato |
|---|---|---|---|
| **Nativa** | C++ (freestanding) | En ring 3, como proceso del kernel | `.axe` (cabecera + ELF64 PIE) |
| **Axolyn** | Lenguaje Axolyn | En la maquina virtual del kernel | Bytecode `.axb` |

Las dos conviven: una aplicacion nativa puede lanzar una aplicacion
Axolyn y viceversa.

---

## 1. Aplicaciones nativas (C++)

### 1.1 El esqueleto minimo

Crea `apps/native/mi_app.cpp`:

```cpp
#include "axos/axos.hpp"

int main(int argc, char** argv)
{
    axos::println("hola desde mi_app");
    for (int i = 1; i < argc; ++i) {
        axos::print("arg: ");
        axos::println(argv[i]);
    }
    return 0;
}
```

Y compila:

```bash
bash scripts/build.sh apps
```

El resultado es `build/apps/mi_app.elf` (ELF64 PIE) y
`build/apps/mi_app.axe` (el mismo ejecutable empaquetado con su cabecera
`AXE1`, tamano y CRC32). Para meterla en la imagen:

```bash
bash scripts/build.sh initrd image
```

El shell ejecuta preferentemente el `.axe`:

```
run mi_app
```

### 1.2 Que hace el arranque de una aplicacion

El kernel carga el ELF, prepara una pila de usuario y salta a `_start`
(defined en `apis/src/start.S`):

```
  kernel                       user mode
  ------                       ---------
  mapea paginas de la app
  escribe en la pila:
     [RSP]      argc
     [RSP+8]    argv[0]
     [RSP+16]   argv[1]
     ...         NULL
  crea el hilo y salta a _start
                               _start:
                                 lee argc/argv de la pila
                                 llama a axos_runtime_start(argc, argv)
                                 llama a main(argc, argv)
                                 AX_SYS_EXIT con el codigo de retorno
```

Reglas:

* `main` debe declararse `extern "C" int main(int, char**)`.
* No hay libc: no existen `printf`, `malloc` ni `strlen`. Usa `libaxos`
  o implementa lo que necesites.
* Devuelve 0 en caso de exito; cualquier otro valor es un error.

### 1.3 La libreria de usuario

`libaxos` (`apis/`) envuelve las llamadas al sistema:

```cpp
#include "axos/axos.hpp"

axos::println("texto");
axos::print_int(42);

auto info = axos::mem::info();
axos::println(info.total_phys);

int fd = axos::fs::open("/apps/hello.axb");
char buffer[128];
int n = axos::fs::read(fd, buffer, sizeof(buffer));
axos::fs::close(fd);

axos::Process child = axos::Process::spawn("/apps/calc.elf");
int status = child.wait();
```

El mecanismo de bajo nivel es una sola instruccion:

```cpp
// apis/include/axos/syscall.h
long ax_syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
```

que hace `int 0x80` con:

```
RAX = numero de syscall
RDI = arg0   RSI = arg1   RDX = arg2
R10 = arg3   R8  = arg4   R9  = arg5
RAX = valor de retorno (0 o -errno)
```

### 1.4 Listado de aplicaciones disponibles

Las aplicaciones se guardan en el initrd (imagen AXFS en memoria) con la
ruta `/apps/<nombre>.axe`. El kernel las busca ahi cuando se ejecutan.
Tambien acepta `.elf` sueltos por compatibilidad.

### 1.5 Formato `.axe` y biblioteca `.axl`

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

Para inspeccionar las bibliotecas desde el shell:

```
libs
  lib/libaxos.axl  libaxos  v1  miembros=12  bytes=170092  crc=A67ED594
```

---

## 2. Aplicaciones Axolyn (bytecode)

### 2.1 Esqueleto minimo

Crea `apps/axolyn/mi_app.ax`:

```
import console;

fn main() {
    console.write("hola desde mi_app\n");
}
```

Compila y ejecuta en el host para probarlo rapido:

```bash
build/axc apps/axolyn/mi_app.ax -o build/apps/mi_app.axb
build/axrun build/apps/mi_app.axb
```

### 2.2 Ejecutarla dentro del sistema

```bash
bash scripts/build.sh apps initrd image
bash scripts/run.sh
```

Y desde otra aplicacion (nativa o Axolyn):

```cpp
int pid = axos::Vm::run("/apps/mi_app.axb");
```

### 2.3 Como se ejecuta dentro del kernel

1. El kernel lee el `.axb` del initrd.
2. Crea una VM (`Kernel/vm/axvm.cpp`) con su heap y su pila.
3. Lanza un hilo del kernel que ejecuta el bytecode.
4. Las funciones nativas (`console.write`, `fs.open`, `proc.exit`...) se
   ejecutan en el propio kernel y hacen de puente con las syscalls.

La referencia completa del lenguaje esta en
[09-lenguaje-axolyn.md](09-lenguaje-axolyn.md) y el formato del bytecode
en [10-bytecode-vm.md](10-bytecode-vm.md).

---

## 3. Probar una aplicacion durante el desarrollo

El ciclo recomendado es:

```bash
# 1. compilar la aplicacion
bash scripts/build.sh apps

# 2. probarla en el host (rapido, sin arrancar el SO)
build/axrun build/apps/mi_app.axb

# 3. arrancar el sistema y probarla dentro
bash scripts/build.sh initrd image && bash scripts/run.sh
```

Para ver el log del kernel por consola serie mientras se ejecuta:

```bash
bash scripts/run.sh --headless
```

(Ctrl+A y luego X para salir de QEMU.)

---

## 4. Como se lanza una aplicacion desde el shell del kernel

El kernel incluye un interprete de ordenes minimo en
`Kernel/src/shell.cpp`. Las ordenes disponibles:

```
help                 lista las ordenes
apps                 lista las aplicaciones del initrd
libs                 lista las bibliotecas .axl (nombre, version, CRC)
run <app> [args]     ejecuta una aplicacion nativa (.axe, .elf) y espera
ax <app> [args]      ejecuta una aplicacion Axolyn (.axb)
mods                 lista los modulos cargados
mem                  informacion de memoria
heap                 informacion del heap del kernel
uptime               tiempo desde el arranque
threads              hilos activos
vm                   maquinas virtuales
clear                limpia la pantalla
reboot               reinicia
```

`run` prueba primero `<app>.axe`, despues `<app>.elf` y por ultimo
`<app>.axb`. Cuando la aplicacion es nativa, el shell espera su salida:
`ax_proc_wait` libera la ranura de user mode y reapea el hilo, de modo que
no se acumulan procesos zombis y la ranura 0 se reutiliza en cada
ejecucion.

---

## 5. Errores habituales

| Sintoma | Causa probable |
|---|---|
| `undefined reference to main` | falta `extern "C" int main(int, char**)` |
| Enlace con `R_X86_64_32S ... can not be used when making a PIE object` | la libreria se compilo sin `-fPIE` |
| La app arranca y muere al instante | `main` devuelve sin llamar a `exit`, o el enlace de la pila esta roto |
| `AX_ENOENT` al abrir un fichero | la ruta no existe en el initrd (remember `/apps/...`) |
| La app nativa no ve el initrd | el ELF se cargo en una ranura distinta: revisa `AX_USER_VIRT_BASE` |
| El programa Axolyn no imprime nada | falta `import console;` o se escribio en `core` en vez de `console` |
