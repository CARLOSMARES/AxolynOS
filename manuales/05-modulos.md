# Modulos del kernel

Un **modulo** es codigo que se carga dentro del kernel en tiempo de ejecucion,
sin reiniciar el sistema. Es la via principal para anadir funcionalidad al sistema
operativo sin recompilarlo entero.

Este manual explica el formato, el API y el procedimiento completo para crear
y publicar un modulo.

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
|  codigo (.text + .rodata)  |  lo que se ejecuta
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
directamente a funciones del kernel**: todo lo que necesita lo recibe en
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
`Kernel/modules/`, asi que no hay que tocar ningun fichero de configuracion:

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
long r = ax_syscall6(120, 21, 0, 0, 0, 0);   /* 42 */
```

Desde una aplicacion en Axolyn todavia no hay acceso directo a syscalls
proprias de modulos: se puede anadir una native en
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
libera la ranula. **Cuidado**: si el modulo ha registrado syscalls, hay
que quitarlo de la tabla antes de liberarlo.

---

## 8. Limitaciones conocidas

* No hay relocaciones: el modulo debe estar enlazado en su ranura.
* Un modulo no puede llamar a funciones internas del kernel, solo a los
  servicios del contexto.
* No hay unloaded seguro automatico: si el modulo ha registrado syscalls,
  la descarga deja punteros colgando.
* El `.bss` de un modulo se pone a cero al cargar, pero no se guarda
  entre cargas.

Estas limitaciones estan anotadas en `manuales/12-depuracion.md` junto con
la forma de trabajar con ellas.

---

## Ejemplo basico: hola mundo de modulo

1. Crear el archivo `Kernel/modules/hola/hola.cpp`:

```cpp
#include "ax_module.h"

#define AXMOD_HOLA_SALUDO 110

static const char g_mensaje[] = "hola desde el modulo hola\n";

static int hola_saludo(uint64_t* args, uint64_t* ret)
{
    (void)args;
    *ret = (uint64_t)(uintptr_t)g_mensaje;
    return 0;
}

extern "C" int _ax_module_entry(AxModuleContext* ctx)
{
    if (ctx == 0 || ctx->abi_version != AX_MOD_ABI_VERSION) {
        return -1;
    }

    ctx->print("[hola] modulo cargado en el kernel\n");
    ctx->print(g_mensaje);
    ctx->register_syscall(AXMOD_HOLA_SALUDO, hola_saludo);
    ctx->publish("hola.saludo", (void*)hola_saludo);

    return 0;
}
```

2. Compilar:

```bash
bash scripts/build.sh modules
```

3. Arrancar en QEMU y verificar:

```
[hola] modulo cargado en el kernel
axolyn:/> mods
```

4. Desde una aplicacion, usar la syscall `AX_SYS_MODLOAD` (15) para cargar
   el modulo dinamicamentedesde user mode, o simplemente confiar en
   `ax_module_autoload()` que lo cargara automaticamente al arrancar si el
   fichero esta en `/modules/` dentro del initrd.

---

## Ejemplo basico: publicar un simbolo y otro modulo leerlo

Supongamos que tenemos dos modulos: `hola` (el anterior) y `consola` que
quiere imprimir el mensaje de `hola`.

En `hola.cpp`:

```cpp
ctx->publish("hola.saludo", (void*)hola_saludo);
```

En `consola.cpp`:

```cpp
void* fn = ctx->lookup("hola.saludo");
if (fn) {
    // Llamar al puntero como funcion: RDI=arg1, RSI=arg2, ... , R8=ret
    __asm__ volatile (
        "call *%0"
        : "=a" (ret)
        : "r" (fn)
        : "memory"
    );
}
```

Esto permite que los modulos se comuniquen publicando y buscando simbolos
por nombre exacto en la tabla global del kernel.

---

## Ejemplo basico: usar syscalls desde un modulo

Si quieres que tu modulo sea invocable desde aplicaciones user mode, registra
syscalls con numeros >= 100:

```cpp
ctx->register_syscall(120, mi_syscall);
```

Desde una aplicacion en C++:

```cpp
#include "axos/axos.hpp"

long r = ax_syscall6(120, 21, 0, 0, 0, 0);   /* devuelve 42 */
```

El numero 120 esta fuera del rango del kernel (0..27) y pertenece al espacio
de los modulos, por lo que la llamada sera gestionada por el modulo cargado.

---

## Ejemplo basico: cargar y listar modulos desde user mode

Las aplicaciones user mode pueden interactuar con el gestor de modulos a
travs de la libreria `libaxos`:

```cpp
#include "axos/axos.hpp"

int main() {
    // Cargar un modulo dinamicamente
    int ret = axos::Module::load("mi_modulo.axmod");
    if (ret != 0) {
        // Error al cargar
        return 1;
    }

    // Listar todos los modulos cargados
    auto modules = axos::Module::list();
    for (const auto& mod : modules) {
        // Imprimir informacion del modulo
        printf("Modulo: %s, tamano: %u\n", mod.name, mod.size);
    }

    return 0;
}
```

Esto permite que las aplicaciones de user mode puedan inspeccionar y cargar
modulos en tiempo de ejecucion, siendo util para sistemas extensibles donde
se quieren añadir funcionalidades sin reiniciar.

---

## Ejemplo basico: descargar un modulo de forma segura

Al descargar un modulo, es importante primero remover cualquier syscall que
haya registrado para evitar punteros colgantes en la tabla de syscalls del kernel.

Desde el kernel:

```cpp
// 1. Primero, quitar las syscalls registradas por el modulo
//    (iterar sobre la tabla de simbolos publicados y quitar las correspondientes)
// 2. Llamar a ax_module_unload(indice)
// 3. Liberar la ranura

ax_module_unload(3);
```

Desde user mode, actualmente no hay una syscall directa para descargar un
modulo, asi que esto se suele hacer reiniciando el modulo o el sistema.

---

## Ejemplo basico: modulos con IPC

Si un modulo necesita comunicarse con otros modulos o con el espacio de
user mode, puede usar el IPC del kernel. Ejemplo basico:

En `modulo_a.cpp`:

```cpp
ctx->ipc_subscribe(42);  // Marcar el puerto 42 como de interes
```

En `modulo_b.cpp`:

```cpp
// Algoritmo para enviar un mensaje al puerto 42
// Esto usara la cola de mensajes global del kernel
```

El kernel provee funcionalidad de puertos de mensajes en `Kernel/src/ipc.cpp`,
asi que los modulos pueden usarla para intercambiar datos sin compartir
memoria directa.

---

## Ejemplo basico: modulos con timer

Usar `time_ms` para medir tiempo de ejecucion o implementar timeouts:

```cpp
static int mi_modulo_tiempo(uint64_t* args, uint64_t* ret)
{
    *ret = ctx->time_ms();
    return 0;
}

ctx->register_syscall(115, mi_modulo_tiempo);
```

Esto devuelve los milisegundos desde el arranque del kernel, util para
implementar funcionalidades que dependen del tiempo sin necesidad de
acceder al hardware directamente.

---

## Ejemplo basico: modulos con impresion formateada

Aunque el contexto solo provee `print(const char* text)`, se puede
imprimir informacion formateada construyendo el string manualmente:

```cpp
char buffer[64];
int len = snprintf(buffer, sizeof(buffer), "contador: %d", contador);
ctx->print(buffer);
```

Dado que el kernel no tiene printf completo en el contexto del modulo,
esta es la forma basica de mostrar informacion dinmica.

---

## Ejemplo basico: modulos y el heap del kernel

Para allocar memoria dinamica dentro de un modulo:

```cpp
void* mi_puntero = ctx->alloc(1024);  // 1024 bytes del heap del kernel
if (mi_puntero) {
    // Usar la memoria
    ctx->free(mi_puntero);  // Liberarla cuando termine
}
```

El heap del kernel esta gestionado por el PMM y el modulo puede solicitar
memoria siempre y cuando tenga el contexto adecuado. Es importante liberar
la memoria cuando ya no se necesita para no agotar los frames fisicos.

---

## Ejemplo basico: modulos con syscall personalizada para leer teclado

Si el modulo necesita leer input del usuario, puede registrar una syscall
personalizada:

```cpp
static int mi_teclado_lectura(uint64_t* args, uint64_t* ret)
{
    // Implementacion basica: leer del buffer del teclado
    // Esto dependera de como este implementado el input en el kernel
    *ret = 0;  // Retornar 0 si no hay teclas presionadas
    return 0;
}

ctx->register_syscall(119, mi_teclado_lectura);
```

Desde user mode, la aplicacion podria entonces hacer:

```cpp
long r = ax_syscall6(119, 0, 0, 0, 0, 0);   /* leer teclado */
```

Esto permite que los modulos extiendan el conjunto de operacones disponibles
en el sistema sin necesidad de modificar el kernel.

---

## Ejemplo basico: modulos con modulos dependientes

Si un modulo depende de otro modulo que provee algun servicio, puede
usar `ctx->lookup()` para encontrar el simbolo del otro modulo y usarlo:

```cpp
void* fn = ctx->lookup("otro_modulo.servicio");
if (fn) {
    // Llamar al servicio del otro modulo
    // args y ret dependen de la firma del servicio
}
```

Esto fomenta la creacion de modulos modulares y reutilizables, donde
cada uno provee funcionalidades concretas y los otros pueden consumirlas.

---

Estos ejemplos basicos deberian cubrir los casos de uso mas comunes al
crear, cargar y trabajar con modulos en AxolynOS. Cada modulo es un mundo
a parte, pero estas patrones se repiten en la mayoria de los casos de uso.