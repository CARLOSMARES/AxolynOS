/*
 * AxolynOS - Splash de arranque (VGA 80x25, sin framebuffer).
 */
#include "ax_splash.h"
#include "ax_console.h"

#define SPLASH_W 80
#define SPLASH_H 25
#define SPLASH_BG 0x1Fu       /* blanco sobre azul */
#define SPLASH_ACCENT 0x1Eu   /* amarillo sobre azul */
#define SPLASH_DIM 0x17u      /* gris claro sobre azul */
#define SPLASH_BAR 0x2Fu      /* blanco sobre verde */
#define SPLASH_BAR_BG 0x1Au   /* verde sobre azul */

static bool g_active = false;
static int  g_percent = 0;
static char g_text[64];

static void splash_copy(char* dst, uint64_t max, const char* src)
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

static void splash_center(int row, const char* text, uint8_t attr)
{
    int len = 0;
    while (text[len]) {
        ++len;
    }
    int col = (SPLASH_W - len) / 2;
    if (col < 0) {
        col = 0;
    }
    ax_console_raw_text(row, col, text, attr);
}

static void splash_draw_bar(void)
{
    int row = 10;
    int left = 16;
    int width = 48;
    int filled = (width * g_percent) / 100;
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    ax_console_raw_put(row, left - 1, '[', SPLASH_BG);
    ax_console_raw_put(row, left + width, ']', SPLASH_BG);
    for (int i = 0; i < width; ++i) {
        ax_console_raw_put(row, left + i, i < filled ? '#' : '-',
                           i < filled ? SPLASH_BAR : SPLASH_BAR_BG);
    }
}

static void splash_draw_percent(void)
{
    char buf[8];
    int p = g_percent;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    int pos = 0;
    if (p == 100) {
        buf[pos++] = '1';
        buf[pos++] = '0';
        buf[pos++] = '0';
    } else if (p >= 10) {
        buf[pos++] = (char)('0' + p / 10);
        buf[pos++] = (char)('0' + p % 10);
    } else {
        buf[pos++] = (char)('0' + p);
    }
    buf[pos++] = '%';
    buf[pos] = '\0';
    ax_console_raw_text(12, 37, "    ", SPLASH_BG);
    ax_console_raw_text(12, 38, buf, SPLASH_ACCENT);
}

static void splash_draw_text(void)
{
    /* Borra la linea de estado sin tocar el resto. */
    for (int col = 0; col < SPLASH_W; ++col) {
        ax_console_raw_put(14, col, ' ', SPLASH_BG);
    }
    int len = 0;
    while (g_text[len]) {
        ++len;
    }
    int col = (SPLASH_W - len) / 2;
    if (col < 0) col = 0;
    ax_console_raw_text(14, col, g_text, SPLASH_DIM);
}

void ax_splash_init(void)
{
    g_active = true;
    g_percent = 0;
    splash_copy(g_text, sizeof(g_text), "iniciando servicios del sistema...");

    ax_console_set_vga_muted(true);
    ax_console_raw_clear(SPLASH_BG);

    /* Marca grafica central. */
    splash_center(3, "A X O L Y N   O S", SPLASH_ACCENT);
    splash_center(4, "sistema operativo x86-64", SPLASH_DIM);
    splash_center(6, "================================================", SPLASH_DIM);
    splash_center(7, "  arranque seguro  |  kernel Axolyn 0.2  ", SPLASH_BG);
    splash_center(8, "================================================", SPLASH_DIM);

    ax_console_raw_text(11, 16, "Cargando servicios de inicio", SPLASH_ACCENT);
    splash_draw_bar();
    splash_draw_percent();
    splash_draw_text();

    splash_center(18, "usuarios cifrados: PBKDF2-HMAC-SHA256 + ChaCha20",
                  SPLASH_DIM);
    splash_center(19, "registro del sistema: AxRegistry", SPLASH_DIM);
    splash_center(20, "shell, servicios, red y AxFS-D", SPLASH_DIM);
    splash_center(23, "AxolynOS 0.2  -  pulsa una tecla al terminar", SPLASH_DIM);

    ax_console_raw_cursor(SPLASH_H - 1, 0);
}

void ax_splash_progress(int percent, const char* text)
{
    if (!g_active) {
        return;
    }
    if (percent < g_percent) {
        percent = g_percent;
    }
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_percent = percent;
    if (text) {
        splash_copy(g_text, sizeof(g_text), text);
    }
    splash_draw_bar();
    splash_draw_percent();
    splash_draw_text();
}

void ax_splash_end(void)
{
    if (!g_active) {
        return;
    }
    ax_splash_progress(100, "sistema listo");
    g_active = false;
}

bool ax_splash_active(void)
{
    return g_active;
}

void ax_splash_dismiss(void)
{
    g_active = false;
    ax_console_force_vga();
    ax_console_clear();
}

void ax_splash_abort(void)
{
    g_active = false;
    ax_console_force_vga();
}
