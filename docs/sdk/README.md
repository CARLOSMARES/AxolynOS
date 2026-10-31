# SDK AxolynOS - Documentación y Ejemplos

## Bienvenido al SDK de AxolynOS

Este SDK provee headers y librerías para desarrollar:
- **Aplicaciones nativas** (user mode, ring 3)
- **Servicios del sistema** (gestor de servicios)
- **Módulos kernel** (.axmod)
- **Aplicaciones Axolyn** (bytecode .ax/.axb)

El SDK se compone de headers en `apis/include/axos/` y la biblioteca `libaxos` en `apis/src/`. Todo el código del kernel es `freestanding` (sin libc), por lo que el SDK ofrece alternativas seguras para trabajar con el sistema.

---

## Estructura del SDK

```
apis/
├── include/axos/          # Headers C para aplicaciones
├── src/                   # Implementación de libaxos
└── sdk/                   # Documentación y ejemplos
    ├── c/                 # Ejemplos en C
    ├── cpp/               # Ejemplos en C++
    └── README.md          # Esta documentación
```

---

## Requisitos previos

- AxolynOS compilado con `make` o `cmake`
- `libaxos` instalada (se genera automáticamente con `make lib`)
- Entorno de desarrollo: gcc, g++, make (o cmake)

---

## Primeros pasos

### 1. Inicializar el SDK

```c
#include <axos/sdk.h>

int main(void) {
    sdk_init();  /* inicializa el sistema de llamadas al sistema */
    /* ... tu código aquí */
    return 0;
}
```

### 2. Versión del SDK

```c
int version = sdk_version();  /* devuelve la versión del SDK */
```

### 3. Errores y códigos de retorno

Todas las funciones del SDK devuelven `0` en éxito o un valor negativo en error (conforme a la convención `-errno` de AxolynOS).

---

## API principal

### Gestión del sistema

| Función | Descripción |
|---|---|
| `sdk_init()` | Inicializa el sistema de syscalls del SDK |
| `sdk_version()` | Devuelve el número de versión del SDK |
| `sdk_poweroff()` | Apaga el sistema (syscall AX_SYS_POWEROFF) |
| `sdk_reboot()` | Reinicia el sistema (syscall AX_SYS_REBOOT) |
| `sdk_suspend()` | Suspende el sistema (syscall AX_SYS_SUSPEND) |
| `sdk_halt()` | Detiene el CPU (syscall AX_SYS_HALT) |

### Lectura de hardware y sistema

| Función | Descripción |
|---|---|
| `sdk_sysinfo(AxSysInfo* info)` | Llena una estructura con información del sistema |
| `sdk_meminfo(AxMemInfo* info)` | Información de memoria total/libre |

### Servicios del sistema

| Función | Descripción |
|---|---|
| `sdk_service_start(name)` | Arranca un servicio por nombre |
| `sdk_service_stop(name)` | Para un servicio por nombre |
| `sdk_service_list()` | Lista todos los servicios registrados (usa `AxServiceInfo`) |

### Modulos del kernel

| Función | Descripción |
|---|---|
| `sdk_module_load(name)` | Carga un módulo `.axmod` por nombre |
| `sdk_module_unload(index)` | Descarga un módulo por índice |
| `sdk_module_list()` | Lista todos los módulos cargados (usa `AxModInfo`) |

---

### Ejemplo basico: hola mundo en C

```c
#include <axos/sdk.h>
#include <axos/console.h>

int main(void) {
    sdk_init();
    sdk_putstr("Hola desde el SDK de AxolynOS\n");
    sdk_halt();  /* detiene el CPU (o sdk_poweroff() para apagar) */
    return 0;
}
```

### Ejemplo basico: informacion del sistema

```c
#include <axos/sdk.h>
#include <axos/types.h>

void print_system_info(void) {
    AxSysInfo info;
    if (sdk_sysinfo(&info) == 0) {
        sdk_print("Kernel: %s %s\n", info.kernel_name, info.kernel_version);
        sdk_print("CPU: %u hilos\n", info.cpu_count);
        sdk_print("RAM: %u MiB libres\n", info.free_phys / (1024*1024));
    } else {
        sdk_print("Error: No se pudo obtener informacion del sistema\n");
    }
}

int main(void) {
    sdk_init();
    print_sysinfo();
    /* Mantener el sistema corriendo */
    for (;;) {
        /* Aqui podrian ir tareas periodicas */
    }
}
```

### Aplicaciones nativas (user mode)

Las aplicaciones nativas se compilan con el compilador `axc` y enlazan contra `libaxos.a`. Ejemplo de estructura:

```
mi_aplicacion/
├── mi_aplicacion.c
└── Makefile (o usar el Makefile del proyecto)
```

Compilacion basica:

```bash
axc mi_aplicacion.c -o mi_aplicacion.elf
# o usando la librería:
axc mi_aplicacion.c -o mi_aplicacion.elf -laxos
```

### Modulos kernel

Los modulos se cargan desdel espacio de user mode usando syscalls:

```c
/* Cargar un modulo desde user mode */
int ret = sdk_module_load("mi_modulo.axmod");
if (ret >= 0) {
    /* modulo cargado exitosamente */
    /* obtener informacion */
    AxModInfo info;
    if (sdk_module_list(&info, 1) == 0) {
        sdk_print("Modulo: %s, tamano: %u\n", info.name, info.size);
    }
} else {
    /* error al cargar */
    sdk_print("No se pudo cargar el modulo: %d\n", ret);
}
```

### Servicios del sistema

```c
/* Listar servicios */
int count = sdk_service_list();
for (int i = 0; i < count; i++) {
    AxServiceInfo info;
    if (sdk_service_get_info(i, &info) == 0) {
        sdk_print("Servicio [%d]: %s - %s\n", i, info.name, info.description);
    }
}

/* Arrancar/parar un servicio */
sdk_service_start("power");   /* arranca el servicio de energía */
sdk_service_stop("power");    /* para el servicio de energía */
```

---

## Configuracion del editor (VSCode-like)

Para una experiencia similar a VSCode con autocompletado, se recomienda:

1. **Configurar .vscode/settings.json**

```json
{
    "C_Cpp.autocomplete": "Cursor word",
    "C_Cpp.intelliSense.fallbackStyle": "Default",
    "files.exclude": {
        "build": true,
        "*.log": true
    }
}
```

2. **Crear .vscode/c_cpp_properties.json**

```json
{
    "configurations": [
        {
            "name": "AxolynOS SDK",
            "includePath": [
                "${workspaceFolder}/apis/include",
                "${workspaceFolder}/docs/sdk/examples",
                "${workspaceFolder}/build/apps",
                "${workspaceFolder}/build/modules"
            ],
            "compilerPath": "/usr/bin/gcc",
            "intelliSenseMode": "gcc-x64"
        }
    ],
    "version": 4
}
```

3. **Crear .vscode/tasks.json** para compilacion automatica:

```json
{
    "version": "0.1.0",
    "tasks": [
        {
            "taskName": "build-sdk",
            "type": "shell",
            "command": "make lib && make apps",
            "group": "build",
            "problemMatcher": []
        }
    ]
}
```

4. **Extensions recomendadas**:
   - `PlatformIO IDE` - completado avanzado
   - `Clang-Complete` - autocompletado basico
   - `C/C++` - Microsoft

---

## API completa enumerada

### Desde `axos/sdk.h`:

- `sdk_init()`, `sdk_version()`, `sdk_poweroff()`, `sdk_reboot()`, `sdk_suspend()`, `sdk_halt()`
- `sdk_sysinfo(AxSysInfo*)`, `sdk_meminfo(AxMemInfo*)`
- `sdk_disk_read(lba, buf, count)`, `sdk_disk_write(lba, buf, count)`
- `sdk_service_start(name)`, `sdk_service_stop(name)`, `sdk_service_list()`
- `sdk_module_load(name)`, `sdk_module_unload(index)`, `sdk_module_list()`

### Desde `axos/console.h` (salida a consola):

- `sdk_print(const char* fmt, ...)`, `sdk_putstr(const char* s)`
- `sdk_print_uint(val)`, `sdk_print_hex(val)`, `sdk_putc(char)`

### Desde `axos/syscall.h`:

- Todos los `AX_SYS_*` definidos en `apis/include/axos/abi.h`

### Desde `ax_module.h` (modulos):

- `ax_module_init()`, `ax_module_autoload()`
- `ax_module_load(name)`, `ax_module_unload(index)`
- `ax_module_fill_info(AxModInfo*, max)`

---

## Preguntas frecuentes

**P: ¿Necesito reiniciar el sistema cada vez que quiero probar un cambio?**

R: No. Puedes cargar modulos dinamicamente con `module_load()` desde una aplicacion user mode, o reiniciar solo el modulo afectado.

**P: ¿Puedo acceder directamente a memoria fisica desde aplicaciones user mode?**

R: No. El SDK provee funciones seguras (`sdk_disk_read`, `sdk_sysinfo`, etc.) que pasan por el kernel. El acceso directo a memoria fisica esta reservado para modulos kernel y el propio kernel.

**P: ¿El SDK funciona en cualquier arquitectura?**

R: No, esta diseñado específicamente para x86_64 con el microkernel de AxolynOS. Los numeros de syscall y las estructuras son proprietarios de este proyecto.

**P: ¿Puedo contribuir con nuevas APIs al SDK?**

R: Absolutamente. Agrega nuevas funcionalidad en `apis/include/axos/` y `apis/src/`, y actualiza `apis/include/axos/abi.h` con el nuevo numero de syscall correspondiente. Recuerda la "REGLA DE ORO": si cambias algo aqui, cambia el numero de syscall en kernel y la libreria a la vez.

---

## Licencia

AxolynOS SDK - Licencia MIT (o segun la licencia del proyecto padre). Consulta el archivo `LICENSE` en la raiz del proyecto para mas detalles.

## Contacto y soporte

- Issue Tracker: https://github.com/CARLOSMARES/AxolynOS/issues
- Documentacion adicional: `manuales/` carpeta del proyecto
- Comunidad: revisa `manuales/08-crear-aplicaciones.md` para guias paso a paso