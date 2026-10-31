/*
 * AxolynOS - Shell del kernel.
 *
 * Un interprete de ordenes minimo que corre como hilo del kernel y lee
 * de la consola serie. Es la herramienta principal para probar el
 * sistema sin depender de las aplicaciones de user mode.
 *
 * Ordenes:
 *   help              esta ayuda
 *   apps              lista las aplicaciones del initrd
 *   libs              lista las bibliotecas .axl de /lib
 *   run <app> [args]  ejecuta una aplicacion nativa (ring 3)
 *   ax <app> [args]   ejecuta una aplicacion Axolyn (bytecode)
 *   mods              lista los modulos cargados
 *   mem               informacion de memoria
 *   heap              informacion del heap del kernel
 *   uptime            tiempo desde el arranque
 *   threads           hilos activos
 *   vm                estado de las maquinas virtuales
 *   clear             limpia la pantalla
 *   reboot            reinicia el sistema
 */
#include "ax_kernel.h"
#include "ax_console.h"
#include "ax_sched.h"
#include "ax_memory.h"
#include "ax_module.h"
#include "ax_axfs.h"
#include "ax_keyboard.h"
#include "ax_signal.h"
#include "ax_pkg.h"
#include "ax_io.h"
#include "ax_disk.h"
#include "ax_fs_disk.h"
#include "ax_install.h"
#include "ax_gc.h"
#include "ax_editor.h"
#include "ax_net.h"
#include "ax_user.h"
#include "ax_service.h"

#define AX_SHELL_MAX_LINE 256
#define AX_SHELL_MAX_ARGS  8
#define AX_SHELL_HISTORY   8
#define AX_SHELL_FILE_MAX  65536

/* Declaracion del lanzador de programas bytecode (Kernel/src/vmhost.cpp). */
extern int ax_vm_run(const char* path, const char* const* argv, int argc);

/* Pantalla de login (definida al final de este fichero). */
static void shell_login_loop(void);

static char g_line[AX_SHELL_MAX_LINE];
static char g_cwd[160] = "/";
static char g_filebuf[AX_SHELL_FILE_MAX];
static char g_history[AX_SHELL_HISTORY][AX_SHELL_MAX_LINE];
static int  g_history_count = 0;

/* El kernel no tiene libc: copia minima propia. */
static void shell_memcpy(char* dst, const char* src, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

/* ------------------------------------------------------------------ */
/* Entrada: teclado PS/2 (IRQ1) y puerto serie COM1                    */
/* ------------------------------------------------------------------ */
static bool serial_ready(void)
{
    return (ax_inb(AX_SERIAL_PORT + 5) & 0x01) != 0;
}

static bool input_ready(void)
{
    return ax_keyboard_has_char() || serial_ready();
}

/*
 * Espera a que haya entrada por cualquiera de las dos fuentes.  Se usa HLT
 * para no consumir CPU: el tick del temporizador despierta el hilo y vuelve
 * a comprobar los buffers.
 */
static char input_getc(void)
{
    while (!input_ready()) {
        asm volatile("hlt");
    }
    if (ax_keyboard_has_char()) {
        return ax_keyboard_getc();
    }
    return (char)ax_inb(AX_SERIAL_PORT);
}

/* Espera un byte como maximo timeout_ms; se usa para leer secuencias CSI. */
static bool input_try(char* out, uint32_t timeout_ms)
{
    uint64_t start = ax_uptime_ms();
    do {
        if (input_ready()) {
            *out = input_getc();
            return true;
        }
        ax_thread_sleep(4);
    } while (ax_uptime_ms() - start < timeout_ms);
    if (input_ready()) {
        *out = input_getc();
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Utilidades de texto                                                 */
/* ------------------------------------------------------------------ */
static void shell_prompt(void)
{
    ax_puts("axolyn:");
    ax_puts(g_cwd);
    ax_puts("> ");
}

static void shell_echo(char c)
{
    ax_putc(c);
}

static void shell_move_left(int n)
{
    if (n > 0) {
        ax_printf("\x1b[%dD", n);
    }
}

static void shell_clear_line(void)
{
    ax_puts("\r");
    ax_puts("\x1b[K");
}

/* ------------------------------------------------------------------ */
/* Rutas y ficheros                                                    */
/* ------------------------------------------------------------------ */
static bool shell_is_abs(const char* s)
{
    return s && s[0] == '/';
}

/*
 * Normaliza una ruta absoluta: elimina '.', resuelve '..' y colapsa las
 * barras.  El resultado siempre empieza por '/'.
 */
static void shell_normalize(const char* in, char* out, size_t max)
{
    size_t len = 1;
    if (!out || max < 2) {
        return;
    }
    out[0] = '/';
    out[1] = '\0';
    if (!in) {
        return;
    }

    const char* p = in;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char* start = p;
        while (*p && *p != '/') {
            p++;
        }
        size_t clen = (size_t)(p - start);
        if (clen == 1 && start[0] == '.') {
            continue;
        }
        if (clen == 2 && start[0] == '.' && start[1] == '.') {
            if (len > 1) {
                len--;                      /* quita la barra final */
                while (len > 1 && out[len - 1] != '/') {
                    len--;
                }
                if (len > 1) {
                    len--;                  /* quita tambien la barra */
                }
                out[len] = '\0';
                if (len == 0) {
                    out[len++] = '/';
                    out[len] = '\0';
                }
            }
            continue;
        }
        if (len > 1) {
            if (len + 1 >= max) {
                break;
            }
            out[len++] = '/';
        }
        for (size_t i = 0; i < clen && len + 1 < max; ++i) {
            out[len++] = start[i];
        }
    }
    out[len] = '\0';
    if (len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
}

/* Resuelve una ruta relativa al directorio actual. */
static void shell_resolve(const char* arg, char* out, size_t max)
{
    if (!arg || !arg[0]) {
        shell_normalize(g_cwd, out, max);
        return;
    }
    if (shell_is_abs(arg)) {
        shell_normalize(arg, out, max);
        return;
    }
    char temp[320];
    size_t j = 0;
    const char* cwd = g_cwd;
    for (size_t i = 0; cwd[i] && j + 1 < sizeof(temp); ++i) {
        temp[j++] = cwd[i];
    }
    if (j == 0 || temp[j - 1] != '/') {
        if (j + 1 < sizeof(temp)) {
            temp[j++] = '/';
        }
    }
    for (size_t i = 0; arg[i] && j + 1 < sizeof(temp); ++i) {
        temp[j++] = arg[i];
    }
    temp[j] = '\0';
    shell_normalize(temp, out, max);
}

static const char* shell_basename(const char* path)
{
    const char* base = path;
    if (!path) {
        return "";
    }
    for (const char* p = path; *p; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    return base;
}

static void shell_error(const char* what, int rc)
{
    ax_puts(what);
    ax_puts(" (error ");
    ax_putdec((int64_t)rc);
    ax_puts(")\n");
}

static bool shell_has_ext(const char* path)
{
    for (const char* p = path; *p; ++p) {
        if (*p == '.') {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Reloj CMOS                                                          */
/* ------------------------------------------------------------------ */
static uint8_t shell_cmos_read(uint8_t reg)
{
    ax_outb(0x70, reg);
    return ax_inb(0x71);
}

static uint8_t shell_bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static void shell_print_2(uint8_t v)
{
    ax_putc((char)('0' + (v / 10) % 10));
    ax_putc((char)('0' + v % 10));
}

static void shell_print_year(uint32_t y)
{
    ax_putc((char)('0' + (y / 1000) % 10));
    ax_putc((char)('0' + (y / 100) % 10));
    ax_putc((char)('0' + (y / 10) % 10));
    ax_putc((char)('0' + y % 10));
}

static void shell_rtc_read(uint8_t* sec, uint8_t* min, uint8_t* hour,
                           uint8_t* day, uint8_t* mon, uint32_t* year)
{
    for (int guard = 0; guard < 100000; ++guard) {
        if ((shell_cmos_read(0x0A) & 0x80) == 0) {
            break;
        }
    }
    *sec  = shell_cmos_read(0x00);
    *min  = shell_cmos_read(0x02);
    *hour = shell_cmos_read(0x04);
    *day  = shell_cmos_read(0x07);
    *mon  = shell_cmos_read(0x08);
    uint8_t yr = shell_cmos_read(0x09);
    uint8_t century = shell_cmos_read(0x32);

    uint8_t regb = shell_cmos_read(0x0B);
    if ((regb & 0x04) == 0) {
        *sec  = shell_bcd(*sec);
        *min  = shell_bcd(*min);
        *hour = (uint8_t)(((*hour & 0x0F) + (((*hour >> 4) & 0x07) * 10)) |
                          (*hour & 0x80));
        *day  = shell_bcd(*day);
        *mon  = shell_bcd(*mon);
        yr    = shell_bcd(yr);
        century = shell_bcd(century);
    }
    if (*hour & 0x80) {
        *hour = (uint8_t)(((*hour & 0x7F) + 12) % 24);
    }
    if (century >= 19 && century <= 21) {
        *year = (uint32_t)century * 100u + yr;
    } else {
        *year = 2000u + yr;
    }
}

static size_t shell_split(char* line, char** args, size_t max)
{
    size_t count = 0;
    char* p = line;

    while (*p != '\0' && count < max) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        args[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return count;
}

static bool shell_streq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static bool shell_starts(const char* text, const char* prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Ordenes                                                             */
/* ------------------------------------------------------------------ */
static void cmd_help(void)
{
    ax_puts("ordenes del sistema:\n");
    ax_puts("  help              esta ayuda\n");
    ax_puts("  man <tema>        ayuda desde .axh (/help/<tema>.axh)\n");
    ax_puts("  apps              aplicaciones del initrd\n");
    ax_puts("  libs              bibliotecas .axl instaladas (/lib)\n");
    ax_puts("  run <app> [args]  ejecuta una aplicacion nativa (.axe/.elf)\n");
    ax_puts("  ax <app> [args]   ejecuta una aplicacion Axolyn (.axb)\n");
    ax_puts("  mods              modulos cargados\n");
    ax_puts("  services          servicios del sistema (estado)\n");
    ax_puts("  service start|stop <nombre>\n");
    ax_puts("  mem               informacion de memoria\n");
    ax_puts("  heap              informacion del heap\n");
    ax_puts("  uptime            tiempo desde el arranque\n");
    ax_puts("  threads           hilos activos\n");
    ax_puts("  vm                maquinas virtuales\n");
    ax_puts("  gc                recolector de basura de procesos\n");
    ax_puts("  cls               limpia la pantalla\n");
    ax_puts("  reboot            reinicia el sistema\n");
    ax_puts("  poweroff          apaga el sistema\n");
    ax_puts("  shutdown          apaga el sistema\n");
    ax_puts("\ndiscos y ficheros (AxFS-D, estilo MS-DOS):\n");
    ax_puts("  disks             discos ATA/AHCI detectados\n");
    ax_puts("  vol               informacion del volumen montado\n");
    ax_puts("  format [n]        formatea el disco n (por defecto 0)\n");
    ax_puts("  mount [n]         monta un volumen AxFS ya formateado\n");
    ax_puts("  install [n]       instala el sistema en el disco n\n");
    ax_puts("  dir [ruta]        lista un directorio\n");
    ax_puts("  cd <ruta>         cambia de directorio\n");
    ax_puts("  pwd               directorio actual\n");
    ax_puts("  type <fich>       muestra un fichero\n");
    ax_puts("  copy <ori> <des>  copia un fichero\n");
    ax_puts("  ren <viejo> <nuevo>  renombra\n");
    ax_puts("  del <fich>        borra un fichero\n");
    ax_puts("  mkdir <dir>       crea un directorio\n");
    ax_puts("  rmdir <dir>       borra un directorio vacio\n");
    ax_puts("  date / time       fecha y hora de la CMOS\n");
    ax_puts("  echo <texto>      escribe texto\n");
    ax_puts("  ver               version del sistema\n");
    ax_puts("\neditores:\n");
    ax_puts("  edit <fich>       editor tipo Notepad (Ctrl+S / Ctrl+Q)\n");
    ax_puts("  vi <fich>         editor modal clasico\n");
    ax_puts("  vim <fich>        vi extendido (:set nu, gg, G)\n");
    ax_puts("  nvim <fich>       el mismo motor con estado NVIM\n");
    ax_puts("\nred:\n");
    ax_puts("  ifconfig          estado de la interfaz e1000\n");
    ax_puts("  dhcp              configura la red por DHCP\n");
    ax_puts("  ping <host>       prueba ICMP\n");
    ax_puts("  nslookup <host>   resuelve un nombre DNS\n");
    ax_puts("  wget <url> [f]    descarga HTTP a AxFS\n");
    ax_puts("  netstat           conexiones y contadores\n");
}

static void cmd_apps(void)
{
    char buffer[4096];
    int n = ax_vfs_list(buffer, sizeof(buffer));
    if (n <= 0) {
        ax_puts("initrd vacio o sin entradas\n");
        return;
    }
    ax_puts("initrd:\n");
    ax_puts(buffer);
    ax_puts("\n");
}

/* Devuelve true si "s" termina en "suffix". */
static bool shell_suffix(const char* s, const char* suffix)
{
    size_t ls = 0, lf = 0;
    while (s[ls]) ls++;
    while (suffix[lf]) lf++;
    if (lf > ls) {
        return false;
    }
    for (size_t i = 0; i < lf; ++i) {
        if (s[ls - lf + i] != suffix[i]) {
            return false;
        }
    }
    return true;
}

/* Lista las bibliotecas .axl instaladas en /lib (cabecera ax_axl_header). */
static void cmd_libs(void)
{
    uint32_t total = ax_vfs_count();
    bool found = false;

    for (uint32_t i = 0; i < total; ++i) {
        const char* name = ax_vfs_name(i);
        if (!name) {
            continue;
        }
        if (name[0] != 'l' || name[1] != 'i' || name[2] != 'b' || name[3] != '/') {
            continue;
        }
        if (!shell_suffix(name, ".axl")) {
            continue;
        }

        uint64_t size = 0;
        const uint8_t* data = (const uint8_t*)ax_vfs_read(name, &size);
        if (!data || size < sizeof(ax_axl_header)) {
            ax_puts("  ");
            ax_puts(name);
            ax_puts("  (fichero demasiado pequeno)\n");
            continue;
        }

        /* Copia byte a byte: el initrd no garantiza alineacion. */
        ax_axl_header hdr;
        uint8_t* dst = (uint8_t*)&hdr;
        for (uint32_t k = 0; k < sizeof(hdr); ++k) {
            dst[k] = data[k];
        }

        char logical[sizeof(hdr.name) + 1];
        for (size_t k = 0; k < sizeof(hdr.name); ++k) {
            logical[k] = hdr.name[k];
        }
        logical[sizeof(hdr.name)] = '\0';

        ax_puts("  ");
        ax_puts(name);
        if (hdr.magic != AX_PKG_MAGIC_AXL) {
            ax_puts("  (firma .axl incorrecta)\n");
            continue;
        }
        if (hdr.version != AX_PKG_VERSION ||
            hdr.kind != AX_PKG_KIND_AR ||
            (uint64_t)hdr.payload_size > size - sizeof(hdr)) {
            ax_puts("  (cabecera .axl no soportada)\n");
            continue;
        }

        uint32_t crc = ax_pkg_crc32(data + sizeof(hdr), hdr.payload_size);

        ax_puts("  ");
        ax_puts(logical);
        ax_puts("  v");
        ax_putdec((int64_t)hdr.version);
        ax_puts("  miembros=");
        ax_putdec((int64_t)hdr.members);
        ax_puts("  bytes=");
        ax_putdec((int64_t)hdr.payload_size);
        ax_puts("  crc=");
        ax_puthex((uint64_t)hdr.payload_crc32);
        if (crc != hdr.payload_crc32) {
            ax_puts("  (CRC INCORRECTO)");
        }
        ax_puts("\n");
        found = true;
    }

    if (!found) {
        ax_puts("no hay bibliotecas .axl en /lib\n");
    }
}

static void cmd_mods(void)
{
    AxModInfo info[AX_MODULES_MAX];
    uint32_t count = ax_module_fill_info(info, AX_MODULES_MAX);
    if (count == 0) {
        ax_puts("no hay modulos cargados\n");
        return;
    }
    ax_puts("modulos:\n");
    for (uint32_t i = 0; i < count; ++i) {
        ax_puts("  [");
        ax_putdec((int64_t)info[i].index);
        ax_puts("] ");
        ax_puts(info[i].name);
        ax_puts("  tamano=");
        ax_putdec((int64_t)info[i].size);
        ax_puts(" vaddr=");
        ax_puthex(info[i].vaddr);
        ax_puts("\n");
    }
}

static void cmd_services(void)
{
    uint32_t total = ax_service_count();
    if (total == 0) {
        ax_puts("no hay servicios registrados\n");
        return;
    }
    ax_puts("servicios:\n");
    for (uint32_t i = 0; i < total; ++i) {
        AxServiceInfoK info;
        if (ax_service_enum(i, &info) != 0) {
            continue;
        }
        ax_puts("  [");
        ax_putdec((int64_t)i);
        ax_puts("] ");
        ax_puts(info.name);
        ax_puts("  estado=");
        ax_puts(ax_service_state_name(info.state));
        ax_puts("  auto=");
        ax_puts(info.auto_start ? "si" : "no");
        ax_puts("  ");
        ax_puts(info.description);
        ax_puts("\n");
    }
}

static void cmd_service(char** args, size_t count)
{
    if (count < 3) {
        ax_puts("uso: service start <nombre> | service stop <nombre>\n");
        return;
    }
    const char* sub = args[1];
    const char* name = args[2];
    int rc = -1;
    bool ok = false;
    if (shell_streq(sub, "start")) {
        ok = (ax_service_start(name) == 0);
    } else if (shell_streq(sub, "stop")) {
        ok = (ax_service_stop(name) == 0);
    } else {
        ax_puts("servicio desconocido: ");
        ax_puts(sub);
        ax_puts("\n");
        return;
    }
    ax_puts(ok ? "servicio " : "no se pudo gestionar el servicio ");
    ax_puts(name);
    ax_puts("\n");
}

static void cmd_mem(void)
{
    AxMemInfo info;
    if (ax_pmm_fill_info(&info) != 0) {
        ax_puts("no se pudo obtener la informacion de memoria\n");
        return;
    }
    ax_puts("memoria:\n  total=");
    ax_putdec((int64_t)info.total_phys);
    ax_puts(" bytes\n  libre=");
    ax_putdec((int64_t)info.free_phys);
    ax_puts(" bytes\n  paginas=");
    ax_putdec((int64_t)info.total_pages);
    ax_puts(" total, ");
    ax_putdec((int64_t)info.free_pages);
    ax_puts(" libres\n  kernel=");
    ax_puthex(info.kernel_virt);
    ax_puts("\n");
}

static void cmd_heap(void)
{
    AxHeapInfo info;
    if (ax_heap_fill_info(&info) != 0) {
        ax_puts("no se pudo obtener la informacion del heap\n");
        return;
    }
    ax_puts("heap: ");
    ax_putdec((int64_t)info.used);
    ax_puts(" / ");
    ax_putdec((int64_t)info.total);
    ax_puts(" bytes, ");
    ax_putdec((int64_t)info.blocks);
    ax_puts(" bloques\n");
}

static void cmd_uptime(void)
{
    ax_puts("uptime: ");
    ax_putdec((int64_t)ax_uptime_ms());
    ax_puts(" ms\n");
}

static void cmd_threads(void)
{
    AxThreadInfo info[AX_MAX_THREADS];
    uint32_t count = ax_thread_fill_info(info, AX_MAX_THREADS);
    ax_puts("hilos:\n");
    for (uint32_t i = 0; i < count; ++i) {
        ax_puts("  ");
        ax_putdec((int64_t)info[i].pid);
        ax_puts("  ");
        ax_puts(info[i].name);
        ax_puts("  estado=");
        ax_putdec((int64_t)info[i].state);
        ax_puts("\n");
    }
}

static void cmd_vm(void)
{
    extern int ax_vm_fill_info(AxVmInfo* out, uint32_t max);
    AxVmInfo info[8];
    uint32_t count = ax_vm_fill_info(info, 8);
    if (count == 0) {
        ax_puts("no hay maquinas virtuales activas\n");
        return;
    }
    ax_puts("maquinas virtuales:\n");
    for (uint32_t i = 0; i < count; ++i) {
        ax_puts("  pid=");
        ax_putdec((int64_t)info[i].pid);
        ax_puts("  ");
        ax_puts(info[i].name);
        ax_puts("  uptime=");
        ax_putdec((int64_t)info[i].uptime_ms);
        ax_puts(" ms\n");
    }
}

/* ------------------------------------------------------------------ */
/* Herramientas de disco, tipo MS-DOS                                  */
/* ------------------------------------------------------------------ */
static void cmd_disks(void)
{
    int n = ax_disk_count();
    if (n == 0) {
        ax_puts("no hay discos ATA/AHCI conectados\n");
        return;
    }
    ax_puts("discos:\n");
    for (int i = 0; i < n; ++i) {
        AxDiskInfo info;
        if (ax_disk_get_info(i, &info) != 0) {
            continue;
        }
        ax_puts("  [");
        ax_putdec(i);
        ax_puts("] ");
        ax_puts(info.name);
        ax_puts("  ");
        ax_putdec((int64_t)(info.sectors / 2048ull));
        ax_puts(" MiB  ");
        ax_putdec((int64_t)info.sectors);
        ax_puts(" sectores");
        if (ax_fs_disk_mounted() && ax_fs_disk_drive() == i) {
            ax_puts("  [AxFS montado]");
        }
        ax_puts("\n");
    }
}

static void cmd_vol(void)
{
    if (!ax_fs_disk_mounted()) {
        ax_puts("vol: no hay volumen AxFS montado (format <disco>)\n");
        return;
    }
    int drive = ax_fs_disk_drive();
    AxDiskInfo info;
    ax_puts("volumen AxFS-D en ");
    if (ax_disk_get_info(drive, &info) == 0) {
        ax_puts(info.name);
    } else {
        ax_puts("disco ");
        ax_putdec(drive);
    }
    ax_puts("\n  total=");
    ax_putdec((int64_t)(ax_fs_disk_total_bytes() / 1024ull));
    ax_puts(" KiB  libre=");
    ax_putdec((int64_t)(ax_fs_disk_free_bytes() / 1024ull));
    ax_puts(" KiB\n  directorio actual: ");
    ax_puts(g_cwd);
    ax_puts("\n");
}

static int cmd_format_drive(int drive)
{
    if (drive < 0 || drive >= ax_disk_count()) {
        ax_puts("format: disco no valido\n");
        return AX_ENOENT;
    }
    AxDiskInfo info;
    ax_disk_get_info(drive, &info);
    ax_puts("format: se perdera todo el contenido de ");
    ax_puts(info.name);
    ax_puts("...\n");
    int rc = ax_install_format(drive);
    if (rc != 0) {
        shell_error("format: error al formatear", rc);
        return rc;
    }
    shell_normalize("/", g_cwd, sizeof(g_cwd));
    ax_puts("format: volumen listo. Usa 'dir' para verlo.\n");
    return 0;
}

static void cmd_mount(char** args, size_t count)
{
    int drive = 0;
    if (count >= 2) {
        int v = 0;
        for (const char* p = args[1]; *p >= '0' && *p <= '9'; ++p) {
            v = v * 10 + (*p - '0');
        }
        drive = v;
    }
    int rc = ax_fs_disk_mount(drive);
    if (rc != 0) {
        shell_error("mount: no se pudo montar el volumen", rc);
        return;
    }
    shell_normalize("/", g_cwd, sizeof(g_cwd));
}

static void cmd_install(char** args, size_t count)
{
    int drive = 0;
    if (count >= 2) {
        int v = 0;
        for (const char* p = args[1]; *p >= '0' && *p <= '9'; ++p) {
            v = v * 10 + (*p - '0');
        }
        drive = v;
    }
    int rc = ax_install_system(drive, true);
    if (rc != 0) {
        shell_error("install: error", rc);
        return;
    }
    shell_normalize("/", g_cwd, sizeof(g_cwd));
}

/* ------------------------------------------------------------------ */
/* Ficheros                                                            */
/* ------------------------------------------------------------------ */
static bool shell_path_is_root(const char* path)
{
    return path[0] == '/' && path[1] == '\0';
}

/* ------------------------------------------------------------------ */
/* Listado estilo MS-DOS                                               */
/* ------------------------------------------------------------------ */

static size_t shell_strlen(const char* s);

typedef struct {
    char     name[64];
    char     base[9];       /* nombre 8.3 en mayusculas */
    char     ext[4];
    bool     dir;
    bool     exec;
    uint64_t size;
} ShellDosEntry;

#define SHELL_DOS_MAX_ENTRIES 128

static ShellDosEntry g_dos_entries[SHELL_DOS_MAX_ENTRIES];
static int            g_dos_count;

static char shell_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static bool shell_name_eq(const char* a, const char* b)
{
    while (*a && *b) {
        if (shell_upper(*a) != shell_upper(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void shell_dos_split(const char* leaf, char* base, char* ext)
{
    size_t len = 0;
    while (leaf[len]) {
        len++;
    }
    size_t dot = len;
    for (size_t i = 0; i < len; ++i) {
        if (leaf[i] == '.') {
            dot = i;
        }
    }

    size_t bi = 0;
    for (size_t i = 0; i < dot && bi < 8; ++i) {
        base[bi++] = shell_upper(leaf[i]);
    }
    base[bi] = '\0';

    size_t ei = 0;
    if (dot < len) {
        for (size_t i = dot + 1; i < len && ei < 3; ++i) {
            ext[ei++] = shell_upper(leaf[i]);
        }
    }
    ext[ei] = '\0';
}

static bool shell_dos_is_exec(const char* leaf)
{
    static const char* exts[] = {
        ".AXE", ".AXB", ".ELF", ".EXE", ".COM", ".BIN", ".BAT", ".AXMOD"
    };
    size_t len = 0;
    while (leaf[len]) {
        len++;
    }
    for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); ++e) {
        size_t el = 0;
        while (exts[e][el]) {
            el++;
        }
        if (len < el) {
            continue;
        }
        bool ok = true;
        for (size_t i = 0; i < el; ++i) {
            if (shell_upper(leaf[len - el + i]) != exts[e][i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

static void shell_dos_add(const char* leaf, bool dir, uint64_t size)
{
    if (!leaf || !leaf[0] || g_dos_count >= SHELL_DOS_MAX_ENTRIES) {
        return;
    }
    size_t len = 0;
    while (leaf[len]) {
        len++;
    }
    if (len >= sizeof(g_dos_entries[0].name)) {
        len = sizeof(g_dos_entries[0].name) - 1;
    }

    char name[64];
    for (size_t i = 0; i < len; ++i) {
        name[i] = leaf[i];
    }
    name[len] = '\0';

    for (int i = 0; i < g_dos_count; ++i) {
        if (shell_name_eq(g_dos_entries[i].name, name)) {
            return;                 /* directorio repetido */
        }
    }

    ShellDosEntry* e = &g_dos_entries[g_dos_count++];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof(e->name)) {
        e->name[i] = name[i];
        i++;
    }
    e->name[i] = '\0';
    shell_dos_split(name, e->base, e->ext);
    e->dir  = dir;
    e->exec = !dir && shell_dos_is_exec(name);
    e->size = dir ? 0 : size;
}

static void shell_dos_sort(void)
{
    for (int i = 1; i < g_dos_count; ++i) {
        ShellDosEntry tmp = g_dos_entries[i];
        int j = i - 1;
        while (j >= 0) {
            const char* a = g_dos_entries[j].name;
            const char* b = tmp.name;
            bool greater = false;
            while (*a && *b) {
                char ca = shell_upper(*a);
                char cb = shell_upper(*b);
                if (ca != cb) {
                    greater = ca > cb;
                    break;
                }
                a++;
                b++;
            }
            if (!*a && *b) {
                greater = false;
            } else if (*a && !*b) {
                greater = true;
            }
            if (!greater) {
                break;
            }
            g_dos_entries[j + 1] = g_dos_entries[j];
            j--;
        }
        g_dos_entries[j + 1] = tmp;
    }
}

static void shell_dos_spaces(int n)
{
    while (n-- > 0) {
        ax_putc(' ');
    }
}

static void shell_dos_size(uint64_t v, char* out, size_t max)
{
    char digits[32];
    int dn = 0;
    if (v == 0) {
        digits[dn++] = '0';
    }
    while (v && dn < (int)sizeof(digits)) {
        digits[dn++] = (char)('0' + (v % 10));
        v /= 10;
    }
    size_t n = 0;
    for (int i = dn - 1; i >= 0; --i) {
        if (n + 2 >= max) {
            break;
        }
        out[n++] = digits[i];
        if (i > 0 && (i % 3) == 0) {
            out[n++] = ',';
        }
    }
    out[n] = '\0';
}

static void shell_dos_display_path(const char* path, char drive)
{
    ax_putc(drive);
    ax_puts(":\\");
    size_t i = 0;
    while (path[i] == '/') {
        i++;
    }
    for (; path[i]; ++i) {
        ax_putc(path[i] == '/' ? '\\' : shell_upper(path[i]));
    }
}

static void shell_dos_print_line(const ShellDosEntry* e,
                                 uint8_t day, uint8_t mon, uint32_t year,
                                 uint8_t hour, uint8_t min)
{
    ax_putc(e->dir ? 'D' : (e->exec ? 'X' : 'F'));
    ax_putc(' ');
    ax_puts(e->base);
    shell_dos_spaces(8 - (int)shell_strlen(e->base));
    ax_putc(' ');

    if (e->dir) {
        shell_dos_spaces(7);
        ax_puts("<DIR>");
        ax_puts("  ");
    } else {
        ax_puts(e->ext);
        shell_dos_spaces(3 - (int)shell_strlen(e->ext));
        char num[32];
        shell_dos_size(e->size, num, sizeof(num));
        shell_dos_spaces(12 - (int)shell_strlen(num));
        ax_puts(num);
        ax_puts("  ");
    }

    shell_print_2(mon);
    ax_putc('-');
    shell_print_2(day);
    ax_putc('-');
    shell_print_2((uint8_t)(year % 100));
    ax_puts("  ");
    uint8_t h12 = (uint8_t)(hour % 12);
    if (h12 == 0) {
        h12 = 12;
    }
    shell_print_2(h12);
    ax_putc(':');
    shell_print_2(min);
    ax_putc(hour >= 12 ? 'p' : 'a');
    ax_putc('\n');
}

static void cmd_dir(char** args, size_t count)
{
    char path[160];
    shell_resolve(count >= 2 ? args[1] : nullptr, path, sizeof(path));

    bool mounted = ax_fs_disk_mounted();
    uint32_t type = 0;
    uint64_t size = 0;
    bool listing_file = false;

    if (!shell_path_is_root(path)) {
        if (mounted) {
            int rc = ax_fs_disk_stat(path, &type, &size);
            if (rc != 0) {
                shell_error("dir: no existe", rc);
                return;
            }
            listing_file = (type == AXFS_DISK_TYPE_FILE);
        } else {
            if (!ax_vfs_exists(path)) {
                ax_puts("dir: no existe\n");
                return;
            }
            size = 0;
            for (uint32_t i = 0; i < ax_vfs_count(); ++i) {
                const AxfsEntry* e = ax_vfs_entry(i);
                if (!e) {
                    continue;
                }
                char full[64];
                size_t n = 0;
                if (e->type == AXFS_TYPE_DIR) {
                    while (e->name[n] && e->name[n] != '/' && n + 2 < sizeof(full)) {
                        full[n] = e->name[n];
                        n++;
                    }
                    full[n] = '\0';
                } else {
                    while (e->name[n] && n + 1 < sizeof(full)) {
                        full[n] = e->name[n];
                        n++;
                    }
                    full[n] = '\0';
                }
                const char* leaf = shell_basename(path);
                if (shell_name_eq(full, leaf)) {
                    size = e->size;
                    type = e->type;
                    break;
                }
            }
            listing_file = (type == AXFS_TYPE_FILE);
        }
    }

    /* Un unico fichero: una sola linea, como MS-DOS. */
    if (listing_file) {
        g_dos_count = 0;
        shell_dos_add(shell_basename(path), false, size);
        uint8_t sec, min, hour, day, mon;
        uint32_t year;
        shell_rtc_read(&sec, &min, &hour, &day, &mon, &year);
        shell_dos_print_line(&g_dos_entries[0], day, mon, year, hour, min);
        return;
    }

    g_dos_count = 0;

    if (mounted) {
        AxfsDiskDirEntry de;
        for (uint32_t i = 0;; ++i) {
            int rc = ax_fs_disk_readdir(path, i, &de);
            if (rc != 0) {
                break;
            }
            shell_dos_add(de.name, de.type == AXFS_DISK_TYPE_DIR, de.size);
        }
    } else {
        char prefix[160];
        size_t plen = 0;
        if (!shell_path_is_root(path)) {
            size_t i = 0;
            while (path[i] == '/') {
                i++;
            }
            while (path[i] && plen + 2 < sizeof(prefix)) {
                prefix[plen++] = path[i++];
            }
            prefix[plen++] = '/';
        }
        prefix[plen] = '\0';

        for (uint32_t i = 0; i < ax_vfs_count(); ++i) {
            const AxfsEntry* e = ax_vfs_entry(i);
            if (!e || !e->name[0]) {
                continue;
            }
            const char* rem = e->name;
            if (plen > 0) {
                size_t k = 0;
                while (k < plen && e->name[k] == prefix[k]) {
                    k++;
                }
                if (k != plen) {
                    continue;
                }
                rem = e->name + plen;
            }
            if (!rem[0]) {
                continue;
            }

            const char* slash = rem;
            while (*slash && *slash != '/') {
                slash++;
            }
            if (*slash == '/') {
                char comp[64];
                size_t n = (size_t)(slash - rem);
                if (n >= sizeof(comp)) {
                    n = sizeof(comp) - 1;
                }
                for (size_t k = 0; k < n; ++k) {
                    comp[k] = rem[k];
                }
                comp[n] = '\0';
                shell_dos_add(comp, true, 0);
            } else if (e->type == AXFS_TYPE_DIR) {
                shell_dos_add(rem, true, 0);
            } else {
                shell_dos_add(rem, false, e->size);
            }
        }
    }

    shell_dos_sort();

    uint8_t sec, min, hour, day, mon;
    uint32_t year;
    shell_rtc_read(&sec, &min, &hour, &day, &mon, &year);

    if (mounted) {
        ax_puts(" Volumen en unidad C: AXOLYNOS\n");
    } else {
        ax_puts(" Volumen en unidad A: AXOLYNOS (initrd)\n");
    }
    ax_puts(" Numero de serie: A0LY-N0S1\n\n");
    ax_puts(" Directorio de ");
    shell_dos_display_path(path, mounted ? 'C' : 'A');
    ax_puts("\n\n");

    uint32_t files = 0;
    uint32_t dirs = 0;
    uint64_t total = 0;
    for (int i = 0; i < g_dos_count; ++i) {
        ShellDosEntry* e = &g_dos_entries[i];
        shell_dos_print_line(e, day, mon, year, hour, min);
        if (e->dir) {
            dirs++;
        } else {
            files++;
            total += e->size;
        }
    }
    if (g_dos_count == 0) {
        ax_puts("  (directorio vacio)\n");
    }

    char num[32];
    shell_dos_size((uint64_t)files, num, sizeof(num));
    ax_puts("\n");
    shell_dos_spaces(14 - (int)shell_strlen(num));
    ax_puts(num);
    ax_puts(" archivo(s)");
    shell_dos_size(total, num, sizeof(num));
    shell_dos_spaces(18 - (int)shell_strlen(num));
    ax_puts(num);
    ax_puts(" bytes\n");
    shell_dos_size((uint64_t)dirs, num, sizeof(num));
    shell_dos_spaces(14 - (int)shell_strlen(num));
    ax_puts(num);
    ax_puts(" dir(es)");
    uint64_t free_bytes = mounted ? ax_fs_disk_free_bytes() : 0;
    shell_dos_size(free_bytes, num, sizeof(num));
    shell_dos_spaces(18 - (int)shell_strlen(num));
    ax_puts(num);
    ax_puts(" bytes libres\n");
}

/* Comprueba en el VFS (initrd) si una ruta ya normalizada es un directorio. */
static bool shell_vfs_is_dir(const char* path)
{
    size_t plen = 0;
    const char* p = path;
    if (p[0] == '/') {
        p++;
    }
    while (p[plen]) {
        plen++;
    }
    uint32_t total = ax_vfs_count();
    for (uint32_t i = 0; i < total; ++i) {
        const AxfsEntry* e = ax_vfs_entry(i);
        if (!e || e->type != AXFS_TYPE_DIR) {
            continue;
        }
        const char* nm = e->name;
        size_t n = 0;
        while (nm[n] && nm[n] != '/') {
            n++;
        }
        if (n != plen) {
            continue;
        }
        bool match = true;
        for (size_t k = 0; k < plen; ++k) {
            if (shell_upper((char)path[1 + k]) != shell_upper(nm[k])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static void cmd_cd(char** args, size_t count)
{
    char path[160];
    shell_resolve(count >= 2 ? args[1] : "/", path, sizeof(path));

    if (!shell_path_is_root(path)) {
        bool ok = false;
        bool is_dir = false;
        if (ax_fs_disk_mounted()) {
            uint32_t type = 0;
            if (ax_fs_disk_stat(path, &type, nullptr) == 0) {
                is_dir = (type == AXFS_DISK_TYPE_DIR);
                ok = true;
            }
        } else if (ax_vfs_exists(path)) {
            /* Sin disco montado el sistema de ficheros activo es el initrd. */
            is_dir = shell_vfs_is_dir(path);
            ok = true;
        }
        if (!ok) {
            shell_error("cd: no existe", -1);
            return;
        }
        if (!is_dir) {
            ax_puts("cd: no es un directorio\n");
            return;
        }
    }

    size_t i = 0;
    while (path[i] && i + 1 < sizeof(g_cwd)) {
        g_cwd[i] = path[i];
        i++;
    }
    g_cwd[i] = '\0';
}

static void cmd_pwd(void)
{
    ax_puts(g_cwd);
    ax_puts("\n");
}

static void cmd_type(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: type <fichero>\n");
        return;
    }
    char path[160];
    shell_resolve(args[1], path, sizeof(path));

    uint64_t got = 0;
    int rc = -1;
    if (ax_fs_disk_mounted()) {
        rc = ax_fs_disk_read(path, g_filebuf, sizeof(g_filebuf) - 1, &got);
    }
    const char* data = g_filebuf;
    if (rc != 0) {
        uint64_t vsize = 0;
        const void* vdata = ax_vfs_read(path, &vsize);
        if (!vdata) {
            shell_error("type: no existe", rc != 0 ? rc : AX_ENOENT);
            return;
        }
        data = (const char*)vdata;
        got = vsize;
    }

    if (got > sizeof(g_filebuf) - 1) {
        got = sizeof(g_filebuf) - 1;
    }
    for (uint64_t i = 0; i < got; ++i) {
        ax_putc(data[i]);
    }
    if (got == 0 || data[got - 1] != '\n') {
        ax_putc('\n');
    }
}

static void cmd_copy(char** args, size_t count)
{
    if (count < 3) {
        ax_puts("uso: copy <origen> <destino>\n");
        return;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("copy: no hay volumen AxFS montado\n");
        return;
    }
    char src[160];
    char dst[160];
    shell_resolve(args[1], src, sizeof(src));
    shell_resolve(args[2], dst, sizeof(dst));

    uint32_t dtype = 0;
    if (ax_fs_disk_stat(dst, &dtype, nullptr) == 0 &&
        dtype == AXFS_DISK_TYPE_DIR) {
        const char* base = shell_basename(src);
        char temp[320];
        size_t j = 0;
        for (size_t i = 0; dst[i] && j + 1 < sizeof(temp); ++i) {
            temp[j++] = dst[i];
        }
        if (j > 0 && temp[j - 1] != '/') {
            temp[j++] = '/';
        }
        for (size_t i = 0; base[i] && j + 1 < sizeof(temp); ++i) {
            temp[j++] = base[i];
        }
        temp[j] = '\0';
        shell_normalize(temp, dst, sizeof(dst));
    }

    uint64_t got = 0;
    int rc = ax_fs_disk_read(src, g_filebuf, sizeof(g_filebuf), &got);
    if (rc != 0) {
        uint64_t vsize = 0;
        const void* vdata = ax_vfs_read(src, &vsize);
        if (!vdata) {
            shell_error("copy: no se pudo leer el origen", rc);
            return;
        }
        if (vsize > sizeof(g_filebuf)) {
            vsize = sizeof(g_filebuf);
        }
        const uint8_t* p = (const uint8_t*)vdata;
        for (uint64_t i = 0; i < vsize; ++i) {
            g_filebuf[i] = (char)p[i];
        }
        got = vsize;
    }

    rc = ax_fs_disk_write(dst, g_filebuf, got);
    if (rc != 0) {
        shell_error("copy: no se pudo escribir el destino", rc);
        return;
    }
    ax_puts("copiados ");
    ax_putdec((int64_t)got);
    ax_puts(" bytes\n");
}

static void cmd_ren(char** args, size_t count)
{
    if (count < 3) {
        ax_puts("uso: ren <antiguo> <nuevo>\n");
        return;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("ren: no hay volumen AxFS montado\n");
        return;
    }
    char a[160];
    char b[160];
    shell_resolve(args[1], a, sizeof(a));
    shell_resolve(args[2], b, sizeof(b));
    int rc = ax_fs_disk_rename(a, b);
    if (rc != 0) {
        shell_error("ren: no se pudo renombrar", rc);
        return;
    }
    ax_puts("renombrado\n");
}

static void cmd_del(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: del <fichero>\n");
        return;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("del: no hay volumen AxFS montado\n");
        return;
    }
    char path[160];
    shell_resolve(args[1], path, sizeof(path));
    int rc = ax_fs_disk_unlink(path);
    if (rc != 0) {
        shell_error("del: no se pudo borrar", rc);
        return;
    }
    ax_puts("borrado\n");
}

static void cmd_mkdir(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: mkdir <directorio>\n");
        return;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("mkdir: no hay volumen AxFS montado\n");
        return;
    }
    char path[160];
    shell_resolve(args[1], path, sizeof(path));
    int rc = ax_fs_disk_mkdir(path);
    if (rc != 0) {
        shell_error("mkdir: no se pudo crear", rc);
        return;
    }
    ax_puts("directorio creado\n");
}

static void cmd_rmdir(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: rmdir <directorio>\n");
        return;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("rmdir: no hay volumen AxFS montado\n");
        return;
    }
    char path[160];
    shell_resolve(args[1], path, sizeof(path));
    int rc = ax_fs_disk_rmdir(path);
    if (rc != 0) {
        shell_error("rmdir: no se pudo borrar", rc);
        return;
    }
    ax_puts("directorio borrado\n");
}

/* ------------------------------------------------------------------ */
/* Utilidades del sistema                                              */
/* ------------------------------------------------------------------ */
static void cmd_cls(void)
{
    ax_puts("\x1b[2J\x1b[H");
    ax_console_clear();
}

static void cmd_ver(void)
{
    ax_puts("AxolynOS 0.2 (Axolyn Kernel) x86_64\n");
    ax_puts("Shell tipo MS-DOS + AxFS-D + editores + red\n");
}

static void cmd_date(void)
{
    uint8_t sec, min, hour, day, mon;
    uint32_t year;
    shell_rtc_read(&sec, &min, &hour, &day, &mon, &year);
    shell_print_2(day);
    ax_putc('/');
    shell_print_2(mon);
    ax_putc('/');
    shell_print_year(year);
    ax_puts("\n");
}

static void cmd_time(void)
{
    uint8_t sec, min, hour, day, mon;
    uint32_t year;
    shell_rtc_read(&sec, &min, &hour, &day, &mon, &year);
    shell_print_2(hour);
    ax_putc(':');
    shell_print_2(min);
    ax_putc(':');
    shell_print_2(sec);
    ax_puts("\n");
}

static void cmd_echo(char** args, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        if (i > 1) {
            ax_putc(' ');
        }
        ax_puts(args[i]);
    }
    ax_putc('\n');
}

static void cmd_gc(void)
{
    int collected = ax_gc_collect();
    AxGcStats st;
    ax_gc_stats(&st);
    ax_puts("recolector de basura (");
    ax_puts(ax_gc_state_name());
    ax_puts(")\n  ultima pasada: ");
    ax_putdec((int64_t)collected);
    ax_puts(" recursos\n  hilos=");
    ax_putdec((int64_t)st.last_threads);
    ax_puts("  ranuras=");
    ax_putdec((int64_t)st.last_slots);
    ax_puts("  fds=");
    ax_putdec((int64_t)st.last_fds);
    ax_puts("  pilas=");
    ax_putdec((int64_t)st.last_stack_bytes);
    ax_puts(" bytes\n  acumulado: hilos=");
    ax_putdec((int64_t)st.total_threads);
    ax_puts("  ranuras=");
    ax_putdec((int64_t)st.total_slots);
    ax_puts("  fds=");
    ax_putdec((int64_t)st.total_fds);
    ax_puts("\n  pendientes=");
    ax_putdec((int64_t)st.pending);
    ax_puts("  memoria libre=");
    ax_putdec((int64_t)(ax_pmm_free_bytes() / 1024ull));
    ax_puts(" KiB\n");
}

static void cmd_editor(char** args, size_t count, int kind)
{
    if (count < 2) {
        const char* name = (kind == AX_EDITOR_MODE_EDIT) ? "edit" :
                           (kind == AX_EDITOR_MODE_VI) ? "vi" :
                           (kind == AX_EDITOR_MODE_VIM) ? "vim" : "nvim";
        ax_puts("uso: ");
        ax_puts(name);
        ax_puts(" <fichero>\n");
        return;
    }
    char path[160];
    shell_resolve(args[1], path, sizeof(path));
    int rc = ax_editor_run(g_cwd, path, kind);
    if (rc != 0) {
        shell_error("editor", rc);
    }
}

/* ------------------------------------------------------------------ */
/* Red                                                                 */
/* ------------------------------------------------------------------ */
static void shell_print_ip(uint32_t ip)
{
    char text[20];
    ax_net_format_ip(ip, text, sizeof(text));
    ax_puts(text);
}

static void cmd_ifconfig(void)
{
    AxNetInfo info;
    ax_net_info(&info);
    if (!info.present) {
        ax_puts("ifconfig: no hay tarjeta e1000\n");
        return;
    }
    char mac[20];
    ax_net_format_mac(info.mac, mac, sizeof(mac));
    ax_puts("eth0  MAC ");
    ax_puts(mac);
    ax_puts("  enlace=");
    ax_puts(info.link ? "activo" : "caido");
    ax_puts("\n  IP       ");
    if (info.configured) {
        shell_print_ip(info.ip);
        ax_puts("  mascara ");
        shell_print_ip(info.mask);
        ax_puts("\n  puerta   ");
        shell_print_ip(info.gateway);
        ax_puts("  DNS ");
        shell_print_ip(info.dns);
        ax_puts(info.dhcp ? "  [DHCP]\n" : "  [manual]\n");
    } else {
        ax_puts("sin configurar (usa 'dhcp')\n");
    }
    ax_puts("  RX=");
    ax_putdec((int64_t)info.rx_packets);
    ax_puts("  TX=");
    ax_putdec((int64_t)info.tx_packets);
    ax_puts("  errores RX=");
    ax_putdec((int64_t)info.rx_errors);
    ax_puts(" TX=");
    ax_putdec((int64_t)info.tx_errors);
    ax_puts("\n");
}

static void cmd_dhcp(void)
{
    ax_puts("dhcp: buscando servidor...\n");
    int rc = ax_net_dhcp();
    if (rc != 0) {
        shell_error("dhcp: no se obtuvo configuracion", rc);
        return;
    }
    AxNetInfo info;
    ax_net_info(&info);
    ax_puts("dhcp: IP ");
    shell_print_ip(info.ip);
    ax_puts("  mascara ");
    shell_print_ip(info.mask);
    ax_puts("\n      puerta ");
    shell_print_ip(info.gateway);
    ax_puts("  DNS ");
    shell_print_ip(info.dns);
    ax_puts("\n");
}

static void cmd_ping(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: ping <host|ip>\n");
        return;
    }
    if (!ax_net_configured()) {
        ax_puts("ping: red sin configurar (usa 'dhcp')\n");
        return;
    }
    uint32_t ip = 0;
    int rc = ax_net_resolve(args[1], &ip);
    if (rc != 0) {
        shell_error("ping: no se pudo resolver", rc);
        return;
    }
    ax_puts("PING ");
    ax_puts(args[1]);
    ax_puts(" (");
    shell_print_ip(ip);
    ax_puts(")\n");
    for (int i = 0; i < 4; ++i) {
        uint32_t rtt = 0;
        rc = ax_net_ping(ip, 1000, &rtt);
        if (rc == 0) {
            ax_puts("  respuesta de ");
            shell_print_ip(ip);
            ax_puts(": tiempo=");
            ax_putdec((int64_t)rtt);
            ax_puts(" ms\n");
        } else {
            ax_puts("  sin respuesta (timeout)\n");
        }
    }
}

static void cmd_nslookup(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: nslookup <nombre>\n");
        return;
    }
    if (!ax_net_configured()) {
        ax_puts("nslookup: red sin configurar (usa 'dhcp')\n");
        return;
    }
    uint32_t ip = 0;
    int rc = ax_net_resolve(args[1], &ip);
    if (rc != 0) {
        shell_error("nslookup: no se pudo resolver", rc);
        return;
    }
    ax_puts(args[1]);
    ax_puts(" -> ");
    shell_print_ip(ip);
    ax_puts("\n");
}

static void cmd_netstat(void)
{
    AxNetInfo info;
    ax_net_info(&info);
    if (!info.present) {
        ax_puts("netstat: no hay interfaz de red\n");
        return;
    }
    ax_puts("estadisticas de red:\n  paquetes RX=");
    ax_putdec((int64_t)info.rx_packets);
    ax_puts("  TX=");
    ax_putdec((int64_t)info.tx_packets);
    ax_puts("\n  ARP peticiones=");
    ax_putdec((int64_t)info.arp_requests);
    ax_puts("  respuestas=");
    ax_putdec((int64_t)info.arp_replies);
    ax_puts("\n  IPv4 entrada=");
    ax_putdec((int64_t)info.ip_in);
    ax_puts("  salida=");
    ax_putdec((int64_t)info.ip_out);
    ax_puts("\n  ICMP entrada=");
    ax_putdec((int64_t)info.icmp_in);
    ax_puts("  salida=");
    ax_putdec((int64_t)info.icmp_out);
    ax_puts("\n  UDP entrada=");
    ax_putdec((int64_t)info.udp_in);
    ax_puts("  salida=");
    ax_putdec((int64_t)info.udp_out);
    ax_puts("\n  TCP entrada=");
    ax_putdec((int64_t)info.tcp_in);
    ax_puts("  salida=");
    ax_putdec((int64_t)info.tcp_out);
    ax_puts("\n");
}

static void cmd_whoami(void)
{
    AxUser* u = ax_user_current();
    if (!u) { ax_puts("guest\n"); return; }
    ax_puts(u->username); ax_puts("\n");
}

static void cmd_users_list(void)
{
    char buf[512];
    if (ax_user_list(buf, sizeof(buf)) < 0) { ax_puts("error\n"); return; }
    ax_puts(buf);
}

static void cmd_login_cmd(char** args, size_t count)
{
    if (count < 3) { ax_puts("uso: login <usuario> <pass>\n"); return; }
    if (ax_user_login(args[1], args[2]) != 0) ax_puts("login fallido\n");
}

static void cmd_logout_cmd(void)
{
    ax_user_logout();
    shell_login_loop();
}

static void cmd_adduser_cmd(char** args, size_t count)
{
    if (count < 3) { ax_puts("uso: adduser <usuario> <pass> [admin]\n"); return; }
    bool admin = (count >= 4 && (args[3][0] == 'a' || args[3][0] == '1'));
    if (ax_user_add(args[1], args[2], admin) != 0) { ax_puts("fallo (requiere root)\n"); return; }
    ax_puts("ok\n");
}

static const char* shell_url_basename(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' && p[1]) {
            base = p + 1;
        }
    }
    if (!*base) {
        return "index.html";
    }
    return base;
}

static void cmd_wget(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: wget <http://host/ruta> [fichero]\n");
        return;
    }
    if (!ax_net_configured()) {
        ax_puts("wget: red sin configurar (usa 'dhcp')\n");
        return;
    }

    const char* url = args[1];
    if (shell_starts(url, "http://")) {
        url += 7;
    } else if (shell_starts(url, "https://")) {
        ax_puts("wget: HTTPS no soportado (sin TLS)\n");
        return;
    }

    const char* host_start = url;
    const char* p = url;
    while (*p && *p != '/' && *p != ':') {
        p++;
    }
    char host[128];
    size_t hn = (size_t)(p - host_start);
    if (hn == 0 || hn >= sizeof(host)) {
        ax_puts("wget: URL no valida\n");
        return;
    }
    for (size_t i = 0; i < hn; ++i) {
        host[i] = host_start[i];
    }
    host[hn] = '\0';

    uint16_t port = 80;
    if (*p == ':') {
        p++;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        if (v > 0 && v < 65536) {
            port = (uint16_t)v;
        }
    }
    const char* path = *p ? p : "/";

    uint64_t got = 0;
    int rc = ax_net_http_get(host, port, path, g_filebuf,
                             sizeof(g_filebuf) - 1, &got);
    if (rc != 0) {
        shell_error("wget: descarga fallida", rc);
        return;
    }
    g_filebuf[got] = '\0';

    if (ax_fs_disk_mounted()) {
        char dest[160];
        const char* name = count >= 3 ? args[2] : shell_url_basename(path);
        shell_resolve(name, dest, sizeof(dest));
        rc = ax_fs_disk_write(dest, g_filebuf, got);
        if (rc != 0) {
            shell_error("wget: no se pudo guardar", rc);
            return;
        }
        ax_puts("descargados ");
        ax_putdec((int64_t)got);
        ax_puts(" bytes -> ");
        ax_puts(dest);
        ax_puts("\n");
    } else {
        ax_puts("wget: sin AxFS; mostrando el cuerpo (");
        ax_putdec((int64_t)got);
        ax_puts(" bytes)\n");
        for (uint64_t i = 0; i < got; ++i) {
            ax_putc(g_filebuf[i]);
        }
        if (got == 0 || g_filebuf[got - 1] != '\n') {
            ax_putc('\n');
        }
    }
}

/*
 * Ejecuta un proceso en primer plano: la shell deja de leer el teclado, el
 * proceso recibe Ctrl+C (SIGINT) y al terminar se vuelve al prompt.  Es el
 * comportamiento clasico de MS-DOS y evita que shell y aplicacion compitan
 * por los mismos caracteres.
 */
static void shell_run_foreground(int pid)
{
    if (pid < 0) {
        return;
    }
    ax_signal_set_foreground((uint64_t)pid);
    int code = ax_proc_wait((uint64_t)pid);
    ax_signal_set_foreground(0);
    if (code == AX_EINTR) {
        ax_puts("^C\n");
    } else if (code > 0) {
        ax_puts("[salida ");
        ax_putdec((int64_t)code);
        ax_puts("]\n");
    }
}

static void cmd_run(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: run <aplicacion> [args...]\n");
        return;
    }

    /* El nombre de la aplicacion se busca en /apps/ */
    char path[128];
    path[0] = '\0';
    if (args[1][0] == '/') {
        for (size_t i = 0; i + 1 < sizeof(path) && args[1][i]; ++i) {
            path[i] = args[1][i];
        }
    } else {
        const char* prefix = "/apps/";
        size_t j = 0;
        for (size_t i = 0; prefix[i] && j + 1 < sizeof(path); ++i) {
            path[j++] = prefix[i];
        }
        for (size_t i = 0; args[1][i] && j + 1 < sizeof(path); ++i) {
            path[j++] = args[1][i];
        }
        path[j] = '\0';
    }

    /* Si no lleva extension se prueba .axe, despues .elf y por ultimo .axb */
    bool has_ext = false;
    for (size_t i = 0; path[i]; ++i) {
        if (path[i] == '.') {
            has_ext = true;
        }
    }

    const char* extra[AX_SHELL_MAX_ARGS];
    size_t extra_count = 0;
    for (size_t i = 2; i < count && extra_count < AX_SHELL_MAX_ARGS; ++i) {
        extra[extra_count++] = args[i];
    }
    extra[extra_count] = 0;

    int pid;
    bool native = false;
    if (!has_ext) {
        char candidate[144];
        size_t len = 0;
        while (path[len] != '\0' && len + 6 < sizeof(candidate)) {
            len++;
        }
        shell_memcpy(candidate, path, len);
        shell_memcpy(candidate + len, ".axe", 5);
        pid = ax_proc_spawn(candidate, extra, (int)extra_count);
        native = (pid >= 0);
        if (pid < 0) {
            shell_memcpy(candidate + len, ".elf", 5);
            pid = ax_proc_spawn(candidate, extra, (int)extra_count);
            native = (pid >= 0);
        }
        if (pid < 0) {
            shell_memcpy(candidate + len, ".axb", 5);
            pid = ax_vm_run(candidate, extra, (int)extra_count);
        }
    } else {
        pid = ax_proc_spawn(path, extra, (int)extra_count);
        native = (pid >= 0);
    }

    if (pid < 0) {
        ax_puts("no se pudo ejecutar: ");
        ax_puts(path);
        ax_puts(" (error ");
        ax_putdec((int64_t)pid);
        ax_puts(")\n");
        return;
    }

    ax_puts("lanzado pid=");
    ax_putdec((int64_t)pid);
    ax_puts("\n");

    (void)native;
    shell_run_foreground(pid);
}

static void cmd_ax(char** args, size_t count)
{
    if (count < 2) {
        ax_puts("uso: ax <aplicacion.axb> [args...]\n");
        return;
    }

    char path[128];
    size_t j = 0;
    const char* prefix = "/apps/";
    if (args[1][0] == '/') {
        for (size_t i = 0; args[1][i] && j + 1 < sizeof(path); ++i) {
            path[j++] = args[1][i];
        }
    } else {
        for (size_t i = 0; prefix[i] && j + 1 < sizeof(path); ++i) {
            path[j++] = prefix[i];
        }
        for (size_t i = 0; args[1][i] && j + 1 < sizeof(path); ++i) {
            path[j++] = args[1][i];
        }
    }
    path[j] = '\0';

    const char* extra[AX_SHELL_MAX_ARGS];
    size_t extra_count = 0;
    for (size_t i = 2; i < count && extra_count < AX_SHELL_MAX_ARGS; ++i) {
        extra[extra_count++] = args[i];
    }
    extra[extra_count] = 0;

    int pid = ax_vm_run(path, extra, (int)extra_count);
    if (pid < 0) {
        ax_puts("no se pudo ejecutar: ");
        ax_puts(path);
        ax_puts("\n");
        return;
    }
    ax_puts("vm iniciada pid=");
    ax_putdec((int64_t)pid);
    ax_puts("\n");
    shell_run_foreground(pid);
}

static void cmd_reboot(void)
{
    ax_puts("reiniciando...\n");
    ax_reboot();
}

/* ------------------------------------------------------------------ */
/* Historial y edicion de la linea                                     */
/* ------------------------------------------------------------------ */
static size_t shell_strlen(const char* s)
{
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static void shell_history_add(const char* line)
{
    if (!line || !line[0]) {
        return;
    }
    if (g_history_count > 0 &&
        shell_streq(g_history[g_history_count - 1], line)) {
        return;
    }
    if (g_history_count == AX_SHELL_HISTORY) {
        for (int i = 1; i < AX_SHELL_HISTORY; ++i) {
            for (size_t j = 0; j < AX_SHELL_MAX_LINE; ++j) {
                g_history[i - 1][j] = g_history[i][j];
            }
        }
        g_history_count--;
    }
    for (size_t i = 0; i < AX_SHELL_MAX_LINE; ++i) {
        g_history[g_history_count][i] = line[i];
    }
    g_history_count++;
}

static void shell_redraw(const char* line, size_t cursor)
{
    ax_puts("\r");
    ax_puts("\x1b[K");
    shell_prompt();
    ax_puts(line);
    size_t len = shell_strlen(line);
    shell_move_left((int)(len - cursor));
}

/* ------------------------------------------------------------------ */
/* Bucle principal                                                     */
/* ------------------------------------------------------------------ */

/* Declaraciones de los nuevos comandos esenciales del sistema. */
/* Estas declaraciones deben estar antes del table de dispatch para que */
/* shell_execute pueda llamarlas.                                       */
static void cmd_cal(void);
static void cmd_cat(char** args, size_t count);
static void cmd_touch(char** args, size_t count);
static void cmd_head(char** args, size_t count);
static void cmd_tail(char** args, size_t count);
static void cmd_wc(char** args, size_t count);
static void cmd_grep(char** args, size_t count);
static void cmd_find(char** args, size_t count);
static void cmd_ps();
static void cmd_kill(char** args, size_t count);
static void cmd_start(char** args, size_t count);
static void cmd_passwd_cmd(char** args, size_t count);
static void cmd_su_cmd(char** args, size_t count);
static void cmd_df();
static void cmd_free();
static void cmd_uname();
static void cmd_which(char** args, size_t count);
static void cmd_history();
static void cmd_services(void);
static void cmd_service(char** args, size_t count);

static void shell_execute(char* line)
{
    char* args[AX_SHELL_MAX_ARGS];
    size_t count = shell_split(line, args, AX_SHELL_MAX_ARGS);
    if (count == 0) {
        return;
    }

    const char* cmd = args[0];

    if (shell_streq(cmd, "help"))        cmd_help();
    else if (shell_streq(cmd, "man")) {
        if (count < 2) {
            ax_puts("uso: man <tema>\n");
        } else {
            char path[128];
            size_t j = 0;
            const char* prefix = "/help/";
            for (size_t i = 0; prefix[i] && j + 1 < sizeof(path); ++i) {
                path[j++] = prefix[i];
            }
            for (size_t i = 0; args[1][i] && j + 1 < sizeof(path); ++i) {
                path[j++] = args[1][i];
            }
            /* Añadir .axh si no tiene extension */
            bool has_ext = false;
            for (size_t i = 0; path[i]; ++i) {
                if (path[i] == '.') {
                    has_ext = true;
                    break;
                }
            }
            if (!has_ext && j + 5 < sizeof(path)) {
                shell_memcpy(path + j, ".axh", 5);
                j += 4;
                path[j] = '\0';
            }
            uint64_t sz = 0;
            const char* data = (const char*)ax_vfs_read(path, &sz);
            if (!data || sz == 0) {
                ax_puts("no se encuentra ayuda: ");
                ax_puts(path);
                ax_puts("\n");
            } else {
                for (uint64_t i = 0; i < sz; ++i) {
                    ax_putc(data[i]);
                }
                if (sz == 0 || data[sz - 1] != '\n') {
                    ax_putc('\n');
                }
            }
        }
    }
    else if (shell_streq(cmd, "apps"))   cmd_apps();
    else if (shell_streq(cmd, "libs"))   cmd_libs();
    else if (shell_streq(cmd, "mods"))   cmd_mods();
    else if (shell_streq(cmd, "services")) cmd_services();
    else if (shell_streq(cmd, "service"))  cmd_service(args, count);
    else if (shell_streq(cmd, "mem"))    cmd_mem();
    else if (shell_streq(cmd, "heap"))   cmd_heap();
    else if (shell_streq(cmd, "uptime")) cmd_uptime();
    else if (shell_streq(cmd, "threads")) cmd_threads();
    else if (shell_streq(cmd, "vm"))     cmd_vm();
    else if (shell_streq(cmd, "gc"))     cmd_gc();
    else if (shell_streq(cmd, "disks"))  cmd_disks();
    else if (shell_streq(cmd, "vol"))    cmd_vol();
    else if (shell_streq(cmd, "dir"))    cmd_dir(args, count);
    else if (shell_streq(cmd, "cd"))     cmd_cd(args, count);
    else if (shell_streq(cmd, "pwd"))    cmd_pwd();
    else if (shell_streq(cmd, "type"))   cmd_type(args, count);
    else if (shell_streq(cmd, "copy"))   cmd_copy(args, count);
    else if (shell_streq(cmd, "ren"))    cmd_ren(args, count);
    else if (shell_streq(cmd, "del"))    cmd_del(args, count);
    else if (shell_streq(cmd, "mkdir"))  cmd_mkdir(args, count);
    else if (shell_streq(cmd, "rmdir"))  cmd_rmdir(args, count);
    else if (shell_streq(cmd, "cls"))    cmd_cls();
    else if (shell_streq(cmd, "clear"))  cmd_cls();
    else if (shell_streq(cmd, "ver"))    cmd_ver();
    else if (shell_streq(cmd, "date"))   cmd_date();
    else if (shell_streq(cmd, "time"))   cmd_time();
    else if (shell_streq(cmd, "echo"))   cmd_echo(args, count);
    else if (shell_streq(cmd, "format")) {
        int drive = 0;
        if (count >= 2) {
            int v = 0;
            for (const char* p = args[1]; *p >= '0' && *p <= '9'; ++p) {
                v = v * 10 + (*p - '0');
            }
            drive = v;
        }
        cmd_format_drive(drive);
    }
    else if (shell_streq(cmd, "mount"))  cmd_mount(args, count);
    else if (shell_streq(cmd, "install")) cmd_install(args, count);
    else if (shell_streq(cmd, "edit"))   cmd_editor(args, count, AX_EDITOR_MODE_EDIT);
    else if (shell_streq(cmd, "vi"))     cmd_editor(args, count, AX_EDITOR_MODE_VI);
    else if (shell_streq(cmd, "vim"))    cmd_editor(args, count, AX_EDITOR_MODE_VIM);
    else if (shell_streq(cmd, "nvim"))   cmd_editor(args, count, AX_EDITOR_MODE_NVIM);
    else if (shell_streq(cmd, "notepad")) cmd_editor(args, count, AX_EDITOR_MODE_EDIT);
    else if (shell_streq(cmd, "ifconfig")) cmd_ifconfig();
    else if (shell_streq(cmd, "dhcp"))    cmd_dhcp();
    else if (shell_streq(cmd, "ping"))    cmd_ping(args, count);
    else if (shell_streq(cmd, "nslookup")) cmd_nslookup(args, count);
    else if (shell_streq(cmd, "wget"))    cmd_wget(args, count);
    else if (shell_streq(cmd, "netstat")) cmd_netstat();
    else if (shell_streq(cmd, "ls")) { char* a2[1] = { (char*)cmd }; cmd_dir(a2, 1); }
    else if (shell_streq(cmd, "whoami")) cmd_whoami();
    else if (shell_streq(cmd, "users"))  cmd_users_list();
    else if (shell_streq(cmd, "login"))  cmd_login_cmd(args, count);
    else if (shell_streq(cmd, "logout")) cmd_logout_cmd();
    else if (shell_streq(cmd, "adduser")) cmd_adduser_cmd(args, count);
    else if (shell_streq(cmd, "reboot")) cmd_reboot();
    else if (shell_streq(cmd, "poweroff") || shell_streq(cmd, "shutdown") || shell_streq(cmd, "halt")) { extern void ax_poweroff(void); ax_poweroff(); }
    else if (shell_streq(cmd, "run"))    cmd_run(args, count);
    else if (shell_streq(cmd, "ax"))     cmd_ax(args, count);
    else {
        /* Intento auto-ejecucion estilo MS-DOS: buscar en / y /apps/ */
        char cand[168];
        int pid = -1;
        bool native_exec = false;
        /* intentar directamente */
        {
            size_t j = 0;
            for (size_t i = 0; cmd[i] && j + 1 < sizeof(cand); ++i) cand[j++] = cmd[i];
            cand[j] = '\0';
            bool has_ext = false;
            for (size_t i = 0; cand[i]; ++i) if (cand[i] == '.') { has_ext = true; break; }
            if (!has_ext) {
                char c2[168]; shell_memcpy(c2, cand, j); shell_memcpy(c2 + j, ".axe", 5);
                pid = ax_proc_spawn(c2, (const char**)args, (int)count);
                if (pid >= 0) native_exec = true;
                if (pid < 0) { shell_memcpy(c2 + j, ".elf", 5); pid = ax_proc_spawn(c2, (const char**)args, (int)count); if (pid >= 0) native_exec = true; }
                if (pid < 0) { shell_memcpy(c2 + j, ".axb", 5); pid = ax_vm_run(c2, (const char**)args, (int)count); if (pid >= 0) native_exec = false; }
            } else {
                pid = ax_proc_spawn(cand, (const char**)args, (int)count); if (pid >= 0) native_exec = true;
                if (pid < 0) { pid = ax_vm_run(cand, (const char**)args, (int)count); if (pid >= 0) native_exec = false; }
            }
        }
        /* intentar en /apps/ */
        if (pid < 0) {
            const char* prefix = "/apps/";
            size_t j = 0;
            for (size_t i = 0; prefix[i] && j + 1 < sizeof(cand); ++i) cand[j++] = prefix[i];
            for (size_t i = 0; cmd[i] && j + 1 < sizeof(cand); ++i) cand[j++] = cmd[i];
            cand[j] = '\0';
            bool has_ext = false;
            for (size_t i = 0; cand[i]; ++i) if (cand[i] == '.') { has_ext = true; break; }
            if (!has_ext) {
                char c2[168]; shell_memcpy(c2, cand, j); shell_memcpy(c2 + j, ".axe", 5);
                pid = ax_proc_spawn(c2, (const char**)args, (int)count); if (pid >= 0) native_exec = true;
                if (pid < 0) { shell_memcpy(c2 + j, ".elf", 5); pid = ax_proc_spawn(c2, (const char**)args, (int)count); if (pid >= 0) native_exec = true; }
                if (pid < 0) { shell_memcpy(c2 + j, ".axb", 5); pid = ax_vm_run(c2, (const char**)args, (int)count); if (pid >= 0) native_exec = false; }
            } else {
                pid = ax_proc_spawn(cand, (const char**)args, (int)count); if (pid >= 0) native_exec = true;
                if (pid < 0) { pid = ax_vm_run(cand, (const char**)args, (int)count); if (pid >= 0) native_exec = false; }
            }
        }
        if (pid >= 0) {
            ax_puts("lanzado pid=");
            ax_putdec((int64_t)pid);
            ax_puts("\n");
            (void)native_exec;
            shell_run_foreground(pid);
            goto done_auto;
        }
        ax_puts("orden desconocida: ");
        ax_puts(cmd);
        ax_puts("\n(escribe 'help')\n");
    done_auto:
        ;
    }
}

/* ------------------------------------------------------------------ */
/* Arranque de sesion: primera cuenta y login                          */
/* ------------------------------------------------------------------ */

static void shell_clear_console(void)
{
    ax_console_clear();
    /* Limpia tambien el terminal serie (QEMU -serial stdio). */
    ax_serial_puts("\x1b[2J\x1b[H");
}

static void shell_read_visible(char* buf, size_t max)
{
    size_t n = 0;
    for (;;) {
        char c = input_getc();
        if (c == '\r' || c == '\n') {
            ax_putc('\n');
            break;
        }
        if ((unsigned char)c == 0x03) {
            n = 0;
            ax_puts("^C\n");
            break;
        }
        if (c == 8 || c == 127) {
            if (n > 0) {
                n--;
                ax_puts("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && n + 1 < max) {
            buf[n++] = c;
            ax_putc(c);
        }
    }
    buf[n] = '\0';
}

static void shell_read_secret(char* buf, size_t max)
{
    size_t n = 0;
    for (;;) {
        char c = input_getc();
        if (c == '\r' || c == '\n') {
            ax_putc('\n');
            break;
        }
        if ((unsigned char)c == 0x03) {
            n = 0;
            ax_puts("^C\n");
            break;
        }
        if (c == 8 || c == 127) {
            if (n > 0) {
                n--;
                ax_puts("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && n + 1 < max) {
            buf[n++] = c;
            ax_putc('*');
        }
    }
    buf[n] = '\0';
}

static bool shell_valid_username(const char* name)
{
    if (!name || !name[0]) {
        return false;
    }
    for (size_t i = 0; name[i]; ++i) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool shell_first_boot_setup(void)
{
    shell_clear_console();
    ax_puts("Bienvenido a AxolynOS\n");
    ax_puts("Primera vez que arrancas el sistema.\n");
    ax_puts("Crea tu cuenta de usuario (se guardara en /etc/passwd).\n\n");

    for (;;) {
        char user[AX_USER_MAX_NAME];
        char pass[AX_USER_MAX_PASS];
        char pass2[AX_USER_MAX_PASS];

        ax_puts("Usuario: ");
        shell_read_visible(user, sizeof(user));
        if (!shell_valid_username(user)) {
            ax_puts("Nombre no valido (letras, numeros, '.', '-' o '_').\n\n");
            continue;
        }

        ax_puts("Contrasena: ");
        shell_read_secret(pass, sizeof(pass));
        if (!pass[0]) {
            ax_puts("La contrasena no puede estar vacia.\n\n");
            continue;
        }

        ax_puts("Repite la contrasena: ");
        shell_read_secret(pass2, sizeof(pass2));
        if (!shell_streq(pass, pass2)) {
            ax_puts("Las contrasenas no coinciden.\n\n");
            continue;
        }

        if (ax_user_setup(user, pass) == 0) {
            ax_puts("\nCuenta creada. Bienvenido, ");
            ax_puts(user);
            ax_puts(".\n");
            ax_thread_sleep(700);
            return true;
        }
        ax_puts("No se pudo crear la cuenta.\n\n");
    }
}

static void shell_login_loop(void)
{
    for (;;) {
        shell_clear_console();
        ax_puts("AxolynOS\n\n");

        char user[AX_USER_MAX_NAME];
        char pass[AX_USER_MAX_PASS];

        ax_puts("Usuario: ");
        shell_read_visible(user, sizeof(user));
        if (!user[0]) {
            continue;
        }

        ax_puts("Contrasena: ");
        shell_read_secret(pass, sizeof(pass));

        if (ax_user_login(user, pass) == 0) {
            return;
        }
        ax_puts("\nLogin incorrecto.\n");
        ax_thread_sleep(600);
    }
}

void ax_shell_thread(void* arg)
{
    (void)arg;
    size_t index = 0;
    size_t cursor = 0;
    int hist_pos = g_history_count;

    if (ax_user_needs_setup()) {
        if (!shell_first_boot_setup()) {
            (void)ax_user_login("root", "root");
        }
    } else if (!ax_user_current()) {
        shell_login_loop();
    }

    /* La pantalla del arranque se limpia: solo queda el prompt. */
    shell_clear_console();

    g_line[0] = '\0';
    ax_puts("AxolynOS shell. Escribe 'help' para la lista de ordenes.\n");
    shell_prompt();

    for (;;) {
        char c = input_getc();

        /* Secuencias de escape: flechas, Inicio, Fin y Supr. */
        if ((unsigned char)c == 0x1B) {
            char a = 0;
            if (!input_try(&a, 35) || a != '[') {
                continue;
            }
            char b = 0;
            if (!input_try(&b, 35)) {
                continue;
            }
            if (b == 'A') {                 /* historial arriba */
                if (g_history_count > 0 && hist_pos > 0) {
                    hist_pos--;
                    size_t n = 0;
                    while (n + 1 < AX_SHELL_MAX_LINE &&
                           g_history[hist_pos][n]) {
                        g_line[n] = g_history[hist_pos][n];
                        n++;
                    }
                    g_line[n] = '\0';
                    index = n;
                    cursor = n;
                    shell_redraw(g_line, cursor);
                }
            } else if (b == 'B') {          /* historial abajo */
                if (hist_pos < g_history_count - 1) {
                    hist_pos++;
                    size_t n = 0;
                    while (n + 1 < AX_SHELL_MAX_LINE &&
                           g_history[hist_pos][n]) {
                        g_line[n] = g_history[hist_pos][n];
                        n++;
                    }
                    g_line[n] = '\0';
                    index = n;
                    cursor = n;
                } else {
                    hist_pos = g_history_count;
                    g_line[0] = '\0';
                    index = 0;
                    cursor = 0;
                }
                shell_redraw(g_line, cursor);
            } else if (b == 'C') {          /* derecha */
                if (cursor < index) {
                    ax_putc(g_line[cursor]);
                    cursor++;
                }
            } else if (b == 'D') {          /* izquierda */
                if (cursor > 0) {
                    cursor--;
                    ax_putc('\b');
                }
            } else if (b == 'H') {          /* Inicio */
                while (cursor > 0) {
                    cursor--;
                    ax_putc('\b');
                }
            } else if (b == 'F') {          /* Fin */
                while (cursor < index) {
                    ax_putc(g_line[cursor]);
                    cursor++;
                }
            } else if (b >= '0' && b <= '9') {
                int num = b - '0';
                char d = 0;
                while (input_try(&d, 20)) {
                    if (d >= '0' && d <= '9') {
                        num = num * 10 + (d - '0');
                    } else {
                        break;
                    }
                }
                if (d == '~') {
                    if (num == 1) {
                        while (cursor > 0) {
                            cursor--;
                            ax_putc('\b');
                        }
                    } else if (num == 3 && cursor < index) {
                        for (size_t i = cursor; i + 1 < index; ++i) {
                            g_line[i] = g_line[i + 1];
                        }
                        index--;
                        g_line[index] = '\0';
                        for (size_t i = cursor; i < index; ++i) {
                            ax_putc(g_line[i]);
                        }
                        ax_putc(' ');
                        shell_move_left((int)(index - cursor + 1));
                    } else if (num == 4) {
                        while (cursor < index) {
                            ax_putc(g_line[cursor]);
                            cursor++;
                        }
                    }
                }
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            g_line[index] = '\0';
            ax_puts("\n");
            shell_history_add(g_line);
            shell_execute(g_line);
            index = 0;
            cursor = 0;
            g_line[0] = '\0';
            hist_pos = g_history_count;
            shell_prompt();
            continue;
        }

        if (c == 8 || c == 127) {          /* backspace */
            if (cursor > 0) {
                for (size_t i = cursor - 1; i + 1 < index; ++i) {
                    g_line[i] = g_line[i + 1];
                }
                cursor--;
                index--;
                g_line[index] = '\0';
                ax_putc('\b');
                for (size_t i = cursor; i < index; ++i) {
                    ax_putc(g_line[i]);
                }
                ax_putc(' ');
                shell_move_left((int)(index - cursor + 1));
            }
            continue;
        }

        if (c == 3) {                     /* Ctrl+C */
            ax_puts("^C\n");
            index = 0;
            cursor = 0;
            g_line[0] = '\0';
            hist_pos = g_history_count;
            shell_prompt();
            continue;
        }

        if (c == 12) {                    /* Ctrl+L */
            cmd_cls();
            shell_redraw(g_line, cursor);
            continue;
        }

        if (c >= 32 && c < 127) {
            if (index + 1 < sizeof(g_line)) {
                for (size_t i = index; i > cursor; --i) {
                    g_line[i] = g_line[i - 1];
                }
                g_line[cursor] = c;
                index++;
                cursor++;
                g_line[index] = '\0';
                ax_putc(c);
                if (cursor < index) {
                    for (size_t i = cursor; i < index; ++i) {
                        ax_putc(g_line[i]);
                    }
                    shell_move_left((int)(index - cursor));
                }
            }
        }
    }
}

void ax_shell_init(void)
{
    ax_thread_create_kernel("shell", ax_shell_thread, 0);
}
