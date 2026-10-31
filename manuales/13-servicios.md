# Servicios del sistema

El **gestor de servicios** de AxolynOS (en `Kernel/src/service.cpp`) es el
punto unico donde se registran y arrancan los subsistemas del kernel.

---

## 1. Como funciona

Al final del arranque (`Kernel/src/kernel.cpp`):

```cpp
ax_service_init();       /* registra los servicios */
ax_service_autostart();  /* arranca los que tienen auto_start */
```

`ax_service_init()` registra todos los servicios conocidos:

| Servicio     | auto_start | start                          | descripcion                      |
|-------------|------------|--------------------------------|----------------------------------|
| kernel      | no         | (nucleo, consola, memoria)    | nucleo del sistema               |
| cpu         | no         | GDT/IDT/PIC/Teclado           | controlador de hardware          |
| scheduler   | no         | planificador                  | gestor de hilos                  |
| syscalls    | no         | tabla de llamadas             | llamadas al sistema              |
| ipc         | no         | IPC entre procesos            | comunicacion mensajes            |
| vfs         | no         | VFS / initrd / AxFS-D         | sistema de ficheros              |
| modules     | no         | gestor de modulos             | carga de .axmod                   |
| power       | si         | ACPI/apagado                  | gestion de energia               |
| pci         | si         | enumeracion PCI               | deteccion de dispositivos PCI    |
| disk        | si         | ATA/AHCI                      | controlador de discos            |
| audio       | si         | audio                         | controlador de audio             |
| video       | si         | video                         | controlador de video             |
| users       | si         | gestor de usuarios            | gestion de cuentas               |
| signals     | si         | senales                       | senales POSIX                    |
| axfs        | si         | AxFS-D en disco               | sistema de ficheros en disco     |
| userdb      | si         | /etc/users.db cifrada         | base de datos de usuarios        |
| registry    | si         | registro del sistema          | registro y configuracion         |
| install     | si         | instalador/formateador        | instalador del sistema           |
| gc          | si         | recolector de recursos        | recuperacion de memoria          |
| network     | si         | TCP/IP e1000                  | pila de red                      |
| shell       | no         | shell del sistema             | interprete de ordenes            |

**Los que tienen `auto_start = true`** se arrancan en orden de registro al
finalizar el arranque. Los demas se pueden arrancar manualmente desde la
shell con `service start <nombre>`.

### Ejemplo: Servicios con auto_start

```text
Al arrancar, estos servicios inician automaticamente:
- power (ACPI/apagado)
- disk (controlador ATA/AHCI)
- network (pila TCP/IP e1000)

Otros servicios deben arrancarse manualmente:
- service start shell   # arrancar el shell
- service stop pci      # parar el controlador PCI (si es posible)
```

### Ejemplo: Arrancar y parar servicios desde shell

```text
# Arrancar el shell
service start shell

# Ver estado actual
services

# Parar un servicio
service stop power
```

### 2. Como agregar un servicio nuevo

1. Anade el wrapper de inicializacion en `Kernel/src/service.cpp`:

```cpp
static void svc_miservicio(void) { ax_miservicio_init(); }
static void svc_miservicio_stop(void) { /* liberacion si la hay */ }
```

2. Registra el servicio en `ax_service_init()`:

```cpp
ax_service_register("miservicio", "descripcion corta del servicio",
                    true, svc_miservicio, svc_miservicio_stop);
```

**Donde `true` significa `auto_start`** (se arranca solo al boot). Si es
`false`, hay que arrancarlo desde la shell:

```text
service start miservicio
```

3. Para servicios que se arrancan solos, basta con `auto_start = true`.
   Para arrancarlos desde la shell:

```text
service start miservicio
service stop miservicio
```

4. Ver estado en el shell:

```text
services
```

### Ejemplo: Agregar un servicio nuevo (power management)

```cpp
/* En Kernel/src/service.cpp */

static void svc_power_init(void) {
    ax_power_init();  // ya existe en ax_power.h
}

static void svc_power_stop(void) {
    // Liberacion de recursos si es necesario
}

// En ax_service_init(), despues de los demas:
ax_service_register("power", "ACPI, apagado y reinicio", true,
                    svc_power_init, svc_power_stop);
```

### 3. Estados y API

- **Estados**: `AX_SERVICE_STOPPED`, `AX_SERVICE_RUNNING`, `AX_SERVICE_FAILED`.
- **Estados por defecto de los servicios del nucleo**: `activo`.
- **API del kernel**:

| Funcion | Descripcion |
|---|---|
| `ax_service_init()` | Registra todos los servicios conocidos |
| `ax_service_register(name, description, auto_start, start, stop)` | Agrega un servicio nuevo |
| `ax_service_mark_started(name)` | Marca un servicio como activo |
| `ax_service_autostart()` | Arranca todos los servicios con auto_start |
| `ax_service_start(name)` | Arranca un servicio por nombre |
| `ax_service_stop(name)` | Para un servicio por nombre |
| `ax_service_enum(index, out)` | Obtiene info de un servicio por indice |
| `ax_service_count()` | Devuelve el numero total de servicios |
| `ax_service_state_name(state)` | Convierte estado a texto |

### Ejemplo: Usar la API de servicios

```c
// Desde un modulo o aplicacion
ax_service_init();  // Ya llamado durante el arranque

// Listar todos los servicios
int count = ax_service_count();
for (int i = 0; i < count; i++) {
    AxServiceInfoK info;
    ax_service_enum(i, &info);
    sdk_print("Servicio [%d]: %s - %s\n", i, info.name, info.description);
}

// Arrancar un servicio especifio
ax_service_start("power");

// Detener un servicio
ax_service_stop("users");
```

### 4. Limitaciones

- Los servicios con `stop` pendiente (por ejemplo drivers hardware) no los
  implementa todavia el runtime; `service stop` marca el estado en el
  registro.
- El shell (`shell` del sistema) es un servicio registrado con
  `auto_start = false` y se marca como activo tras arrancar el shell.

### Ejemplo completo: Ciclo de vida de un servicio

```text
1. Arranque del sistema
   -> ax_service_init() registra todos los servicios
   -> ax_service_autostart() arranca los de auto_start (power, disk, network, ...)
   -> shell se inicia como servicio (auto_start = false, pero se marca activo)

2. Desde la shell del usuario
   -> services                  # ver estado actual
   -> service start network     # arrancar red si no vino auto_start
   -> service stop audio        # parar audio (marca estado stopped)
   -> services                  # ver que audio ahora está stopped

3. Reinicio del sistema
   -> Todo se reinicia, los servicios con auto_start vuelven a arrancar
```

---

**Siguiente manual**: (ninguno, es el ultimo)