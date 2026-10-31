/*
 * AxolynOS - Gestor de servicios de arranque.
 */
#include "ax_service.h"
#include "ax_console.h"
#include "ax_splash.h"
#include "ax_power.h"
#include "ax_pci.h"
#include "ax_disk.h"
#include "ax_audio.h"
#include "ax_video.h"
#include "ax_user.h"
#include "ax_signal.h"
#include "ax_fs_disk.h"
#include "ax_registry.h"
#include "ax_install.h"
#include "ax_gc.h"
#include "ax_net.h"

struct Service {
    char     name[AX_SERVICE_NAME];
    char     description[AX_SERVICE_DESC];
    uint32_t state;
    bool     auto_start;
    void   (*start)(void);
    void   (*stop)(void);
};

static Service g_services[AX_SERVICE_MAX];
static int     g_count = 0;

static int svc_strlen(const char* s)
{
    int n = 0;
    if (s) {
        while (s[n]) {
            ++n;
        }
    }
    return n;
}

static void svc_copy(char* dst, uint64_t max, const char* src)
{
    uint64_t i = 0;
    if (!dst || max == 0) {
        return;
    }
    if (src) {
        for (; i + 1 < max && src[i]; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static bool svc_streq(const char* a, const char* b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int svc_find(const char* name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < g_count; ++i) {
        if (svc_streq(g_services[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Envoltorios de inicializacion                                       */
/* ------------------------------------------------------------------ */
static void svc_power(void)    { ax_power_init(); }
static void svc_pci(void)      { ax_pci_init(); }
static void svc_disk(void)     { ax_disk_init(); }
static void svc_audio(void)    { ax_audio_init(); }
static void svc_video(void)    { ax_video_init(); }
static void svc_users(void)    { ax_user_init(); }
static void svc_signals(void)  { ax_signal_init(); }
static void svc_axfs(void)     { ax_fs_disk_init(); }
static void svc_userdb(void)   { ax_user_boot(); }
static void svc_registry(void) { ax_registry_init(); }
static void svc_install(void)  { ax_install_init(); }
static void svc_gc(void)       { ax_gc_init(); }
static void svc_net(void)      { ax_net_init(); }

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void ax_service_init(void)
{
    g_count = 0;

    /* Servicios ya inicializados antes de crear el gestor. */
    ax_service_register("kernel", "nucleo, consola y memoria", false, 0, 0);
    ax_service_register("cpu", "GDT, TSS, IDT, PIC y teclado", false, 0, 0);
    ax_service_register("scheduler", "planificador de hilos", false, 0, 0);
    ax_service_register("syscalls", "tabla de llamadas al sistema", false, 0, 0);
    ax_service_register("ipc", "comunicacion entre procesos", false, 0, 0);
    ax_service_register("vfs", "sistema de ficheros virtual", false, 0, 0);
    ax_service_register("modules", "carga de modulos del kernel", false, 0, 0);
    ax_service_mark_started("kernel");
    ax_service_mark_started("cpu");
    ax_service_mark_started("scheduler");
    ax_service_mark_started("syscalls");
    ax_service_mark_started("ipc");
    ax_service_mark_started("vfs");
    ax_service_mark_started("modules");

    /* Hardware y servicios persistente. */
    ax_service_register("power", "ACPI, apagado y reinicio", true, svc_power, 0);
    ax_service_register("pci", "enumeracion PCI/PCIe", true, svc_pci, 0);
    ax_service_register("disk", "disco ATA/AHCI", true, svc_disk, 0);
    ax_service_register("audio", "audio AC97/HDA", true, svc_audio, 0);
    ax_service_register("video", "video VESA/VBE", true, svc_video, 0);
    ax_service_register("users", "gestor de usuarios cifrado", true, svc_users, 0);
    ax_service_register("signals", "senales POSIX del kernel", true, svc_signals, 0);
    ax_service_register("axfs", "AxFS-D en disco", true, svc_axfs, 0);
    ax_service_register("userdb", "base /etc/users.db cifrada", true, svc_userdb, 0);
    ax_service_register("registry", "registro del sistema", true, svc_registry, 0);
    ax_service_register("install", "instalador y formateador", true, svc_install, 0);
    ax_service_register("gc", "recolector de recursos", true, svc_gc, 0);
    ax_service_register("network", "pila TCP/IP e1000", true, svc_net, 0);

    ax_service_register("shell", "shell del sistema", false, 0, 0);
    ax_service_mark_started("shell");
    ax_printf("[service] gestor con %d servicios registrados\n", g_count);
}

int ax_service_register(const char* name, const char* description,
                        bool auto_start, void (*start)(void),
                        void (*stop)(void))
{
    if (!name || !name[0] || g_count >= AX_SERVICE_MAX) {
        return -1;
    }
    if (svc_find(name) >= 0) {
        return -2;
    }
    Service* s = &g_services[g_count];
    svc_copy(s->name, sizeof(s->name), name);
    svc_copy(s->description, sizeof(s->description), description);
    s->state = AX_SERVICE_STOPPED;
    s->auto_start = auto_start;
    s->start = start;
    s->stop = stop;
    ++g_count;
    return 0;
}

void ax_service_mark_started(const char* name)
{
    int idx = svc_find(name);
    if (idx >= 0) {
        g_services[idx].state = AX_SERVICE_RUNNING;
    }
}

int ax_service_autostart(void)
{
    int total = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_services[i].auto_start) {
            ++total;
        }
    }
    int done = 0;
    for (int i = 0; i < g_count; ++i) {
        Service* s = &g_services[i];
        if (!s->auto_start || s->state == AX_SERVICE_RUNNING) {
            continue;
        }
        char text[64];
        svc_copy(text, sizeof(text), "servicio: ");
        int pos = svc_strlen(text);
        for (int k = 0; s->name[k] && pos + 1 < (int)sizeof(text); ++k) {
            text[pos++] = s->name[k];
        }
        text[pos] = '\0';
        ax_splash_progress(30 + (done * 60) / (total ? total : 1), text);
        if (s->start) {
            s->start();
        }
        s->state = AX_SERVICE_RUNNING;
        ++done;
    }
    ax_splash_progress(94, "servicios de arranque completados");
    return 0;
}

int ax_service_start(const char* name)
{
    int idx = svc_find(name);
    if (idx < 0) {
        return -1;
    }
    Service* s = &g_services[idx];
    if (s->state == AX_SERVICE_RUNNING) {
        return 0;
    }
    s->state = AX_SERVICE_RUNNING;
    if (s->start) {
        s->start();
    }
    return 0;
}

int ax_service_stop(const char* name)
{
    int idx = svc_find(name);
    if (idx < 0) {
        return -1;
    }
    Service* s = &g_services[idx];
    if (s->state != AX_SERVICE_RUNNING) {
        return 0;
    }
    if (s->stop) {
        s->stop();
    }
    s->state = AX_SERVICE_STOPPED;
    return 0;
}

int ax_service_enum(uint32_t index, AxServiceInfoK* out)
{
    if (!out || index >= (uint32_t)g_count) {
        return -1;
    }
    Service* s = &g_services[index];
    svc_copy(out->name, sizeof(out->name), s->name);
    svc_copy(out->description, sizeof(out->description), s->description);
    out->state = s->state;
    out->auto_start = s->auto_start;
    return 0;
}

int ax_service_count(void)
{
    return g_count;
}

const char* ax_service_state_name(uint32_t state)
{
    switch (state) {
        case AX_SERVICE_RUNNING: return "activo";
        case AX_SERVICE_FAILED:  return "fallo";
        default:                 return "parado";
    }
}
