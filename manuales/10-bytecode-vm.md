# Bytecode AXB y la maquina virtual

El compilador `axc` no genera codigo nativo: genera **bytecode AXB** que
ejecuta la maquina virtual que vive dentro del kernel
(`Kernel/vm/axvm.cpp`).

Ventajas de esta decision:

* La VM es pequena y portable (esta escrita en C++ sin dependencias).
* Los programas Axolyn se pueden cargar y ejecutar en el kernel sin
  necesitar un enlazador dinamico.
* El mismo bytecode se puede validar, depurar o convertir a otras formas.

## 1. Formato del fichero `.axb`

Todos los valores son little-endian.

```
+----------------------------------+
|  AxbHeader (64 bytes)            |
+----------------------------------+
|  code[]        (code_size bytes) |
+----------------------------------+
|  constants[]   (const_count i64) |
+----------------------------------+
|  strings[]     (str_count)       |
+----------------------------------+
|  str_data[]    (str_data_size)   |
+----------------------------------+
|  functions[]   (func_count)      |
+----------------------------------+
|  globals[]     (global_count)    |
+----------------------------------+
|  natives[]     (native_count)    |
+----------------------------------+
|  imports[]     (import_count)    |
+----------------------------------+
```

### Cabecera (`AxbHeader`)

| Campo | Descripcion |
|---|---|
| `magic` | `"AXBYTCD1"` |
| `version` | 1 |
| `code_size` | bytes de codigo |
| `const_count` | numero de constantes `i64` |
| `str_count` | entradas de la tabla de cadenas |
| `str_data_size` | bytes del blob de cadenas |
| `func_count` | funciones declaradas |
| `global_count` | variables globales |
| `native_count` | natives conocidas |
| `import_count` | namespaces importados |
| `entry_func` | indice de la funcion de entrada |
| `main_func` | indice de `main` |
| `global_slots` | celdas de 8 bytes reservadas para las globales |

### Ejemplo: Inspeccionar un archivo .axb

```bash
# Ver la cabecera del bytecode
build/axc apps/axolyn/hello.ax --dump-bytecode

# Resultado esperado:
# magic: AXBYTCD1
# version: 1
# code_size: 128
# const_count: 0
# str_count: 2
# str_data_size: 13
# func_count: 1
# global_count: 0
# native_count: 2
# import_count: 1
# entry_func: 0
# main_func: 0
# global_slots: 0
```

### Tablas

* `AxbFunction`: `name_off`, `addr` (offset en el codigo), `argc`,
  `nlocals`, `stack_size`.
* `AxbGlobal`: `name_off`, `slot`, `size`, `flags`.
* `AxbNative`: `name_off`, `id` (id de la tabla de natives).
* `AxbImport`: `ns_off`, `native_base`, `count`.

**Las cadenas de las tablas son offsets dentro de `str_data`.**

## 2. La maquina

* Pila de valores: celdas de 8 bytes.
* Pila de marcos: cada llamada tiene un marco con sus argumentos y
  variables locales.
* Heap propio: arena con bloques de bytes (cadenas, arrays, structs).
* Celda = 8 bytes siempre. Un array de 5 `int` son 5 celdas; un struct
  con 2 campos son 2 celdas.

### Ejemplo: Estructura de memoria de la VM

```text
Stack (pila de valores):
  +--------+--------+--------+
  |  celda |  celda |  celda |  <- puntero de pila
  +--------+--------+--------+
  |  valor |  valor |  valor |
  +--------+--------+--------+

Heap (arena de memoria):
  +--------+--------+--------+--------+
  | bloque | bloque | bloque | bloque |  <- puntero de heap
  +--------+--------+--------+--------+
  | cadena | array  | struct | libre  |
  +--------+--------+--------+--------+
```

### 2.1 Opcodes

| Opcode | Efecto |
|---|---|
| `PUSH_I64` | empuja una constante immediate |
| `PUSH_STR` | empuja un puntero a una cadena del blob |
| `POP` / `DUP` / `DUP2` / `SWAP` | manipulacion de la pila |
| `LOAD_LOCAL` / `STORE_LOCAL` | variables locales (por indice de marco) |
| `LOAD_GLOBAL` / `STORE_GLOBAL` | variables globales |
| `LOCAL_ADDR` / `GLOBAL_ADDR` | direccion de una celda (para `&`) |
| `JMP` / `JZ` / `JNZ` / `JLT` / `JGE` | saltos (offsets absolutos) |
| `CALL` / `CALL_NATIVE` / `RET` | llamadas |
| `ADD` `SUB` `MUL` `DIV` `MOD` `NEG` | aritmetica |
| `NOT` `BITNOT` `AND` `OR` `XOR` `SHL` `SHR` `USHR` | logica y bits |
| `EQ` `NE` `LT` `LE` `GT` `GE` | comparacion (dejan 0 o 1) |
| `DEREF` / `STORE` | punteros |
| `NEW` / `FREE` | reserva y libera memoria |
| `ARRAY_NEW` / `ARRAY_GET` / `ARRAY_SET` / `ARRAY_LEN` | arrays |
| `STRUCT_NEW` / `STRUCT_GET` / `STRUCT_SET` | structs |
| `STR_LEN` / `STR_CONCAT` / `STR_EQ` / `STR_CMP` / `STR_CHAR_AT` | cadenas |
| `PRINT` / `PRINTLN` / `PRINT_I64` | salida |
| `YIELD` / `SLEEP` / `HALT` / `ASSERT` | control de la VM |

### Ejemplo: Programa en bytecode conceptual

```text
; Programa "Hola Mundo"
PUSH_I64 42        ; empuja 42 a la pila
PRINT_I64          ; imprime 42 y quita de la pila
HALT               ; detiene la VM
```

### 2.2 Convencion de llamada

```
CALL <indice de funcion> <numero de argumentos>
```

Los argumentos se pasan **apilados** en el mismo orden de la declaracion;
la VM copia los `argc` valores al marco nuevo.

### Ejemplo: Llamada a funcion

```text
; Llamar a func con 2 argumentos
; En el codigo fuente: fn sumar(a: int, b: int): int { return a + b; }
; En bytecode:
LOAD_LOCAL 0       ; cargar primer argumento (a)
LOAD_LOCAL 1       ; cargar segundo argumento (b)
ADD                ; a + b
RET                ; retornar resultado
```

### 2.3 Estado de ejecucion

```c
typedef enum {
    AX_VM_RUNNING,  /* sigue */
    AX_VM_HALTED,   /* el programa termino */
    AX_VM_YIELD,    /* cedio el CPU */
    AX_VM_SLEEP,    /* esta durmiendo */
    AX_VM_ERROR     /* error fatal */
} AxVMStatus;
```

`axvm_run()` ejecuta hasta alcanzar uno de esos estados y se puede
llamar otra vez para continuar.

### Ejemplo: Bucle de ejecucion de la VM

```c
// En el kernel, el bucle principal de la VM
AxVMStatus status = AX_VM_RUNNING;
while (status == AX_VM_RUNNING) {
    status = axvm_run();
    // Si status es AX_VM_YIELD, el hilo cede el CPU
    // Si es AX_VM_SLEEP, el hilo esta durmiendo
    // Si es AX_VM_ERROR, hubo un fallo fatal
}
```

## 3. Nativas

La tabla de ids esta en `Kernel/vm/axvm.h` (`AX_NT_*`) y es compartida con
el compilador:

| Namespace | Ids | Funciones |
|---|---|---|
| `core` | 1-5 | `print`, `println`, `print_int`, `strlen`, `int_to_str` |
| `console` | 6-8 | `write`, `read_line`, `clear` |
| `fs` | 9-14 | `open`, `read`, `write`, `close`, `list`, `stat` |
| `proc` | 15-18 | `exit`, `pid`, `spawn`, `gets` |
| `mem` | 19-21 | `info`, `alloc`, `free` |
| `time` | 22-23 | `now_ms`, `sleep` |

El kernel engancha esas natives a sus propias syscalls
(`Kernel/src/vmhost.cpp`), de modo que un programa Axolyn puede abrir
ficheros, lanzar procesos o consultar la memoria.

### Ejemplo: Usar natives desde Axolyn

```axolyn
import core;
import time;
import fs;
import proc;

fn main() {
    // Imprimir con core.print
    core.println("Hola desde Axolyn");
    
    // Dormir 500 ms
    time.sleep(50);
    
    // Obtener tiempo actual
    var t: int = time.now_ms();
    core.print("Tiempo: ");
    core.print_int(t);
    core.write(" ms\n");
    
    // Salir del proceso
    proc.exit(0);
}
```

## 4. Como se ejecuta un programa en el kernel

```
AX_SYS_VMRUN("/apps/hello.axb")
    |
    +--> ax_vfs_read()            lee el .axb del initrd
    +--> axvm_create()            crea la VM con el heap del kernel
    +--> axvm_load()              parsea la imagen
    +--> ax_thread_create_kernel() hilo que ejecuta el bytecode
              |
              +--> axvm_run()    bucle: ejecuta, cede, duerme, termina
```

El hilo de la VM es un hilo del kernel normal: puede bloquearse con
`time.sleep()` y no bloquea a los demas.

### Ejemplo: Ejecutar una aplicacion desde user mode

```c
// Desde una aplicacion nativa
long pid = ax_syscall1(AX_SYS_VMRUN, (uint64_t)"/apps/hello.axb");
// La VM se crea y empieza a ejecutarse
```

## 5. La VM en el host

El mismo `Kernel/vm/axvm.cpp` se compila para Linux, y `axrun` lo usa:

```bash
build/axc apps/axolyn/hello.ax -o /tmp/hello.axb
build/axrun /tmp/hello.axb
```

`axrun` implementa las natives minimas (`core.*`) para poder probar
programas sin arrancar el sistema.

### Ejemplo: Probar en host

```bash
# Compilar para host
build/axc apps/axolyn/hello.ax -o /tmp/hello.axb

# Ejecutar en host
build/axrun /tmp/hello.axb
# Salida: "Hola desde Axolyn" (usando natives minimas de axrun)
```

## 6. Añadir una nueva native

1. Añade el id a `AX_NT_*` en `Kernel/vm/axvm.h` (`AX_NT_*`) y actualiza
   `ax_native_namespaces` en `Kernel/vm/axvm.cpp`.

2. Implementa el caso en la funcion nativa del kernel
   (`Kernel/src/vmhost.cpp`) y en el runner de host si quieres probarlo
   fuera del sistema.

3. El compilador solo necesita la tabla de namespaces para resolver
   `namespace.funcion`; no hay que registrar nada mas.

**El orden de los ids es **parte del formato**: cambiarlo rompe la
compatibilidad con los `.axb` ya generados.

### Ejemplo: Añadir una nueva nativa

```c
// En Kernel/vm/axvm.h
enum {
    AX_NT_CORE_PRINT      = 1,
    // ...
    AX_NT_MI_NATIVA       = 24,  // nueva nativa
};

// En Kernel/src/vmhost.cpp
case AX_NT_MI_NATIVA:
    // Implementar funcionalidad nueva
    return 0;

// En el compilador (sema.cpp)
{ "mi_nativa", AX_NT_MI_NATIVA, ... }
```

## 5. La VM en el host

El mismo `Kernel/vm/axvm.cpp` se compila para Linux, y `axrun` lo usa:

```bash
build/axc apps/axolyn/hello.ax -o /tmp/hello.axb
build/axrun /tmp/hello.axb
```

`axrun` implementa las natives minimas (`core.*`) para poder probar
programas sin arrancar el sistema.

### Ejemplo: Probar en host

```bash
# Compilar para host
build/axc apps/axolyn/hello.ax -o /tmp/hello.axb

# Ejecutar en host
build/axrun /tmp/hello.axb
# Salida: "Hola desde Axolyn" (usando natives minimas de axrun)
```

## 6. Añadir una nueva native

1. Añade el id a `AX_NT_*` en `Kernel/vm/axvm.h` (`AX_NT_*`) y actualiza
   `ax_native_namespaces` en `Kernel/vm/axvm.cpp`.
2. Implementa el caso en la funcion nativa del kernel
   (`Kernel/src/vmhost.cpp`) y en el runner de host si quieres probarlo
   fuera del sistema.
3. El compilador solo necesita la tabla de namespaces para resolver
   `namespace.funcion`; no hay que registrar nada mas.

**El orden de los ids es **parte del formato**: cambiarlo rompe la
compatibilidad con los `.axb` ya generados.

### Ejemplo: Añadir una nueva nativa

```c
// En Kernel/vm/axvm.h
enum {
    AX_NT_CORE_PRINT      = 1,
    // ...
    AX_NT_MI_NATIVA       = 24,  // nueva nativa
};

// En Kernel/src/vmhost.cpp
case AX_NT_MI_NATIVA:
    // Implementar funcionalidad nueva
    return 0;

// En el compilador (sema.cpp)
{ "mi_nativa", AX_NT_MI_NATIVA, ... }
```

---

**Siguiente manual**: [11-compilador.md](11-compilador.md)