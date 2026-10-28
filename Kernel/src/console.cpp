/*
 * AxolynOS - Consola de video (VGA 80x25) y salida por puerto serie COM1.
 *
 * El buffer de texto de la VGA vive en la direccion fisica 0xB8000. El kernel
 * la accede a traves del mapa de identidad bajo que instala el arranque
 * (boot.S) y luego el VMM, por lo que aqui se usa la direccion virtual 0xB8000
 * tal cual.
 *
 * Toda la salida se duplica al puerto serie (0x3F8) para poder depurar el
 * kernel por consola serie o por QEMU -serial.
 */

#include "ax_types.h"
#include "ax_console.h"
#include "ax_keyboard.h"
#include "ax_signal.h"
#include "ax_sched.h"

/* ------------------------------------------------------------------ */
/* Acceso a puertos de E/S                                              */
/* ------------------------------------------------------------------ */
static inline void ax_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t ax_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ------------------------------------------------------------------ */
/* Puerto serie COM1                                                   */
/* ------------------------------------------------------------------ */
#define AX_SERIAL_DATA    (AX_SERIAL_PORT + 0)
#define AX_SERIAL_INT_EN  (AX_SERIAL_PORT + 1)
#define AX_SERIAL_FIFO    (AX_SERIAL_PORT + 2)
#define AX_SERIAL_LCR     (AX_SERIAL_PORT + 3)
#define AX_SERIAL_MCR     (AX_SERIAL_PORT + 4)
#define AX_SERIAL_LSR     (AX_SERIAL_PORT + 5)

#define AX_SERIAL_LSR_THRE 0x20    /* registro de transmision vacio     */
#define AX_SERIAL_LSR_DRH  0x01    /* data ready (bit mas significativo) */

void ax_serial_init(void)
{
    /* Desactivamos las interrupciones del UART mientras lo configuramos. */
    ax_outb(AX_SERIAL_INT_EN, 0x00);
    /* DLAB = 1 para poder tocar el divisor de velocidad. */
    ax_outb(AX_SERIAL_LCR, 0x80);
    ax_outb(AX_SERIAL_DATA, 0x01);     /* divisor bajo  = 1 -> 115200 baudios */
    ax_outb(AX_SERIAL_INT_EN, 0x00);   /* divisor alto = 0                   */
    /* 8 bits de datos, sin paridad, 1 bit de parada (8N1). */
    ax_outb(AX_SERIAL_LCR, 0x03);
    /* Activa y vacia la FIFO de 14 bytes. */
    ax_outb(AX_SERIAL_FIFO, 0xC7);
    /* IRQ habilitadas, RTS y DSR activados. */
    ax_outb(AX_SERIAL_MCR, 0x0B);
}

void ax_serial_putc(char c)
{
    if (c == '\n') {
        ax_serial_putc('\r');
    }
    while ((ax_inb(AX_SERIAL_LSR) & AX_SERIAL_LSR_THRE) == 0) {
        /* Espera activa a que el puerto pueda aceptar un byte. */
    }
    ax_outb(AX_SERIAL_DATA, (uint8_t)c);
}

void ax_serial_puts(const char* s)
{
    if (!s) {
        return;
    }
    while (*s) {
        ax_serial_putc(*s++);
    }
}

/*
 * Lee un byte del puerto serie.  La espera es activa: si el buffer de
 * recepcion esta vacio se duerme con HLT (que consume el tick del
 * temporizador, de modo que el planificador sigue funcionando).
 */
char ax_console_getc(void)
{
    while ((ax_inb(AX_SERIAL_LSR) & AX_SERIAL_LSR_DRH) == 0) {
        __asm__ __volatile__("hlt");
    }
    return (char)ax_inb(AX_SERIAL_DATA);
}

/* 1 si hay al menos un byte disponible en el puerto serie. */
bool ax_console_getc_ready(void)
{
    return (ax_inb(AX_SERIAL_LSR) & AX_SERIAL_LSR_DRH) != 0;
}

/*
 * Entrada combinada: teclado PS/2 y puerto serie.  Las teclas extendidas del
 * teclado se convierten en secuencias ANSI en el driver (flechas, Inicio,
 * Fin, Supr, RePag, AvPag), de modo que los editores solo ven un flujo de
 * bytes y pueden compartir el mismo parser que un terminal serie.
 */
bool ax_console_trygetc(char* out)
{
    if (!out) {
        return false;
    }
    if (ax_keyboard_has_char()) {
        *out = ax_keyboard_getc();
        return true;
    }
    if (ax_console_getc_ready()) {
        *out = (char)ax_inb(AX_SERIAL_DATA);
        return true;
    }
    return false;
}

char ax_console_read(void)
{
    char c = 0;
    for (;;) {
        if (ax_console_trygetc(&c)) {
            return c;
        }
        /* Una senal (Ctrl+C del proceso en primer plano) despierta la
           lectura: se entrega como el caracter de control equivalente. */
        ax_signal_check();
        if (ax_signal_taken_mask() != 0) {
            return '\x03';
        }

        /*
         * No se puede usar HLT aqui: esta funcion se ejecuta dentro de una
         * syscall (int 0x80), que entra con las interrupciones desactivadas.
         * En su lugar se cede el CPU; el planificador y el hilo idle dejan
         * que sigan llegando el teclado y el temporizador.
         */
        ax_thread_yield();
    }
}

/* ------------------------------------------------------------------ */
/* Consola VGA 80x25                                                    */
/* ------------------------------------------------------------------ */
#define AX_VGA_CELLS (AX_VGA_WIDTH * AX_VGA_HEIGHT)

/* Atributos de color: bits 0-3 foreground, 4-6 background, 7 blink. */
#define AX_COLOR_DEFAULT 0x07

static uint16_t* ax_vga_buffer = (uint16_t*)AX_VGA_BUFFER;
static int ax_vga_row = 0;
static int ax_vga_col = 0;
static uint8_t ax_vga_attr = AX_COLOR_DEFAULT;

/* Estado del parser de secuencias de escape ANSI.  Ademas de aplicar los
 * codigos sobre la VGA, se reenvian al puerto serie para que los editores de
 * pantalla completa funcionen en terminales ANSI (QEMU -serial stdio, SSH,
 * etc.). */
enum {
    AX_ESC_NONE = 0,
    AX_ESC_SEEN,
    AX_ESC_CSI,
    AX_ESC_NUMBER
};

static int  ax_esc_state = AX_ESC_NONE;
static int  ax_esc_params[4];
static int  ax_esc_nparams = 0;
static int  ax_esc_value = 0;
static bool ax_esc_have_value = false;

static inline void ax_vga_write_cell(int row, int col, char c, uint8_t attr)
{
    ax_vga_buffer[row * AX_VGA_WIDTH + col] = (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

static void ax_vga_scroll(void)
{
    /* Desplazamos el contenido una linea hacia arriba y limpiamos la ultima. */
    for (int row = 1; row < AX_VGA_HEIGHT; ++row) {
        for (int col = 0; col < AX_VGA_WIDTH; ++col) {
            ax_vga_buffer[(row - 1) * AX_VGA_WIDTH + col] =
                ax_vga_buffer[row * AX_VGA_WIDTH + col];
        }
    }
    for (int col = 0; col < AX_VGA_WIDTH; ++col) {
        ax_vga_write_cell(AX_VGA_HEIGHT - 1, col, ' ', ax_vga_attr);
    }
    ax_vga_row = AX_VGA_HEIGHT - 1;
}

static void ax_vga_newline(void)
{
    ax_vga_col = 0;
    if (++ax_vga_row >= AX_VGA_HEIGHT) {
        ax_vga_scroll();
    }
}

/* Actualiza el cursor hardware de la VGA (CRTC, puertos 0x3D4/0x3D5). */
static void ax_vga_update_cursor(void)
{
    uint16_t offset = (uint16_t)(ax_vga_row * AX_VGA_WIDTH + ax_vga_col);
    ax_outb(0x3D4, 0x0F);
    ax_outb(0x3D5, (uint8_t)(offset & 0xFF));
    ax_outb(0x3D4, 0x0E);
    ax_outb(0x3D5, (uint8_t)((offset >> 8) & 0xFF));
}

void ax_console_init(void)
{
    ax_vga_row = 0;
    ax_vga_col = 0;
    ax_vga_attr = AX_COLOR_DEFAULT;
    ax_console_clear();
}

static void ax_console_clear_screen(void);

void ax_console_clear(void)
{
    ax_console_clear_screen();
    ax_vga_attr = AX_COLOR_DEFAULT;
    ax_vga_update_cursor();
}

static void ax_console_clear_to_eol(void)
{
    for (int col = ax_vga_col; col < AX_VGA_WIDTH; ++col) {
        ax_vga_write_cell(ax_vga_row, col, ' ', ax_vga_attr);
    }
}

static void ax_console_clear_screen(void)
{
    for (int row = 0; row < AX_VGA_HEIGHT; ++row) {
        for (int col = 0; col < AX_VGA_WIDTH; ++col) {
            ax_vga_write_cell(row, col, ' ', ax_vga_attr);
        }
    }
    ax_vga_row = 0;
    ax_vga_col = 0;
}

void ax_console_set_cursor(int row, int col)
{
    if (row < 0) row = 0;
    if (row >= AX_VGA_HEIGHT) row = AX_VGA_HEIGHT - 1;
    if (col < 0) col = 0;
    if (col >= AX_VGA_WIDTH) col = AX_VGA_WIDTH - 1;
    ax_vga_row = row;
    ax_vga_col = col;
    ax_vga_update_cursor();
}

/*
 * Parser minimo de secuencias de escape ANSI.  Se soportan los codigos de
 * uso habitual: SGR (colores, intenso y video inverso), posicionamiento de
 * cursor (H/f), movimiento (A/B/C/D), borrado (J/K) y visibilidad (h/l).
 */
static void ax_console_apply_sgr(int code)
{
    switch (code) {
        case 0: ax_vga_attr = AX_COLOR_DEFAULT; break;
        case 1: ax_vga_attr |= 0x08; break;
        case 7: {
            /* Video inverso: intercambia primer y segundo plano. */
            uint8_t fg = (uint8_t)(ax_vga_attr & 0x0F);
            uint8_t bg = (uint8_t)((ax_vga_attr >> 4) & 0x07);
            ax_vga_attr = (uint8_t)((fg << 4) | (bg & 0x0F));
            break;
        }
        case 22: ax_vga_attr &= (uint8_t)~0x08; break;
        case 27: ax_vga_attr = AX_COLOR_DEFAULT; break;
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            ax_vga_attr = (uint8_t)((ax_vga_attr & 0xF0) | (code - 30));
            break;
        case 39:
            ax_vga_attr = (uint8_t)((ax_vga_attr & 0xF0) | (AX_COLOR_DEFAULT & 0x0F));
            break;
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            ax_vga_attr = (uint8_t)((ax_vga_attr & 0x8F) | ((code - 40) << 4));
            break;
        case 49:
            ax_vga_attr = (uint8_t)((ax_vga_attr & 0x8F) | ((AX_COLOR_DEFAULT >> 4) << 4));
            break;
        default: break;
    }
}

static int ax_esc_param(int index, int fallback)
{
    if (index < 0 || index >= ax_esc_nparams) {
        return fallback;
    }
    return ax_esc_params[index] ? ax_esc_params[index] : fallback;
}

static void ax_console_apply_csi(char final)
{
    switch (final) {
        case 'm':
            if (ax_esc_nparams == 0) {
                ax_console_apply_sgr(0);
            } else {
                for (int i = 0; i < ax_esc_nparams; ++i) {
                    ax_console_apply_sgr(ax_esc_params[i]);
                }
            }
            break;
        case 'H':
        case 'f': {
            int row = ax_esc_param(0, 1) - 1;
            int col = ax_esc_param(1, 1) - 1;
            ax_console_set_cursor(row, col);
            break;
        }
        case 'A':
            ax_console_set_cursor(ax_vga_row - ax_esc_param(0, 1), ax_vga_col);
            break;
        case 'B':
            ax_console_set_cursor(ax_vga_row + ax_esc_param(0, 1), ax_vga_col);
            break;
        case 'C':
            ax_console_set_cursor(ax_vga_row, ax_vga_col + ax_esc_param(0, 1));
            break;
        case 'D':
            ax_console_set_cursor(ax_vga_row, ax_vga_col - ax_esc_param(0, 1));
            break;
        case 'J': {
            int mode = ax_esc_param(0, 0);
            if (mode == 2) {
                ax_console_clear_screen();
                ax_vga_update_cursor();
            } else if (mode == 0) {
                ax_console_clear_to_eol();
                for (int row = ax_vga_row + 1; row < AX_VGA_HEIGHT; ++row) {
                    for (int col = 0; col < AX_VGA_WIDTH; ++col) {
                        ax_vga_write_cell(row, col, ' ', ax_vga_attr);
                    }
                }
            } else if (mode == 1) {
                for (int row = 0; row < ax_vga_row; ++row) {
                    for (int col = 0; col < AX_VGA_WIDTH; ++col) {
                        ax_vga_write_cell(row, col, ' ', ax_vga_attr);
                    }
                }
                for (int col = 0; col <= ax_vga_col && col < AX_VGA_WIDTH; ++col) {
                    ax_vga_write_cell(ax_vga_row, col, ' ', ax_vga_attr);
                }
            }
            break;
        }
        case 'K': {
            int mode = ax_esc_param(0, 0);
            if (mode == 2) {
                for (int col = 0; col < AX_VGA_WIDTH; ++col) {
                    ax_vga_write_cell(ax_vga_row, col, ' ', ax_vga_attr);
                }
            } else {
                ax_console_clear_to_eol();
            }
            break;
        }
        default:
            break;
    }
}

static void ax_esc_push_value(void)
{
    if (ax_esc_nparams < 4) {
        ax_esc_params[ax_esc_nparams++] = ax_esc_value;
    }
    ax_esc_value = 0;
    ax_esc_have_value = false;
}

/*
 * Consume una secuencia de escape.  Devuelve true si el caracter recibido
 * forma parte de la secuencia y por tanto no debe imprimirse.
 */
static bool ax_console_escape(char c)
{
    /* Todo byte de una secuencia ANSI se reenvia al puerto serie. */
    if (ax_esc_state != AX_ESC_NONE) {
        ax_serial_putc(c);
    }

    switch (ax_esc_state) {
        case AX_ESC_NONE:
            if (c == 0x1B) {
                ax_serial_putc(c);
                ax_esc_state = AX_ESC_SEEN;
                ax_esc_nparams = 0;
                ax_esc_value = 0;
                ax_esc_have_value = false;
                return true;
            }
            return false;

        case AX_ESC_SEEN:
            if (c == '[') {
                ax_esc_state = AX_ESC_CSI;
                return true;
            }
            ax_esc_state = AX_ESC_NONE;
            return true;

        case AX_ESC_CSI:
            /* Prefijos privados de terminal (ESC[?25l, ESC[>0c...). Se
               reenvian al serie pero no se interpretan en la VGA. */
            if (c == '?' || c == '>' || c == '=' || c == '!') {
                return true;
            }
            if (c >= '0' && c <= '9') {
                ax_esc_state = AX_ESC_NUMBER;
                ax_esc_value = c - '0';
                ax_esc_have_value = true;
                return true;
            }
            if (c == ';') {
                ax_esc_push_value();
                return true;
            }
            ax_console_apply_csi(c);
            ax_esc_state = AX_ESC_NONE;
            return true;

        case AX_ESC_NUMBER:
            if (c >= '0' && c <= '9') {
                ax_esc_value = ax_esc_value * 10 + (c - '0');
                ax_esc_have_value = true;
                return true;
            }
            if (c == ';') {
                ax_esc_push_value();
                ax_esc_state = AX_ESC_CSI;
                return true;
            }
            ax_esc_push_value();
            ax_console_apply_csi(c);
            ax_esc_state = AX_ESC_NONE;
            return true;

        default:
            ax_esc_state = AX_ESC_NONE;
            return true;
    }
}

void ax_putc(char c)
{
    if (ax_console_escape(c)) {
        return;
    }
    if (c == '\n') {
        ax_vga_newline();
        ax_vga_update_cursor();
        ax_serial_putc('\n');
        return;
    }
    if (c == '\r') {
        ax_vga_col = 0;
        ax_vga_update_cursor();
        ax_serial_putc('\r');
        return;
    }
    if (c == '\b') {
        if (ax_vga_col > 0) {
            ax_vga_col--;
        } else if (ax_vga_row > 0) {
            ax_vga_row--;
            ax_vga_col = AX_VGA_WIDTH - 1;
        }
        ax_vga_update_cursor();
        ax_serial_putc('\b');
        return;
    }
    if (c == '\t') {
        for (int i = 0; i < 4; ++i) {
            ax_putc(' ');
        }
        return;
    }
    ax_vga_write_cell(ax_vga_row, ax_vga_col, c, ax_vga_attr);
    if (++ax_vga_col >= AX_VGA_WIDTH) {
        ax_vga_newline();
    }
    ax_vga_update_cursor();
    ax_serial_putc(c);
}

void ax_puts(const char* s)
{
    if (!s) {
        return;
    }
    while (*s) {
        ax_putc(*s++);
    }
}

void ax_puthex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[16];
    int i = 16;
    if (value == 0) {
        ax_putc('0');
        return;
    }
    while (value && i > 0) {
        buffer[--i] = digits[value & 0xF];
        value >>= 4;
    }
    while (i < 16) {
        ax_putc(buffer[i++]);
    }
}

void ax_putdec(int64_t value)
{
    char buffer[24];
    int i = 24;
    uint64_t magnitude;

    if (value < 0) {
        ax_putc('-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;   /* evita el desbordamiento de INT64_MIN */
    } else {
        magnitude = (uint64_t)value;
    }
    if (magnitude == 0) {
        ax_putc('0');
        return;
    }
    while (magnitude && i > 0) {
        buffer[--i] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    while (i < 24) {
        ax_putc(buffer[i++]);
    }
}

/* ------------------------------------------------------------------ */
/* Impresion con formato                                               */
/* ------------------------------------------------------------------ */
static void ax_print_u64(uint64_t value, unsigned base, bool upper)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char* digits = upper ? upper_digits : lower_digits;
    char buffer[24];
    int i = 24;

    if (value == 0) {
        ax_putc('0');
        return;
    }
    while (value && i > 0) {
        buffer[--i] = digits[value % base];
        value /= base;
    }
    while (i < 24) {
        ax_putc(buffer[i++]);
    }
}

static void ax_print_i64(int64_t value)
{
    if (value < 0) {
        ax_putc('-');
        ax_print_u64((uint64_t)(-(value + 1)) + 1u, 10, false);
    } else {
        ax_print_u64((uint64_t)value, 10, false);
    }
}

void ax_vprintf(const char* fmt, va_list args)
{
    if (!fmt) {
        return;
    }

    while (*fmt) {
        char c = *fmt++;

        if (c != '%') {
            ax_putc(c);
            continue;
        }

        /* Modificadores de longitud: 'l' (long) y "ll" (long long). */
        int long_count = 0;
        while (*fmt == 'l') {
            ++long_count;
            ++fmt;
        }
        /* Ignoramos 'z' y 'j': en el kernel todos los enteros son de 64 bits. */
        if (*fmt == 'z' || *fmt == 'j') {
            ++fmt;
            long_count = 2;
        }

        switch (*fmt) {
            case 's': {
                const char* s = va_arg(args, const char*);
                ax_puts(s ? s : "(null)");
                ++fmt;
                break;
            }
            case 'c': {
                int ch = va_arg(args, int);
                ax_putc((char)ch);
                ++fmt;
                break;
            }
            case 'd':
            case 'i': {
                if (long_count >= 2) {
                    ax_print_i64(va_arg(args, long long));
                } else if (long_count == 1) {
                    ax_print_i64(va_arg(args, long));
                } else {
                    ax_print_i64((int64_t)va_arg(args, int));
                }
                ++fmt;
                break;
            }
            case 'u': {
                if (long_count >= 2) {
                    ax_print_u64(va_arg(args, unsigned long long), 10, false);
                } else if (long_count == 1) {
                    ax_print_u64(va_arg(args, unsigned long), 10, false);
                } else {
                    ax_print_u64(va_arg(args, unsigned int), 10, false);
                }
                ++fmt;
                break;
            }
            case 'x':
            case 'X': {
                bool upper = (*fmt == 'X');
                if (long_count >= 2) {
                    ax_print_u64(va_arg(args, unsigned long long), 16, upper);
                } else if (long_count == 1) {
                    ax_print_u64(va_arg(args, unsigned long), 16, upper);
                } else {
                    ax_print_u64(va_arg(args, unsigned int), 16, upper);
                }
                ++fmt;
                break;
            }
            case 'p': {
                ax_putc('0');
                ax_putc('x');
                ax_print_u64((uint64_t)(uintptr_t)va_arg(args, void*), 16, false);
                ++fmt;
                break;
            }
            case '%': {
                ax_putc('%');
                ++fmt;
                break;
            }
            case '\0': {
                return;
            }
            default: {
                /* Especificador desconocido: se imprime tal cual. */
                ax_putc('%');
                ax_putc(*fmt);
                ++fmt;
                break;
            }
        }
    }
}

void ax_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ax_vprintf(fmt, args);
    va_end(args);
}
