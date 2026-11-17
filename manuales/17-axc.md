# Archivos de configuracion .axc

Un **`.axc`** describe una aplicacion: su nombre, version, publicador,
ejecutable, icono y las **conexiones** que tiene con otras aplicaciones.
Es el manifiesto de aplicacion de AxolynOS.

---

## 1. Formato

Un `.axc` es texto plano con dos secciones:

```ini
[app]
name        = calc
version     = 1.2.0
publisher   = AxolynOS Project
exec        = /apps/calc.axe
icon        = /system/icons/calc.ico
description = Calculadora de la shell

[connections]
run         = editor
open        = desktop.ini
depends     = libaxos
```

### Seccion `[app]`

| Clave         | Oligatorio | Descripcion                          |
|---------------|-----------|---------------------------------------|
| `name`        | si        | Nombre unico de la aplicacion.        |
| `version`     | no        | Version (texto libre).                |
| `publisher`   | no        | Publicador o fabricante.              |
| `exec`        | no        | Ruta del ejecutable.                  |
| `icon`        | no        | Ruta del icono.                       |
| `description` | no        | Descripcion corta.                    |

### Seccion `[connections]`

Cada linea es una conexion con otra aplicacion:

```text
tipo = aplicacion destino
```

- `tipo` es libre: `run`, `open`, `depends`, `uses`, ...
- `value` es el nombre de la aplicacion destino (u otra ruta).

---

## 2. Donde viven

Los `.axc` del sistema se guardan en **`/config/`**:

```text
/config/axolynos.axc    configuracion del propio sistema
/config/calc.axc        calculadora
/config/sysinfo.axc     informacion del sistema
/config/pageinfo.axc    paginacion y swap
```

El servicio `axc` los carga automaticamente al arrancar desde el initrd, por
lo que `appinfo` y `axcdemo` funcionan sin hacer nada mas.

### Crear un .axc

Basta crear el fichero en `/config` (o donde se quiera) y cargarlo:

```text
axolyn:/> axc /config/miapp.axc
```

También se puede cargar desde una aplicacion con `axos::axc_load(path)`.

---

## 3. Ordenes de la shell

| Orden                       | Descripcion                                |
|-----------------------------|--------------------------------------------|
| `axc`                       | Lista las aplicaciones configuradas.       |
| `axc list`                  | Igual que `axc`.                           |
| `axc <fichero.axc>`         | Carga y muestra un fichero `.axc`.         |
| `axc info <app>`            | Muestra la configuracion de una aplicacion.|
| `appinfo <app>`             | Atajo de `axc info`.                       |

Ejemplo:

```text
axolyn:/> axc
aplicaciones configuradas (4):
  [0] AxolynOS  v0.1.0
      publicador : AxolynOS Project
      ejecutable: /system/axolyn.elf
      conexion  : config -> ini
      conexion  : sysfiles -> axsys
  [1] calc  v1.2.0
      publicador : AxolynOS Project
      ejecutable: /apps/calc.axe
      conexion  : run -> editor
      conexion  : open -> desktop.ini
      conexion  : depends -> libaxos

axolyn:/> appinfo calc
  [1] calc  v1.2.0
      publicador : AxolynOS Project
      ejecutable: /apps/calc.axe
      descripcion: Calculadora de la shell
      conexion  : run -> editor
      conexion  : open -> desktop.ini
      conexion  : depends -> libaxos
```

---

## 4. Syscalls

Las syscalls de `.axc` son las numeros **63 a 67**:

| Num | Nombre                 | Argumentos                              | Devuelve        |
|-----|------------------------|-----------------------------------------|-----------------|
| 63  | `AX_SYS_AXC_LIST`      | `rdi=indice rsi=AxcInfo*`               | 0 o -ENOENT     |
| 64  | `AX_SYS_AXC_LOAD`      | `rdi=ruta`                              | indice o -errno |
| 65  | `AX_SYS_AXC_CONN`      | `rdi=indice rsi=conexion rdx=AxcConnection*` | 0 o -ENOENT |
| 66  | `AX_SYS_AXC_FIND`      | `rdi=nombre`                            | indice o -ENOENT|
| 67  | `AX_SYS_AXC_CONNECTION`| `rdi=nombre rsi=tipo rdx=buf r10=max`   | 0 o -ENOENT     |

### Estructuras

```c
typedef struct {
    char   name[32];
    char   version[16];
    char   publisher[48];
    char   exec[128];
    char   icon[128];
    char   description[64];
    ax_u32 conn_count;
    ax_u32 reserved;
} AxcInfo;

typedef struct {
    char key[16];
    char value[64];
} AxcConnection;
```

### Ejemplo en C

```c
#include <axos/axos.hpp>

int main(void)
{
    axos::AppConfig c = axos::axc_list(1);
    axos::print(c.name);
    axos::print(" v");
    axos::println(c.version);

    for (unsigned int i = 0; i < c.conn_count; ++i) {
        axos::AppConnection cn = axos::axc_conn(1, i);
        axos::print("  ");
        axos::print(cn.key);
        axos::print(" -> ");
        axos::println(cn.value);
    }
    return 0;
}
```

---

## 5. Estructura de carpetas del sistema

El sistema arranca con este arbol base (en el initrd y en AxFS-D):

```text
/           raiz del sistema
├── apps    aplicaciones instaladas (.axe, .elf, .axb)
├── config  configuracion de aplicaciones (.axc)
├── etc     configuracion del sistema (motd, users.db, hostname)
├── home    carpeta personal de cada usuario (/home/<usuario>)
├── lib     bibliotecas (.axl)
├── system  archivos de sistema protegidos (.axsys)
├── tmp     archivos temporales
└── usr     datos de usuario y programas
```

Al iniciar sesion la shell entra en **`/home/<usuario>`**.  En el initrd, que
es de solo lectura, esa carpeta no existe aun y la shell se queda en `/`; en el
sistema instalado (AxFS-D) se crea automaticamente.

### Aplicaciones ocultas

`dir` y `ls` ocultan las aplicaciones por defecto (`.axe`, `.elf`, `.axb`,
`.axmod`) para no ensuciar los listados: se ejecutan por nombre (`run calc` o
`calc`).  Para verlas:

```text
dir /a
ls -a
```
