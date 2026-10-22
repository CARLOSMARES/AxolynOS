# Modulos del kernel

Un **modulo** es codigo que se carga dentro del kernel en tiempo de
ejecucion, sin reiniciar el sistema. Es la via principal para anadir
funcionalidad al sistema operativo sin recompilarlo entero.

Este manual explica el formato, el API y el procedimiento completo para
crear y publicar un modulo.

---

## 1. Como funciona

```
    build/modules/mi_modulo.elf        (ELF enlazado en una ranura fija)
            |
            |  scripts/tools/mkmod
            v
    build/modules/mi_modulo.axmod      (binario plano + cabecera)
            |
            |  scripts/build.sh -> initramfs AXFS
            v
    /modules/mi_modulo.axmod           (dentro de build/initrd.axfs)
            |
            |  arranque del kernel: ax_module_autoload()
            v
    AX_MODULES_VIRT_BASE + ranura*1 MiB   (codigo en ring 0)
            |
            |  se llama a _ax_module_entry(ctx)
            v
    el modulo registra syscalls, publica simbolos, abre ficheros, ...
```

El kernel reserva 16 ranuras de 1 MiB en la parte alta del espacio de
direcciones (`AX_MODULES_VIRT_BASE`, ver `Kernel/include/ax_config.h`).

---

## 2. Anatomia de un modulo

Un `.axmod` es un fichero plano:

```
+---------------------------+  offset 0
|  AxModHeader (128 bytes)  |  magic "AXMOD001", tamaños, ranura, nombre
+---------------------------+  offset = header.code_size
|  codigo (.text + .rodata) |  lo que se ejecuta
+---------------------------+  offset = header.code_size + header.data_size
|  datos (.data)            |  variables inicializadas
+---------------------------+  (el .bss se pone a cero al cargar)
```

La cabecera (`AxModHeader` en `Kernel/include/ax_module.h`) es:

| Campo | Significado |
|---|---|
| `magic` | `AXMOD001` |
| `abi_version` | version del API de modulos (1) |
| `init_offset` | desplazamiento del punto de entrada `_ax_module_entry` |
| `code_size` | bytes de codigo |
| `data_size` | bytes de datos inicializados |
| `bss_size` | bytes de `.bss` (se ponen a cero) |
| `slot` | ranura asignada dentro de la ventana de modulos |
| `name` | nombre del modulo (32 bytes) |
| `checksum` | suma de los bytes de codigo |

### Sin relocaciones

El enlazador coloca cada modulo en una direccion **fija** conocida
(`Kernel/linker/module.ld` + `-defsym AX_MOD_BASE=...`), de modo que el
cargador solo tiene que copiar los bytes: no hay que resolver ninguna
relocacion. La contrapartida es que **un modulo no puede llamar
directamente a funciones del kernel**: todo lo que necesite lo recibe en
el `AxModuleContext`.

---

## 3. El API de modulos

Al cargar el modulo, el kernel le entrega un `AxModuleContext`:

```c
typedef struct AxModuleContext {
    uint32_t abi_version;
    const char* name;
    void*  (*alloc)(uint64_t size);          /* heap del kernel      */
    void   (*free)(void* ptr);
    void   (*print)(const char* text);       /* consola del kernel   */
    int    (*register_syscall)(uint64_t nr, ax_syscall_handler handler);
    void   (*ipc_subscribe)(int port);
    uint64_t (*time_ms)(void);
    int    (*publish)(const char* name, void* symbol);
    void*  (*lookup)(const char* name);
} AxModuleContext;
```

* `alloc`/`free`: memoria del heap del kernel (`ax_malloc`).
* `print`: escribe en la consola (VGA y serie).
* `register_syscall`: registra una funcion como llamada al sistema. Los
  numeros **100 en adelante** estan reservados para los modulos (el kernel
  usa 0..27, ver `apis/include/axos/abi.h`).
* `publish`/`lookup`: tabla de simbolos compartida entre modulos.
* `ipc_subscribe`: marca un puerto de IPC como de interes para el modulo.
* `time_ms`: milisegundos desde el arranque.

---

## 4. Crear un modulo paso a paso

### 4.1 Escribir el fuente

Crea `Kernel/modules/mi_modulo/mi_modulo.cpp`:

```cpp
#include "ax_module.h"

#define AXMOD_MI_MODULO_HOLA 120   /* >= 100: reservas de los modulos */

static const char g_saludo[] = "hola desde mi_modulo\n";

static int mi_modulo_hola(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = (uint64_t)(uintptr_t)g_saludo;   /* puntero valido en el kernel */
    return 0;
}

extern "C" int _ax_module_entry(AxModuleContext* ctx)
{
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;
    }

    ctx->print("[mi_modulo] cargado\n");
    ctx->register_syscall(AXMOD_MI_MODULO_HOLA, mi_modulo_hola);
    ctx->publish("mi_modulo.hola", (void*)mi_modulo_hola);

    return 0;      /* 0 = correcto */
}
```

Reglas para el codigo de un modulo:

1. El punto de entrada **debe** llamarse `_ax_module_entry` (es el `ENTRY`
   del script de enlazado de modulos).
2. No llames a funciones del kernel directamente: usa `ctx->`.
3. No uses la STL, excepciones ni RTTI: el modulo se compila con las mismas
   banderas freestanding que el kernel.
4. Los punteros que devuelvas a user mode deben apuntar a memoria del
   kernel; el llamante no puede leerlos directamente (para eso estan las
   syscalls de copia).

### 4.2 Compilarlo

El script de compilacion descubre solo los directorios dentro de
`Kernel/modules/`, asi que no hay que tocar ningun fichero de
configuracion:

```bash
bash scripts/build.sh modules
```

Que hace, por modulo (en orden alfabetico, de forma que la ranura es
estable):

1. Compila los `.cpp`/`.S` del directorio con las banderas del kernel.
2. Enlaza con `Kernel/linker/module.ld` en
   `AX_MODULES_VIRT_BASE + ranura * 1 MiB`.
3. Empaqueta el ELF en `.axmod` con `scripts/tools/mkmod`.

Resultado:

```
build/modules/mi_modulo.elf
build/modules/mi_modulo.axmod
```

### 4.3 Probarlo

```bash
bash scripts/build.sh          # reconstruye initrd e imagen
bash scripts/run.sh            # arranca en QEMU
```

En el log serie debe aparecer:

```
[mi_modulo] cargado
```

Tambien puedes cargarlo desde una aplicacion con la syscall
`AX_SYS_MODLOAD` (numero 15) pasando el nombre del fichero dentro del
initrd.

---

## 5. Registrar llamadas al sistema desde un modulo

`ax_syscall_register` permite que un modulo exponga functionality a las
aplicaciones. El kernel mantiene la tabla en `Kernel/src/syscall.cpp`.

```cpp
static int mi_syscall(uint64_t* args, uint64_t* ret)
{
    /* args[0..5] son RDI, RSI, RDX, R10, R8, R9 */
    *ret = args[0] * 2;
    return 0;      /* 0 = ok, negativo = -errno */
}

ctx->register_syscall(120, mi_syscall);
```

Desde una aplicacion en C++ (usando `libaxos`):

```cpp
long r = ax_syscall6(120, 21, 0, 0, 0, 0, 0);   /* 42 */
```

Desde una aplicacion en Axolyn todavia no hay acceso directo a syscalls
propias de modulos: se puede anadir una native en
`Kernel/src/vmhost.cpp` que llame a la tabla de syscalls.

---

## 6. Compartir simbolos entre modulos

```cpp
/* en el modulo A */
ctx->publish("mi_modulo.hola", (void*)mi_modulo_hola);

/* en el modulo B (cargado despues) */
void* fn = ctx->lookup("mi_modulo.hola");
if (fn) { ((int (*)(uint64_t*, uint64_t*))fn)(args, ret); }
```

La tabla es global al kernel y se consulta por nombre exacto.

---

## 7. Cargar y descargar modulos

Desde el kernel:

```c
ax_module_init();          /* prepara la tabla de modulos */
ax_module_autoload();      /* carga los .axmod del initrd */
ax_module_load("mi_modulo.axmod");
ax_module_unload(3);
ax_module_fill_info(info, 16);
```

Desde user mode:

```cpp
axos::Module::load("mi_modulo.axmod");
axos::Module::list();
```

Al descargar un modulo se llama a su punto de salida si lo declara y se
libera la ranura. **Cuidado**: si el modulo ha registrado syscalls, hay
que quitarlo de la tabla antes de liberarlo.

---

## 8. Limitaciones conocidas

* No hay relocaciones: el modulo debe estar enlazado en su ranura.
* Un modulo no puede llamar a funciones internas del kernel, solo a los
  servicios del contexto.
* No hay unloaded seguro automatico: si el modulo tiene punteros publicados
  en otros modulos, la descarga deja punteros colgando.
* El `.bss` de un modulo se pone a cero al cargar, pero no se guarda
  entre cargas.

Estas limitaciones estan anotadas en `manuales/12-depuracion.md` junto con
la forma de trabajar con ellas.
