# El compilador Axolyn (`axc`)

`axc` es el compilador del lenguaje Axolyn. Traduce un fichero fuente `.ax`
a una imagen de bytecode `.axb` que ejecuta la maquina virtual del kernel.

```
  hola.ax  --[axc]-->  hola.axb  --[VM del kernel]-->  salida
                    |
                    +--> axrun (host) para pruebas rapidas
```

* Lenguaje: [09-lenguaje-axolyn.md](09-lenguaje-axolyn.md)
* Formato del bytecode: [10-bytecode-vm.md](10-bytecode-vm.md)

## 1. Ubicacion de los ficheros

```
compiladores/axc/
  include/axc/
    lexer.h      analizador lexico
    parser.h     analizador sintactico (descendente recursivo)
    ast.h        arbol de sintaxis abstracta + tabla de tipos
    sema.h       analisis semantico (simbolos, tipos, natives)
    codegen.h    generador de bytecode AXB
  src/
    lexer.cpp
    parser.cpp
    ast.cpp
    sema.cpp
    codegen.cpp
    axrun.cpp    runner de host (ejecuta .axb fuera del kernel)
  tests/
    run_tests.sh
    *.ax
```

El compilador se construye como parte de `scripts/build.sh`:

```bash
bash scripts/build.sh compiler
```

que produce:

| Artefacto | Que es |
|---|---|
| `build/axc` | el compilador |
| `build/axrun` | el runner de host (enlaza `Kernel/vm/axvm.cpp`) |

`axrun` reutiliza **el mismo fuente de la VM** que el kernel, de modo que una
prueba que pasa en el host se comporta igual en el sistema.

## 2. Uso en linea de ordenes

```bash
# Compilar
build/axc hola.ax -o build/apps/hola.axb

# Ver el bytecode generado
build/axc hola.ax --dump-bytecode

# Mostrar solo los diagnosticos
build/axc hola.ax -o /dev/null
```

| Opcion | Efecto |
|---|---|
| `-o <fichero>` | nombre del `.axb` de salida |
| `--dump-bytecode` | imprime el desensamblado del AXB por pantalla |
| `--dump-ast` | imprime el arbol de sintaxis abstracta |
| `-h`, `--help` | ayuda |

El compilador devuelve 0 si todo va bien y 1 si hay errores. Los avisos no
cambian el codigo de salida.

## 3. El proceso de compilacion

El compilador es una tuberia de cuatro fases. Cada fase consume la anterior y
deja el resultado en memoria; el fichero `.axb` solo se escribe al final, si
todo ha ido bien.

```
fichero .ax
    |
    v
[1] Lexer      texto  -> lista de tokens
    |
    v
[2] Parser     tokens -> AST
    |
    v
[3] Sema       AST    -> AST anotado (tipos, slots, ids de natives)
    |
    v
[4] Codegen    AST    -> BytecodeImage
    |
    v
  fichero .axb
```

### 3.1 Fase 1: el analizador lexico (`lexer.cpp`)

Convierte el texto en una lista de `Token`. Cada token lleva su posicion
(linea y columna) para poder informar de errores con precision.

Los tipos de token son:

| Categoria | Valores |
|---|---|
| Fins de fichero | `End` |
| Literales | `Ident`, `IntLit`, `StringLit` |
| Palabras clave | `KwFn`, `KwVar`, `KwLet`, `KwIf`, `KwElse`, `KwWhile`, `KwFor`, `KwReturn`, `KwBreak`, `KwContinue`, `KwStruct`, `KwImport`, `KwInt`, `KwBool`, `KwString`, `KwVoid`, `KwTrue`, `KwFalse` |
| Aritmetica | `Plus`, `Minus`, `Star`, `Slash`, `Percent` |
| Asignacion | `Assign`, `PlusAssign`, `MinusAssign`, `StarAssign`, `SlashAssign`, `PercentAssign` |
| Comparacion | `Eq`, `NotEq`, `Lt`, `Le`, `Gt`, `Ge` |
| Logica | `AndAnd`, `OrOr`, `Bang` |
| Punctuacion | `Amp`, `Dot`, `LParen`, `RParen`, `LBrace`, `RBrace`, `LBracket`, `RBracket`, `Comma`, `Semi`, `Colon` |

Reglaslexicas relevantes:

* Los comentarios son `//` hasta el fin de linea y `/* ... */`.
* Los enteros son decimales o hexadecimales (`0x1F`).
* Las cadenas van entre comillas dobles y admiten `\n`, `\t`, `\\`, `\"`,
  `\0` y `\r`.
* Los identificadores son letras, digitos y `_`, y no pueden empezar por un
  digito.

### 3.2 Fase 2: el analizador sintactico (`parser.cpp`)

Parser descendente recursivo. Construye el AST definido en `ast.h`.

Los nodos de expresion son:

| Nodo | Que representa |
|---|---|
| `IntExpr` | literal entero |
| `BoolExpr` | literal `true` / `false` |
| `StringExpr` | literal de cadena |
| `IdentExpr` | identificador (local, global, funcion o namespace) |
| `FieldExpr` | acceso a campo `a.b` |
| `IndexExpr` | indexacion `a[i]` |
| `UnaryExpr` | `-x`, `!x`, `*p`, `&x` |
| `BinaryExpr` | operacion binaria |
| `CallExpr` | llamada a funcion o nativa |
| `AssignExpr` | asignacion (tambien `+=`, `-=`, ...) |
| `NewExpr` | `new T` |

Los nodos de sentencia son:

| Nodo | Que representa |
|---|---|
| `VarDeclStmt` | `var x: int = 0;` / `let x: int = 0;` |
| `ExprStmt` | expresion suelta |
| `AssignStmt` | asignacion como sentencia |
| `BlockStmt` | bloque `{ ... }` |
| `IfStmt` | `if / else` |
| `WhileStmt` | `while` |
| `ForStmt` | `for` |
| `ReturnStmt` | `return` |
| `BreakStmt` / `ContinueStmt` | `break` / `continue` |

Y a nivel de programa: `ImportDecl`, `GlobalDecl`, `FuncDecl`, `StructDef`.

El parser tambien decide la precedencia de operadores, que es:

```
postfijo (llamada, indice, punto)  >  unario  >  * / %  >  + -  >
relacional  >  igualdad  >  &&  >  ||
```

### 3.3 Fase 3: el analisis semantico (`sema.cpp`)

Resuelve nombres, comprueba tipos y asigna las direcciones que luego usa el
generador de codigo. En esta fase:

1. **Se recogen los structs** y se calcula el desplazamiento de cada campo
   (todo alineado a 8 bytes, porque cada valor ocupa una celda de 8 bytes).
2. **Se recogen las funciones** y se les asigna un indice (`FuncDecl::index`).
   Las funciones pueden llamarse entre si en cualquier orden: el indice se
   asigna en una pasada previa, antes de analizar el cuerpo.
3. **Se comprueban los `import`.** Un namespace usado sin `import` es un
   error.
4. **Se analiza cada funcion**, con una tabla de simbolos por ambito
   (bloques anidados crean ambitos nuevos).
5. **Se asignan las celdas locales** (`FuncDecl::localCount`): cada variable
   local y cada parametro ocupa una celda de 8 bytes en el marco (frame).
6. **Se resuelven las globales**: cada una recibe un numero de celda global.
7. **Se resuelven las llamadas nativas**: `console.write` se convierte en un
   identificador numerico de la tabla `AX_NT_*`.

Errores que detecta:

* variable no declarada,
* uso de un `let` como destino de una asignacion,
* numero o tipo incorrecto de argumentos en una llamada,
* tipos incompatibles en una asignacion,
* `break` / `continue` fuera de un bucle,
* `return` con valor en una funcion `void` (y al reves),
* namespace no importado,
* tipo de struct desconocido.

### 3.4 Fase 4: el generador de codigo (`codegen.cpp`)

Traduce el AST anotado a la imagen AXB. La estructura `BytecodeImage` guarda:

| Campo | Contenido |
|---|---|
| `code` | los bytes de codigo (una secuencia de instrucciones) |
| `consts` | la tabla de constantes enteras de 64 bits |
| `strings` | la tabla de cadenas (las direcciones son indices) |
| `funcs` | una entrada por funcion: direccion, numero de parametros, locales |
| `globals` | una entrada por global: celda y tamano |
| `natives` | las funciones nativas usadas y su identificador |
| `imports` | los namespaces importados y su rango de identificadores |

Convenios de generacion:

* La pila de la VM es una pila de valores de 8 bytes.
* Las variables locales viven en el marco de la pila; `AX_OP_LOAD_LOCAL` y
  `AX_OP_STORE_LOCAL` las leen y escriben por indice.
* Las globales viven en un array de celdas; `AX_OP_LOAD_GLOBAL` /
  `AX_OP_STORE_GLOBAL` las acceden por indice.
* Los saltos (`AX_OP_JMP`, `AX_OP_JZ`, ...) llevan un **desplazamiento
  absoluto** dentro del bloque de codigo. El generador usa etiquetas y las
  parchea al final, asi que el codigo se puede reubicar sin recalcular nada.
* `&&` y `||` cortocircuitan: se generan como saltos, no como operadores.
* Una llamada a funcion de usuario se emite como `AX_OP_CALL` (indice de
  funcion y numero de argumentos). Una llamada nativa, como
  `AX_OP_CALL_NATIVE` (identificador nativo y numero de argumentos).

## 4. La tabla de funciones nativas

El compilador y la VM **comparten los mismos identificadores**. La
referencia es `Kernel/vm/axvm.h`:

```c
enum {
    AX_NT_CORE_PRINT      = 1,
    AX_NT_CORE_PRINTLN    = 2,
    ...
    AX_NT_TIME_SLEEP      = 23,
    AX_NT_MAX             = 24
};
```

| Namespace | Rango | Funciones |
|---|---|---|
| `core` | 1 - 5 | `print`, `println`, `print_int`, `strlen`, `int_to_str` |
| `console` | 6 - 8 | `write`, `read_line`, `clear` |
| `fs` | 9 - 14 | `open`, `read`, `write`, `close`, `list`, `stat` |
| `proc` | 15 - 18 | `exit`, `pid`, `spawn`, `gets` |
| `mem` | 19 - 21 | `info`, `alloc`, `free` |
| `time` | 22 - 23 | `now_ms`, `sleep` |

El compilador mantiene su propia copia de esta tabla en
`compiladores/axc/src/sema.cpp` (funcion `nativeTable()`), con la misma
firma de cada nativa: nombre, identificador, tipos de los parametros y tipo
de retorno.

> **Al añadir una nativa nueva hay que tocar los dos lados**: el `enum` de
> `Kernel/vm/axvm.h`, la tabla de `sema.cpp` y la implementacion en el
> anfitrion de la VM (`Kernel/src/vmhost.cpp`).

El fichero `.axb` guarda, en su tabla de imports, el namespace y el primer
identificador de cada namespace, de modo que la VM puede resolver
`ns.fun -> id` sin buscar por nombre.

## 5. Diagnosticos

Todos los errores se recogen en un vector de `Diag`:

```c
struct Diag {
    int         line;
    int         column;
    std::string message;
};
```

y se imprimen con el formato habitual:

```
hola.ax:12:5: error: variable 'x' no declarada
```

El compilador **recoge todos los errores** antes de parar (no se detiene en
el primero), de modo que una sola ejecucion muestra todos los problemas del
fichero.

## 6. Pruebas

```bash
bash scripts/test.sh compiler
```

Ejecuta `compiladores/axc/tests/run_tests.sh`, que:

1. compila cada `.ax` de `tests/` y comprueba que el `.axb` se genera,
2. ejecuta el `.axb` con `build/axrun` y compara la salida con la esperada,
3. comprueba que los programas con errores fallan con codigo de salida 1.

Para depurar un caso concreto:

```bash
build/axc compiladores/axc/tests/fallo.ax --dump-ast
build/axc compiladores/axc/tests/fallo.ax --dump-bytecode
build/axrun build/apps/fallo.axb
```

## 7. Como extender el compilador

### 7.1 Una sentencia nueva

1. Anade el token en `lexer.h` (`TokenType`) y su lexema en `lexer.cpp`.
2. Anade el nodo en `ast.h` (`struct MiSentencia : Stmt`).
3. Anade la regla en `parser.cpp` (que la consuma) y en `ast.cpp` (si hace
   falta un constructor).
4. Anade el analisis en `sema.cpp` (`analyzeStatement`).
5. Anade la generacion en `codegen.cpp` (`genStatement`).

### 7.2 Una opcode nueva

1. Anade el valor en el `enum AxOpcode` de `Kernel/vm/axbytecode.h`.
2. Implementa la ejecucion en `Kernel/vm/axvm.cpp`.
3. Emitela desde `codegen.cpp`.

El opcode debe ocupar el valor siguiente al ultimo y el `AX_OP_MAX` se
ajusta solo.

### 7.3 Un tipo nuevo

1. Anade el `Kind` correspondiente en `TypeRef::Kind` (`ast.h`).
2. Anade el constructor (`makeType...`) y actualiza `describe()`.
3. Anade la comprobacion de compatibilidad en `Sema::typesCompatible`.
4. Si es un tipo con representacion en celdas, no hace falta tocar la VM:
   la VM solo ve celdas de 8 bytes.

## 8. Resumen de ficheros

| Fichero | Responsabilidad |
|---|---|
| `include/axc/lexer.h`, `src/lexer.cpp` | Texto -> tokens |
| `include/axc/parser.h`, `src/parser.cpp` | Tokens -> AST |
| `include/axc/ast.h`, `src/ast.cpp` | Nodos del AST y tipos |
| `include/axc/sema.h`, `src/sema.cpp` | Simbolos, tipos, natives |
| `include/axc/codegen.h`, `src/codegen.cpp` | AST -> AXB |
| `src/main.cpp` | linea de ordenes |
| `src/axrun.cpp` | runner de host |
