# El lenguaje Axolyn

Axolyn es el lenguaje propio de AxolynOS. Se parece a C, pero es mas
pequeno y esta pensado para ejecutarse sobre la maquina virtual de
bytecode del kernel.

```axolyn
import console;

const MAX: int = 100;

struct Point {
    x: int,
    y: int,
}

fn sumar(a: int, b: int): int {
    return a + b;
}

fn main() {
    var contador: int = 0;
    var lista: [10]int;

    while (contador < 10) {
        lista[contador] = contador * contador;
        contador = contador + 1;
    }

    if (contador > 5) {
        console.print("muchos\n");
    } else {
        console.print("pocos\n");
    }

    console.write("hola desde Axolyn\n");
}
```

## 1. Comentarios

```
// comentario de una linea
/* comentario
   de varias lineas */
```

## 2. Tipos

| Tipo | Descripcion | Representacion |
|---|---|---|
| `int` | entero de 64 bits con signo | celda de 8 bytes |
| `bool` | booleano | celda de 8 bytes (0 / 1) |
| `string` | cadena de bytes UTF-8 terminada en `\0` | puntero |
| `void` | sin valor | - |
| `*T` | puntero a un `T` | celda de 8 bytes |
| `[N]T` | array de `N` elementos de tipo `T` | puntero a celdas contiguas |
| `Nombre` | struct declarado | puntero a un bloque de celdas |

Todos los valores ocupan una celda de 8 bytes. Los campos de un struct
tambien, por lo que un struct con dos `int` ocupa 16 bytes.

## 3. Declaraciones

### Variables

```
var x: int = 0;          // variable mutable
let y: int = 10;         // constante (no se puede reasignar)
var s: string = "hola";
var p: *int = 0;
var a: [5]int;           // array de 5 enteros (se reserva al declararlo)
```

### Globales

```
var total: int = 0;
var nombre: string = "sistema";
```

Se declaran fuera de cualquier funcion y viven en el segmento de globales
del bytecode.

### Funciones

```
fn sin_valor() {
    console.print("hola\n");
}

fn con_valor(a: int, b: int): int {
    return a + b;
}
```

El punto de entrada del programa es `fn main()`. Puede declarar
parametros (`fn main(argc: int)`) y devolver un `int` (codigo de salida).

### Structs

```
struct Rectangulo {
    ancho: int,
    alto: int,
}

fn area(r: Rectangulo): int {
    return r.ancho * r.alto;
}
```

### Imports

```
import console;
import fs;
```

El `import` da acceso a las funciones nativas de ese namespace.

## 4. Sentencias

| Sentencia | Forma |
|---|---|
| Declaracion | `var x: int = 0;` |
| Asignacion | `x = 1;` `x += 1;` `x -= 1;` `x *= 2;` `x /= 2;` `x %= 2;` |
| Condicional | `if (cond) { ... } else { ... }` |
| Bucle | `while (cond) { ... }` |
| Bucle for | `for (var i: int = 0; i < n; i = i + 1) { ... }` |
| Retorno | `return valor;` / `return;` |
| Salto | `break;` `continue;` |
| Expresion | `una_llamada();` |

La asignacion usa `=` (no `==`). Los comparadores son `==`, `!=`, `<`,
`<=`, `>`, `>=`.

## 5. Expresiones

Prioridad, de mayor a menor:

1. Postfijo: `f(x)`, `a[i]`, `s.campo`
2. Unario: `-x`, `!x`, `*p` (desreferencia), `&x` (direccion)
3. Multiplicativo: `*`, `/`, `%`
4. Aditivo: `+`, `-`
5. Relacional: `<`, `<=`, `>`, `>=`
6. Igualdad: `==`, `!=`
7. `&&`
8. `||`

Los operadores `&&` y `||` cortocircuitan.

Literales: `42`, `-7`, `true`, `false`, `"texto"`.

## 6. Funciones nativas

| Namespace | Funcion | Devuelve |
|---|---|---|
| `core` | `print(s)`, `println(s)`, `print_int(i)`, `strlen(s)`, `int_to_str(i)` | - / `int` / `string` |
| `console` | `write(s)`, `read_line()`, `clear()` | `string` / - |
| `fs` | `open(path)`, `read(fd, buf, len)`, `write(fd, buf, len)`, `close(fd)`, `list()`, `stat(path)` | `int` |
| `proc` | `exit(code)`, `pid()`, `spawn(path)`, `gets(buf, max)` | `int` |
| `mem` | `info()`, `alloc(n)`, `free(p)` | `int` |
| `time` | `now_ms()`, `sleep(ms)` | `int` |

Ejemplos:

```
import console;
import time;

fn main() {
    console.write("hora: ");
    console.print_int(time.now_ms());
    console.write("\n");

    var linea: string = console.read_line();
    console.write("has dicho: ");
    console.write(linea);
    console.write("\n");
}
```

## 7. Compilar y ejecutar

```bash
# compilar a bytecode
build/axc apps/axolyn/hello.ax -o build/apps/hello.axb

# ejecutar en el host (para pruebas rapidas)
build/axrun build/apps/hello.axb

# ver el codigo generado
build/axc apps/axolyn/hello.ax --dump-bytecode
```

Dentro del sistema operativo, la aplicacion se carga desde el initrd y se
lanza con la syscall `AX_SYS_VMRUN` (ver
[08-crear-aplicaciones.md](08-crear-aplicaciones.md)).

## 8. Ejemplos completos

### 8.1 FizzBuzz

```
import core;

fn main() {
    var i: int = 1;
    while (i <= 20) {
        if (i % 15 == 0) {
            core.println("FizzBuzz");
        } else if (i % 3 == 0) {
            core.println("Fizz");
        } else if (i % 5 == 0) {
            core.println("Buzz");
        } else {
            core.print_int(i);
            core.println("");
        }
        i = i + 1;
    }
}
```

### 8.2 Tabla de multiplicar

```
import console;

fn main() {
    var tabla: int = 7;
    var i: int = 1;
    while (i <= 10) {
        console.print_int(tabla);
        console.write(" x ");
        console.print_int(i);
        console.write(" = ");
        console.print_int(tabla * i);
        console.write("\n");
        i = i + 1;
    }
}
```

## 9. Limitaciones actuales

* No hay flotantes (`float`): todo el numero es entero de 64 bits.
* No hay funciones anonymous ni lambdas.
* No hay genericos ni herencia de structs.
* No hay recolector de basura: `mem.alloc` libera la memoria de la VM
  (el heap de la VM es una arena).
* Los arrays tienen tamano fijo en el momento de declararlos.
* `core.print` escribe en la consola del kernel a traves de una native;
  las cadenas deben estar terminadas en `\0`.
