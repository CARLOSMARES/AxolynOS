# Bytecode AXB y la maquina virtual

El compilador `axc` no genera codigo nativo: genera **bytecode AXB** que
ejecuta la maquina virtual que vive dentro del kernel
(`Kernel/vm/axvm.cpp`).

Ventajas de esta decision:

* La VM es pequena y portable (esta escrita en C++ sin dependencias).
* Los programas Axolyn se pueden cargar y ejecutar en el kernel sin
 sin necesitar un enlazador dinamico.
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

### Tablas

* `AxbFunction`: `name_off`, `addr` (offset en el codigo), `argc`,
  `nlocals`, `stack_size`.
* `AxbGlobal`: `name_off`, `slot`, `size`, `flags`.
* `AxbNative`: `name_off`, `id` (id de la tabla de natives).
* `AxbImport`: `ns_off`, `native_base`, `count`.

Las cadenas de las tablas son offsets dentro de `str_data`.

## 2. La maquina

* Pila de valores: celdas de 8 bytes.
* Pila de marcos: cada llamada tiene un marco con sus argumentos y
  variables locales.
* Heap propio: arena con bloques de bytes (cadenas, arrays, structs).
* Celda = 8 bytes siempre. Un array de 5 `int` son 5 celdas; un struct
  con 2 campos son 2 celdas.

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

La definicion exacta (con los operandos) esta en
`Kernel/vm/axbytecode.h`, que es el contrato entre el compilador y la VM.

### 2.2 Convencion de llamada

```
CALL <indice de funcion> <numero de argumentos>
```

Los argumentos se pasan **apilados** en el mismo orden de la declaracion;
la VM copia los `argc` valores al marco nuevo.

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

## 5. La VM en el host

El mismo `Kernel/vm/axvm.cpp` se compila para Linux, y `axrun` lo usa:

```bash
build/axc apps/axolyn/hello.ax -o /tmp/hello.axb
build/axrun /tmp/hello.axb
```

`axrun` implementa las natives minimas (`core.*`) para poder probar
programas sin arrancar el sistema.

## 6. Añadir una nueva native

1. Añade el id a `AX_NT_*` en `Kernel/vm/axvm.h` (al final de su
   namespace) y actualiza `ax_native_namespaces` en `Kernel/vm/axvm.cpp`.
2. Implementa el caso en la funcion nativa del kernel
   (`Kernel/src/vmhost.cpp`) y en el runner de host si quieres probarlo
   fuera del sistema.
3. El compilador solo necesita la tabla de namespaces para resolver
   `namespace.funcion`; no hay que registrar nada mas.

El orden de los ids es **parte del formato**: cambiarlo rompe la
compatibilidad con los `.axb` ya generados.
