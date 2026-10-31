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

**Todos los valores ocupan una celda de 8 bytes**. Los campos de un struct
tambien, por lo que un struct con dos `int` ocupa 16 bytes.

### Ejemplo: Declaracion de tipos

```axolyn
// Variables basicas
var x: int = 0;          // variable mutable
let y: int = 10;         // constante (no se puede reasignar)
var s: string = "hola";
var p: *int = 0;

// Array estatico
var a: [5]int;           // array de 5 enteros (se reserva al declararlo)

// Struct
struct Point {
    x: int,
    y: int,
};

var p: Point;            // variable struct
p.x = 10;
p.y = 20;
```

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

```axolyn
fn sin_valor() {
    console.print("hola\n");
}

fn con_valor(a: int, b: int): int {
    return a + b;
}
```

El punto de entrada del programa es `fn main()`. Puede declarar
parametros (`fn main(argc: int)`) y devolver un `int` (codigo de salida).

### Ejemplo: Funciones con retorno

```axolyn
// Funcion que suma dos numeros
fn sumar(a: int, b: int): int {
    return a + b;
}

// Funcion sin retorno
fn saludar() {
    console.print("Hola!\n");
}

// Punto de entrada
fn main() {
    var resultado: int = sumar(3, 5);  // resultado = 8
    saludar();
}
```

### Structs

```axolyn
struct Rectangulo {
    ancho: int,
    alto: int,
}

fn area(r: Rectangulo): int {
    return r.ancho * r.alto;
}
```

### Ejemplo: Struct con metodos

```axolyn
// Definir un struct
struct Circulo {
    radio: int,
    centro_x: int,
    centro_y: int,
}

// Funcion que calcula el area
fn area_circulo(c: Circulo): int {
    return 3 * c.radio * c.radio;  // Aproximacion de pi
}

// Funcion que imprime el radio
fn imprimir_radio(c: Circulo) {
    console.print("Radio: ");
    console.print_int(c.radio);
    console.write("\n");
}

// Punto de entrada
fn main() {
    var c: Circulo;
    c.radio = 5;
    c.centro_x = 0;
    c.centro_y = 0;
    
    var a: int = area_circulo(c);  // area = 3 * 5 * 5 = 75
    imprimir_radio(c);
}
```

### Imports

```axolyn
import console;
import fs;
```

El `import` da acceso a las funciones nativas de ese namespace.

### Ejemplo: Usar imports

```axolyn
// Importar varios namespaces
import console;
import fs;
import time;

fn main() {
    // Usar funciones de console
    console.print("Hola ");
    
    // Usar funciones de time
    var ahora: int = time.now_ms();
    console.print_int(ahora);
    
    // Abrir un fichero
    var fd: int = fs.open("/apps/texto.txt", "r");
}
```

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

**La asignacion usa `=` (no `==`). Los comparadores son `==`, `!=`, `<`,
`<=`, `>`, `>=`.**

### Ejemplo: Sentencias completas

```axolyn
fn ejemplo() {
    var x: int = 0;          // Declaracion
    
    x = 1;                   // Asignacion simple
    x += 1;                  // Incremento
    x -= 2;                  // Decremento
    
    if (x > 0) {             // Condicional
        console.print("positivo\n");
    } else {
        console.print("negativo\n");
    }
    
    while (x < 10) {         // Bucle
        console.print_int(x);
        console.write("\n");
        x = x + 1;
    }
    
    for (var i: int = 0; i < 5; i = i + 1) {  // Bucle for
        console.print_int(i);
    }
    
    if (x == 5) {            // Comparacion exacta
        console.print("es 5\n");
    }
}
```

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

### Literales

`42`, `-7`, `true`, `false`, `"texto"`.

### Ejemplo: Expresiones completas

```axolyn
fn ejemplo_expresiones() {
    var a: int = 10;
    var b: int = 5;
    var c: int;
    var texto: string;
    
    // Aritmetica
    c = a + b;     // c = 15
    c = a - b;     // c = 5
    c = a * b;     // c = 50
    c = a / b;     // c = 2
    c = a % b;     // c = 0
    
    // Comparacion
    if (a > b) {     // true
        texto = "a es mayor";
    }
    
    if (a >= b) {    // true
        texto = "a es mayor o igual";
    }
    
    if (a < b) {     // false
        // no hacer nada
    }
    
    if (a <= b) {    // false
        // no hacer nada
    }
    
    if (a == b) {    // false
        // no hacer nada
    }
    
    if (a != b) {    // true
        texto = "a no es igual a b";
    }
    
    // Logica corta
    if (a > 0 && b > 0) {
        texto = "ambientes positivos";
    }
    
    if (a > 0 || b < 0) {
        texto = "al menos uno es positivo";
    }
}
```

## 6. Funciones nativas

| Namespace | Funcion | Devuelve |
|---|---|---|
| `core` | `print(s)`, `println(s)`, `print_int(i)`, `strlen(s)`, `int_to_str(i)` | - / `int` / `string` |
| `console` | `write(s)`, `read_line()`, `clear()` | `string` / - |
| `fs` | `open(path)`, `read(fd, buf, len)`, `write(fd, buf, len)`, `close(fd)`, `list()`, `stat(path)` | `int` |
| `proc` | `exit(code)`, `pid()`, `spawn(path)`, `gets(buf, max)` | `int` |
| `mem` | `info()`, `alloc(n)`, `free(p)` | `int` |
| `time` | `now_ms()`, `sleep(ms)` | `int` |

### Ejemplo: Usar funciones nativas

```axolyn
import core;
import time;
import fs;

fn main() {
    // Imprimir con formato
    core.print("El valor es: ");
    core.print_int(42);
    core.println("");
    
    // Dormir 1 segundo
    time.sleep(1000);
    
    // Abrir y leer un fichero
    var fd: int = fs.open("/apps/hello.txt", "r");
    var buf: [64]int;  // buffer de 64 enteros
    var n: int = fs.read(fd, buf, 64);
    fs.close(fd);
    
    // Mostrar el tiempo de ejecucion
    core.print("Tiempo: ");
    core.print_int(time.now_ms());
    core.write(" ms\n");
}
```

### Ejemplo: Programa completo con todas las funciones nativas

```axolyn
import core;
import console;
import time;
import fs;
import proc;

fn main() {
    // Imprimir bienvenida
    core.println("Bienvenido al lenguaje Axolyn");
    
    // Bucle con retardo
    var i: int = 0;
    while (i < 5) {
        core.print_int(i);
        core.write("...");
        i = i + 1;
    }
    core.println("");
    
    // Tiempo
    core.print("Tiempo actual: ");
    core.print_int(time.now_ms());
    core.write(" ms desde el arranque\n");
    
    // Pequena pausa
    time.sleep(100);
    
    // Intentar abrir un fichero que probablemente no exista
    var fd: int = fs.open("/nonexistent.txt", "r");
    if (fd < 0) {
        core.print("No se pudo abrir el fichero\n");
    } else {
        core.print("Fichero abierto correctamente\n");
        fs.close(fd);
    }
    
    // Terminar el proceso con codigo 0
    proc.exit(0);
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

### Ejemplo: Compilar y ver bytecode

```bash
# Compilar
build/axc apps/axolyn/fizzbuzz.ax -o build/apps/fizzbuzz.axb

# Ver el codigo ensamblado (dump-bytecode)
build/axc apps/axolyn/fizzbuzz.ax --dump-bytecode

# Ejecutar en host sin arrancar el SO
build/axrun build/apps/fizzbuzz.axb
```

Dentro del sistema operativo, la aplicacion se carga desde el initrd y se
lanza con la syscall `AX_SYS_VMRUN` (ver
[08-crear-aplicaciones.md](08-crear-aplicaciones.md)).

## 8. Ejemplos completos

### 8.1 FizzBuzz

```axolyn
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

```axolyn
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

### 8.3 Juego adivina el numero

```axolyn
import core;
import time;
import proc;

fn main() {
    // Generar numero aleatorio (usando el tiempo como semilla)
    var semilla: int = time.now_ms();
    core.print("Adivina el numero entre 1 y 100\n");
    
    // Simulacion simple: usamos un numero fijo para el ejemplo
    var numero_secreto: int = 42;
    var intento: int;
    var intentos: int = 0;
    
    while (true) {
        core.print("Intento ");
        core.print_int(intentos + 1);
        core.write(": ");
        // Leer entrada del usuario (simulado)
        // En un caso real, usaríamos consola.read_line()
        intento = 42;  // Para el ejemplo, asumimos que el usuario adivina
        intentos = intentos + 1;
        
        if (intento == numero_secreto) {
            core.println("¡Correcto! El numero es 42");
            core.print("Intentos: ");
            core.print_int(intentos);
            core.write("\n");
            proc.exit(0);
        } else if (intento < numero_secreto) {
            core.println("El numero es mayor");
        } else {
            core.println("El numero es menor");
        }
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

---

**Siguiente manual**: [10-bytecode-vm.md](10-bytecode-vm.md)