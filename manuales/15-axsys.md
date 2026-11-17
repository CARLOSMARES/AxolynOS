# Archivos de sistema .axsys

Un **`.axsys`** es un contenedor con metadatos y contenido binario que solo
el kernel puede leer. Es el analogo de los ficheros `.sys` de Windows: sirve
para distribuir modulos, firmwares, controladores o datos de sistema con
informacion de version, fabricante y firma.

---

## 1. Principio de diseno

- **Cabecera propia**: magic `AXSYS001`, version, banderas, tamano y CRC del
  contenido, mas un CRC de la propia cabecera.
- **Metadatos**: hasta 8 pares `clave=valor` (version, compilacion, licencia,
  ...).
- **Firma SHA-256**: el contenido se firma con SHA-256 y el kernel la
  verifica antes de cargarlo.
- **Contenido opaco**: los bytes del contenido se copian a memoria del
  kernel. Las aplicaciones de user mode **no pueden leerlos**, solo los
  metadatos y la firma.

### Formato del fichero

```text
┌──────────────────────────────┬──────────┐
│ AxSysFileHeader              │ 208 bytes│
├──────────────────────────────┼──────────┤
│ AxSysFileMeta * meta_count   │ 80 c/u   │
├──────────────────────────────┼──────────┤
│ Firma SHA-256 del contenido  │ 32 bytes │
├──────────────────────────────┼──────────┤
│ Contenido binario            │ variable │
└──────────────────────────────┴──────────┘
```

### Cabecera (208 bytes, empaquetada)

| Campo           | Tipo     | Descripcion                                  |
|-----------------|----------|----------------------------------------------|
| `magic`         | char[8]  | `"AXSYS001"`                                 |
| `version`       | uint16   | `1`                                          |
| `flags`         | uint16   | `READONLY`, `KERNEL`, `SIGNED`               |
| `meta_count`    | uint32   | Numero de entradas de metadatos (<= 8)       |
| `content_type`  | uint32   | 1 raw, 2 ELF, 3 datos, 4 configuracion       |
| `content_size`  | uint64   | Bytes del contenido                          |
| `content_crc32` | uint64   | CRC-32 IEEE del contenido                    |
| `header_crc32`  | uint64   | CRC-32 de la cabecera con este campo a 0     |
| `name`          | char[32] | Nombre logico del archivo                    |
| `product`       | char[32] | Producto                                     |
| `manufacturer`  | char[32] | Fabricante                                   |
| `description`   | char[64] | Descripcion                                  |
| `reserved`      | uint32   | Reservado (0)                                |

### Entrada de metadatos (80 bytes)

```c
typedef struct {
    char key[16];
    char value[64];
} AxSysFileMeta;
```

---

## 2. Crear un .axsys con mksys

La herramienta de host `tools/mksys.cpp` genera el contenedor:

```bash
mksys <entrada> <salida.axsys> <nombre> <producto> <fabricante> \
      [descripcion] [-k clave=valor]... [-t raw|elf|data|config] [--no-sign]
```

Ejemplo:

```bash
mksys datos.bin controlador.axsys ethernet "Controlador e1000" \
     "AxolynOS Project" "Driver de red" -t elf \
     -k version=1.2 -k licencia=MIT -k pci=8086:100e
```

La herramienta calcula el CRC del contenido, el CRC de la cabecera y la
firma SHA-256, y escribe todo en el fichero de salida.

### Regla de construccion

El Makefile y CMake compilan automaticamente los contenidos de
`apps/sysfiles/*.sys` y los copian al initrd en `/system/*.axsys`:

```bash
make system/axolynos.axsys   # genera build/system/axolynos.axsys
```

---

## 3. Carga en el kernel

Al arrancar, el servicio `sysfiles` llama a `ax_sysfile_init()` y
`ax_sysfile_autoload()`, que recorre el initrd y carga todos los
`/system/*.axsys`.

`ax_sysfile_load(ruta)` hace lo siguiente:

1. Lee el fichero desde el VFS (initrd) o desde AxFS-D.
2. Comprueba el magic, la version y el CRC de la cabecera.
3. Comprueba el CRC del contenido.
4. Comprueba la firma SHA-256 (si el fichero la declara).
5. Copia la cabecera, los metadatos y el contenido a memoria del kernel y
   registra el archivo en una tabla de 16 huecos.

Si alguna comprobacion falla, el fichero se rechaza con un mensaje en consola.

### Ordenes de la shell

| Orden                   | Descripcion                                  |
|-------------------------|----------------------------------------------|
| `sysfiles`              | Lista los archivos cargados con sus metadatos.|
| `sysload <fichero>`     | Carga un `.axsys` del initrd o del disco.     |

Ejemplo:

```text
axolyn:/> sysfiles
archivos de sistema (.axsys):
  [0] axolynos  AxolynOS  AxolynOS Project
      Archivo de sistema de AxolynOS
      tipo=3  bytes=382  crc=53aad025  firma=valida  meta=2
      version = 1.0
      type = system

axolyn:/> sysload /system/axolynos.axsys
archivo de sistema cargado en el indice 0
```

---

## 4. Syscalls

Las syscalls de archivos de sistema son las numeros **55 a 57**:

| Num | Nombre               | Argumentos                             | Devuelve        |
|-----|----------------------|----------------------------------------|-----------------|
| 55  | `AX_SYS_SYSFILE_LIST`| `rdi=indice rsi=AxSysFileInfo*`        | 0 o -ENOENT     |
| 56  | `AX_SYS_SYSFILE_LOAD`| `rdi=ruta`                             | indice o -errno |
| 57  | `AX_SYS_SYSFILE_META`| `rdi=indice rsi=clave rdx=buf r10=max`| 0 o -ENOENT     |

### Estructura `AxSysFileInfo`

```c
typedef struct {
    ax_u32 index;
    ax_u32 flags;
    ax_u32 content_type;
    ax_u64 content_size;
    ax_u64 content_crc32;
    ax_u32 meta_count;
    ax_u32 signature_ok;
    char   name[32];
    char   product[32];
    char   manufacturer[32];
    char   description[64];
} AxSysFileInfo;
```

### Ejemplo en C

```c
#include <axos/axos.hpp>

int main(void)
{
    for (unsigned int i = 0; i < 16; ++i) {
        axos::SysFileInfo info = axos::sysfile_list(i);
        if (info.error < 0) {
            break;
        }
        axos::print(info.name);
        axos::print(" de ");
        axos::println(info.manufacturer);
    }

    axos::sysfile_load("/system/mi_driver.axsys");
    return 0;
}
```

---

## 5. Seguridad

- El contenido de un `.axsys` **nunca** se expone a user mode: no existe una
  syscall para leerlo.
- El kernel comprueba el CRC de la cabecera, el CRC del contenido y la firma
  SHA-256. Un fichero manipulado se rechaza.
- Las aplicaciones pueden consultar los metadatos y la validez de la firma,
  pero no alterar el contenido.
