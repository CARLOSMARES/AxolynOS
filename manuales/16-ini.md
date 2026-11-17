# Ficheros INI y desktop.ini

AxolynOS incluye un analizador de ficheros **INI** al estilo de Windows, con
soporte especifico para `desktop.ini`, el fichero que personaliza la
apariencia de una carpeta.

---

## 1. Sintaxis

Un fichero INI es texto plano con secciones, claves y comentarios:

```ini
; Comentario con punto y coma
# Comentario con almohadilla

[general]
nombre = AxolynOS
version = 1.0

[usuario]
tema = "oscuro"
idioma = es
```

Reglas del analizador:

- Las **secciones** se abren con `[nombre]` y solo afectan a las claves que
  siguen.
- Las **claves** tienen la forma `clave = valor`. El separador es el primer
  `=`.
- Los **comentarios** empiezan por `;` o `#` y terminan al final de la linea.
- Las **comillas** dobles o simples que envuelven un valor se eliminan.
- Las busquedas **no distinguen mayusculas** (`[General]` es igual que
  `[general]`).
- Una clave repetida actualiza el valor anterior.
- Las claves antes de cualquier seccion pertenecen a la seccion vacia (`""`).

---

## 2. desktop.ini

`desktop.ini` es un INI normal con una seccion especial:

```ini
[.ShellClassInfo]
DisplayName = Aplicaciones
IconFile = /system/iconos/apps.ico
IconIndex = 2
InfoTip = Aplicaciones instaladas en AxolynOS
```

| Clave         | Descripcion                                        |
|---------------|----------------------------------------------------|
| `DisplayName` | Nombre para mostrar de la carpeta.                 |
| `IconFile`    | Fichero de icono.                                  |
| `IconIndex`   | Indice del icono dentro del fichero.               |
| `IconResource`| Como `IconFile`, pero admite `"fichero,indice"`.   |
| `InfoTip`     | Texto de informacion emergente.                    |

La shell lee `desktop.ini` al listar una carpeta y con la orden `desktop`.
Los valores se extraen con `ax_ini_load_desktop()`.

---

## 3. Ordenes de la shell

| Orden                          | Descripcion                                  |
|--------------------------------|----------------------------------------------|
| `ini <fichero>`                | Lista todas las entradas del fichero.        |
| `ini <fichero> <seccion>`      | Lista las claves de una seccion.             |
| `ini <fichero> <seccion> <clave>` | Muestra el valor de una clave.            |
| `desktop [dir]`                | Muestra los metadatos de `desktop.ini`.      |
| `type <fichero>`               | Muestra el contenido del INI.                |

Ejemplo:

```text
axolyn:/> ini /system/ejemplo.ini
4 entrada(s) en /system/ejemplo.ini:
[general] nombre = AxolynOS
[general] version = 1.0
[general] depuracion = no
[general] arranque_grafico = true
[usuario] tema = oscuro
[usuario] idioma = es
[pantalla] ancho = 80
[pantalla] alto = 25
[red] interfaz = e1000
[red] dhcp = si
[red] ping = 10.0.2.2
[.ShellClassInfo] DisplayName = Carpeta de ejemplo

axolyn:/> ini /system/ejemplo.ini general version
1.0

axolyn:/> desktop /apps
desktop.ini [.ShellClassInfo]:
  carpeta : /apps
  nombre  : Aplicaciones
  icono   : /system/iconos/apps.ico (indice 2)
  info    : Aplicaciones instaladas en AxolynOS
```

---

## 4. Syscalls

Las syscalls de INI son las numeros **58 a 62**:

| Num | Nombre           | Argumentos                                | Devuelve            |
|-----|------------------|-------------------------------------------|---------------------|
| 58  | `AX_SYS_INI_PARSE`| `rdi=ruta`                               | entradas o -errno   |
| 59  | `AX_SYS_INI_GET`  | `rdi=seccion rsi=clave rdx=buf r10=max`  | 0 o -ENOENT         |
| 60  | `AX_SYS_INI_SET`  | `rdi=seccion rsi=clave rdx=valor`        | 0 o -ENOSPC         |
| 61  | `AX_SYS_INI_ENUM` | `rdi=indice rsi=AxIniEntry*`             | 0 o -ENOENT         |
| 62  | `AX_SYS_INI_SAVE` | `rdi=ruta`                               | 0 o -errno          |

### Estructura `AxIniEntry`

```c
typedef struct {
    char section[32];
    char key[32];
    char value[128];
} AxIniEntry;
```

### Ejemplo en C

```c
#include <axos/axos.hpp>

int main(void)
{
    if (axos::ini_parse("/system/ejemplo.ini") < 0) {
        axos::println("no se pudo cargar el INI");
        return 1;
    }

    char value[AX_INI_VALUE_LEN];
    if (axos::ini_get("general", "nombre", value, sizeof(value)) == 0) {
        axos::print("nombre = ");
        axos::println(value);
    }

    axos::ini_set("general", "tema", "claro");
    axos::ini_save("/system/ejemplo.ini");
    return 0;
}
```

---

## 5. API del kernel

El kernel expone el mismo analizador a los servicios y modulos
(`Kernel/include/ax_ini.h`):

```c
int  ax_ini_parse(const char* text, uint64_t size);
int  ax_ini_load(const char* path);
int  ax_ini_save(const char* path);
int  ax_ini_get(const char* section, const char* key, char* out, uint64_t max);
int  ax_ini_set(const char* section, const char* key, const char* value);
int  ax_ini_enum(uint32_t index, AxIniEntry* out);
int  ax_ini_get_int(const char* section, const char* key, int def);
bool ax_ini_get_bool(const char* section, const char* key, bool def);

typedef struct {
    bool valid;
    char display_name[64];
    char icon_file[128];
    char info_tip[256];
    int  icon_index;
} AxIniDesktop;

int  ax_ini_load_desktop(const char* dir, AxIniDesktop* out);
```

La tabla en memoria es estatica (16 secciones, 96 entradas, valores de
hasta 128 caracteres): no usa heap y es segura en el contexto del kernel.
