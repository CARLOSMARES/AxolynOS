/*
 * AxolynOS - Controlador de teclado PS/2.
 *
 * Detalles de bajo nivel:
 *   - Puerto 0x60: datos (lectura = scancode, escritura = parametro).
 *   - Puerto 0x64: estado/comando.
 *       bit 0 = output buffer full  (hay dato en 0x60)
 *       bit 1 = input buffer full   (no se puede escribir todavia)
 *       bit 2 = system flag
 *
 * Inicializacion:
 *   1. Se vacia el buffer de salida.
 *   2. Se activa el primer puerto PS/2 (comando 0xAE).
 *   3. Se lee la configuracion del controlador (0x20) y se activa el bit 0
 *      (IRQ1) y se desactiva el bit 4 (reloj del teclado).
 *
 * La traduccion usa el conjunto de scancodes 1 (make code = tecla pulsada,
 * make code | 0x80 = tecla liberada).  0xE0 marca las teclas extendidas
 * (flechas, Supr, etc.); de momento se ignoran salvo Inicio/Fin.
 */

#include "ax_types.h"
#include "ax_keyboard.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Puertos del controlador 8042                                        */
/* ------------------------------------------------------------------ */
#define AX_KBD_DATA    0x60
#define AX_KBD_STATUS  0x64
#define AX_KBD_CMD     0x64

#define AX_KBD_STAT_OUT_FULL 0x01
#define AX_KBD_STAT_IN_FULL  0x02

#define AX_KBD_CMD_READ_CFG  0x20
#define AX_KBD_CMD_WRITE_CFG 0x60
#define AX_KBD_CMD_ENABLE    0xAE

/* ------------------------------------------------------------------ */
/* Estado interno                                                      */
/* ------------------------------------------------------------------ */
#define AX_KBD_BUFFER_SIZE 256

static char     g_buffer[AX_KBD_BUFFER_SIZE];
static uint32_t g_head = 0;     /* indice de lectura  */
static uint32_t g_tail = 0;     /* indice de escritura */
static uint32_t g_count = 0;

static bool g_shift = false;
static bool g_ctrl  = false;
static bool g_alt   = false;
static bool g_caps  = false;
static bool g_extended = false;

static inline uint8_t ax_kbd_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ax_kbd_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Espera a que el controlador pueda aceptar un comando/parametro. */
static bool ax_kbd_wait_input(void)
{
    for (int i = 0; i < 100000; ++i) {
        if ((ax_kbd_inb(AX_KBD_STATUS) & AX_KBD_STAT_IN_FULL) == 0) {
            return true;
        }
    }
    return false;
}

/* Espera a que haya un dato disponible en el puerto 0x60. */
static bool ax_kbd_wait_output(void)
{
    for (int i = 0; i < 100000; ++i) {
        if (ax_kbd_inb(AX_KBD_STATUS) & AX_KBD_STAT_OUT_FULL) {
            return true;
        }
    }
    return false;
}

static void ax_kbd_flush(void)
{
    for (int i = 0; i < 32; ++i) {
        if ((ax_kbd_inb(AX_KBD_STATUS) & AX_KBD_STAT_OUT_FULL) == 0) {
            break;
        }
        (void)ax_kbd_inb(AX_KBD_DATA);
    }
}

/* ------------------------------------------------------------------ */
/* Buffer circular                                                     */
/* ------------------------------------------------------------------ */
static void ax_kbd_push(char c)
{
    if (g_count >= AX_KBD_BUFFER_SIZE) {
        return;                     /* se descarta si el buffer esta lleno */
    }
    g_buffer[g_tail] = c;
    g_tail = (g_tail + 1) % AX_KBD_BUFFER_SIZE;
    g_count++;
}

bool ax_keyboard_has_char(void)
{
    return g_count > 0;
}

uint32_t ax_keyboard_pending(void)
{
    return g_count;
}

char ax_keyboard_getc(void)
{
    if (g_count == 0) {
        return 0;
    }
    char c = g_buffer[g_head];
    g_head = (g_head + 1) % AX_KBD_BUFFER_SIZE;
    g_count--;
    return c;
}

/* ------------------------------------------------------------------ */
/* Tablas de traduccion (scancode set 1, distribucion US)              */
/* ------------------------------------------------------------------ */
static const char ax_kbd_map_normal[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ', 0
};

static const char ax_kbd_map_shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ', 0
};

/* Caracter ASCII a partir del scancode (sin modificadores). */
static char ax_kbd_translate(uint8_t scancode)
{
    if (scancode >= 128) {
        return 0;
    }
    char c = g_shift ? ax_kbd_map_shift[scancode] : ax_kbd_map_normal[scancode];

    /* Caps Lock solo afecta a las letras. */
    if (g_caps) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }

    /* Ctrl + letra -> caracter de control (Ctrl+C = 3). */
    if (g_ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        char base = (c >= 'a' && c <= 'z') ? c : (char)(c - 'A' + 'a');
        c = (char)(base - 'a' + 1);
    }

    return c;
}

/* ------------------------------------------------------------------ */
/* IRQ1                                                                */
/* ------------------------------------------------------------------ */
void ax_keyboard_irq(void)
{
    /*
     * Se leen todos los bytes pendientes: si el controlador ha acumulado
     * varios scancodes (teclas rapidas) hay que vaciarlo entero o la IRQ
     * se quedaria reclamando.
     */
    while (ax_kbd_inb(AX_KBD_STATUS) & AX_KBD_STAT_OUT_FULL) {
        uint8_t scancode = ax_kbd_inb(AX_KBD_DATA);

        /* Prefijo de tecla extendida (flechas, Supr, etc.). */
        if (scancode == 0xE0) {
            g_extended = true;
            continue;
        }

        bool released = (scancode & 0x80) != 0;
        uint8_t code  = (uint8_t)(scancode & 0x7F);

        if (g_extended) {
            g_extended = false;
            /* Teclas extendidas: se traducen a las mismas secuencias ANSI que
               envia un terminal, para que la shell y los editores no tengan
               que distinguir el origen. */
            const char* seq = nullptr;
            switch (code) {
                case 0x48: seq = "\x1b[A"; break;   /* flecha arriba */
                case 0x50: seq = "\x1b[B"; break;   /* flecha abajo  */
                case 0x4D: seq = "\x1b[C"; break;   /* flecha derecha*/
                case 0x4B: seq = "\x1b[D"; break;   /* flecha izquierda */
                case 0x47: seq = "\x1b[H"; break;   /* Inicio        */
                case 0x4F: seq = "\x1b[F"; break;   /* Fin           */
                case 0x52: seq = "\x1b[2~"; break;  /* Insert        */
                case 0x53: seq = "\x1b[3~"; break;  /* Supr          */
                case 0x49: seq = "\x1b[5~"; break;  /* RePag         */
                case 0x51: seq = "\x1b[6~"; break;  /* AvPag         */
                default: break;
            }
            if (seq && !released) {
                for (int i = 0; seq[i]; ++i) {
                    ax_kbd_push(seq[i]);
                }
            } else if (code == 0x1D) {
                g_ctrl = !released;
            } else if (code == 0x38) {
                g_alt = !released;
            }
            continue;
        }

        switch (code) {
            case 0x2A:  /* Shift izquierdo */
            case 0x36:  /* Shift derecho */
                g_shift = !released;
                continue;
            case 0x1D:  /* Ctrl */
                g_ctrl = !released;
                continue;
            case 0x38:  /* Alt */
                g_alt = !released;
                continue;
            case 0x3A:  /* Caps Lock: solo al pulsar */
                if (!released) {
                    g_caps = !g_caps;
                }
                continue;
            default:
                break;
        }

        if (released) {
            continue;
        }

        char c = ax_kbd_translate(code);
        if (c) {
            ax_kbd_push(c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Inicializacion                                                      */
/* ------------------------------------------------------------------ */
void ax_keyboard_init(void)
{
    g_head = g_tail = g_count = 0;
    g_shift = g_ctrl = g_alt = g_caps = g_extended = false;

    ax_kbd_flush();

    /* Activar el primer puerto PS/2 (teclado). */
    if (ax_kbd_wait_input()) {
        ax_kbd_outb(AX_KBD_CMD, AX_KBD_CMD_ENABLE);
    }

    /* Activar la IRQ1 y el reloj del teclado en la configuracion del 8042. */
    if (ax_kbd_wait_input()) {
        ax_kbd_outb(AX_KBD_CMD, AX_KBD_CMD_READ_CFG);
    }
    if (ax_kbd_wait_output()) {
        uint8_t cfg = ax_kbd_inb(AX_KBD_DATA);
        cfg |= 0x01;        /* IRQ1 habilitada              */
        cfg &= (uint8_t)~0x10;  /* reloj del teclado activo  */
        if (ax_kbd_wait_input()) {
            ax_kbd_outb(AX_KBD_CMD, AX_KBD_CMD_WRITE_CFG);
        }
        if (ax_kbd_wait_input()) {
            ax_kbd_outb(AX_KBD_DATA, cfg);
        }
    }

    ax_kbd_flush();
}
