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

| Servicio     | auto_start | start                          |
|-------------|------------|--------------------------------|
| kernel      | no         | (nucleo, consola, memoria)    |
| cpu         | no         | GDT/IDT/PIC/Teclado           |
| scheduler   | no         | planificador                  |
| syscalls    | no         | tabla de llamadas             |
| ipc         | no         | IPC entre procesos            |
| vfs         | no         | VFS / initrd / AxFS-D         |
| modules     | no         | gestor de modulos             |
| power       | si         | ACPI/apagado                  |
| pci         | si         | enumeracion PCI              |
| disk        | si         | ATA/AHCI                      |
| audio       | si         | audio                         |
| video       | si         | video                         |
| users       | si         | gestor de usuarios            |
| signals     | si         | senales                       |
| axfs        | si         | AxFS-D en disco               |
| userdb      | si         | /etc/users.db cifrada        |
| registry    | si         | registro del sistema         |
| install     | si         | instalador/formateador        |
| gc          | si         | recolector de recursos       |
| network     | si         | TCP/IP e1000                 |
| shell       | no         | shell del sistema            |

Los que tienen `auto_start = true` se arrancan en orden de registro al
finalizar el arranque.

## 2. Como agregar un servicio nuevo

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

## 3. Estados y API

- Estados: `AX_SERVICE_STOPPED`, `AX_SERVICE_RUNNING`, `AX_SERVICE_FAILED`.
- Estados por defecto de los servicios del nucleo: `activo`.
- API del kernel: `ax_service_init`, `ax_service_register`,
  `ax_service_mark_started`, `ax_service_autostart`, `ax_service_start`,
  `ax_service_stop`, `ax_service_enum`, `ax_service_count`,
  `ax_service_state_name`.

---

## 4. Limitaciones

- Los servicios con `stop` pendiente (por ejemplo drivers hardware) no los
  implementa todavia el runtime; `service stop` marca el estado en el registro.
- El shell (`shell` del sistema) es un servicio registrado con
  `auto_start = false` y se marca como activo tras arrancar el shell.
