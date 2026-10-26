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
#include "ax_pkg.h"

#define AX_SHELL_MAX_LINE 256
#define AX_SHELL_MAX_ARGS  8

/* Declaracion del lanzador de programas bytecode (Kernel/src/vmhost.cpp). */
extern int ax_vm_run(const char* path, const char* const* argv, int argc);

static char g_line[AX_SHELL_MAX_LINE];

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
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool serial_ready(void)
{
    return (inb(AX_SERIAL_PORT + 5) & 0x01) != 0;
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
    return (char)inb(AX_SERIAL_PORT);
}

/* ------------------------------------------------------------------ */
/* Utilidades de texto                                                 */
/* ------------------------------------------------------------------ */
static void shell_prompt(void)
{
    ax_puts("axolyn> ");
}

static void shell_echo(char c)
{
    ax_putc(c);
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

/* ------------------------------------------------------------------ */
/* Ordenes                                                             */
/* ------------------------------------------------------------------ */
static void cmd_help(void)
{
    ax_puts("ordenes disponibles:\n");
    ax_puts("  help              esta ayuda\n");
    ax_puts("  man <tema>        muestra ayuda desde .axh (/help/<tema>.axh)\n");
    ax_puts("  apps              aplicaciones del initrd\n");
    ax_puts("  libs              bibliotecas .axl instaladas (/lib)\n");
    ax_puts("  run <app> [args]  ejecuta una aplicacion nativa (.axe/.elf)\n");
    ax_puts("  ax <app> [args]   ejecuta una aplicacion Axolyn (.axb)\n");
    ax_puts("  mods              modulos cargados\n");
    ax_puts("  mem               informacion de memoria\n");
    ax_puts("  heap              informacion del heap\n");
    ax_puts("  uptime            tiempo desde el arranque\n");
    ax_puts("  threads           hilos activos\n");
    ax_puts("  vm                maquinas virtuales\n");
    ax_puts("  clear             limpia la pantalla\n");
    ax_puts("  reboot            reinicia el sistema\n");
}

static void cmd_apps(void)
{
    char buffer[2048];
    int n = ax_vfs_list(buffer, sizeof(buffer));
    if (n <= 0) {
        ax_puts("no hay ficheros en el initrd\n");
        return;
    }
    ax_puts("ficheros del initrd:\n");
    ax_puts(buffer);
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

    /* La shell espera al proceso: asi se libera su ranura de user mode y el
     * hilo no queda como zombi.  Mientras espera, el planificador ejecuta la
     * aplicacion. */
    if (native) {
        (void)ax_proc_wait((uint64_t)pid);
    }
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
}

static void cmd_reboot(void)
{
    ax_puts("reiniciando...\n");
    ax_reboot();
}

/* ------------------------------------------------------------------ */
/* Bucle principal                                                     */
/* ------------------------------------------------------------------ */
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
    else if (shell_streq(cmd, "mem"))    cmd_mem();
    else if (shell_streq(cmd, "heap"))   cmd_heap();
    else if (shell_streq(cmd, "uptime")) cmd_uptime();
    else if (shell_streq(cmd, "threads")) cmd_threads();
    else if (shell_streq(cmd, "vm"))     cmd_vm();
    else if (shell_streq(cmd, "clear"))  ax_console_clear();
    else if (shell_streq(cmd, "reboot")) cmd_reboot();
    else if (shell_streq(cmd, "run"))    cmd_run(args, count);
    else if (shell_streq(cmd, "ax"))     cmd_ax(args, count);
    else {
        ax_puts("orden desconocida: ");
        ax_puts(cmd);
        ax_puts("\n(escribe 'help')\n");
    }
}

void ax_shell_thread(void* arg)
{
    (void)arg;
    size_t index = 0;

    ax_puts("\n");
    ax_puts("AxolynOS shell. Escribe 'help' para la lista de ordenes.\n");
    shell_prompt();

    for (;;) {
        char c = input_getc();

        if (c == '\r' || c == '\n') {
            g_line[index] = '\0';
            ax_puts("\n");
            shell_execute(g_line);
            index = 0;
            shell_prompt();
            continue;
        }

        if (c == 8 || c == 127) {          /* backspace */
            if (index > 0) {
                index--;
                ax_puts("\b \b");
            }
            continue;
        }

        if (c == 3) {                     /* Ctrl+C */
            ax_puts("\n");
            index = 0;
            shell_prompt();
            continue;
        }

        if (c >= 32 && c < 127) {
            if (index + 1 < sizeof(g_line)) {
                g_line[index++] = c;
                shell_echo(c);
            }
        }
    }
}

void ax_shell_init(void)
{
    ax_thread_create_kernel("shell", ax_shell_thread, 0);
}
