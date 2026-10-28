/*
 * AxolynOS - Editores de texto (edit, vi, vim, nvim).
 *
 * Los cuatro comparten el mismo buffer y el mismo renderizador ANSI/VGA.
 * El buffer es de 64 KiB; el texto se carga y guarda en AxFS-D.
 *
 * Atajos:
 *   edit : flechas, Inicio/Fin, RePag/AvPag, Ctrl+S guardar, Ctrl+Q salir,
 *          Ctrl+Z deshacer.
 *   vi   : i/a/o insertar, h/j/k/l mover, x borrar, dd borrar linea,
 *          yy copiar linea, p pegar, u deshacer, / buscar, : comandos.
 *   vim  : ademas gg, G, :set nu y :set nonu.
 *   nvim : igual que vim, con estado propio.
 */
#include "ax_editor.h"
#include "ax_fs_disk.h"
#include "ax_console.h"
#include "ax_sched.h"
#include "axos/abi.h"

#define AX_ED_MAX        65536
#define AX_ED_ROWS       22
#define AX_ED_STATUS_ROW 23
#define AX_ED_MSG_ROW    24
#define AX_ED_COLS       80

/* Teclas especiales devueltas por ed_read_key(). */
enum {
    EDK_UP = 0x100,
    EDK_DOWN,
    EDK_LEFT,
    EDK_RIGHT,
    EDK_HOME,
    EDK_END,
    EDK_DELETE,
    EDK_INSERT,
    EDK_PGUP,
    EDK_PGDN
};

enum {
    VI_NORMAL = 0,
    VI_INSERT = 1,
    VI_COMMAND = 2
};

static char     g_text[AX_ED_MAX];
static char     g_undo[AX_ED_MAX];
static char     g_yank[512];
static uint64_t g_len;
static uint64_t g_cursor;
static uint64_t g_undo_len;
static uint64_t g_undo_cursor;
static bool     g_undo_valid;
static bool     g_modified;
static bool     g_show_numbers;
static int      g_scroll;
static char     g_path[160];
static char     g_name[40];
static char     g_msg[96];
static int      g_kind;
static int      g_yank_len;
static bool     g_yank_line;
/* Byte que hay que devolver en la siguiente lectura (por ejemplo el ':'
   que sigue a un ESC aislado en vi).  -1 significa vacio. */
static int      g_pushback = -1;

/* ------------------------------------------------------------------ */
/* Utilidades de texto                                                 */
/* ------------------------------------------------------------------ */
static uint64_t ed_strlen(const char* s)
{
    uint64_t n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static bool ed_starts(const char* s, const char* p)
{
    while (*p) {
        if (*s++ != *p++) {
            return false;
        }
    }
    return true;
}

static const char* ed_basename(const char* path)
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

static uint64_t ed_min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

static bool ed_is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 128;
}

/* ------------------------------------------------------------------ */
/* Buffer                                                              */
/* ------------------------------------------------------------------ */
static void ed_set_msg(const char* text)
{
    uint64_t i = 0;
    if (!text) {
        g_msg[0] = '\0';
        return;
    }
    while (text[i] && i + 1 < sizeof(g_msg)) {
        g_msg[i] = text[i];
        i++;
    }
    g_msg[i] = '\0';
}

static uint64_t ed_line_start(uint64_t pos)
{
    if (pos > g_len) {
        pos = g_len;
    }
    while (pos > 0 && g_text[pos - 1] != '\n') {
        pos--;
    }
    return pos;
}

static uint64_t ed_line_end(uint64_t pos)
{
    if (pos > g_len) {
        pos = g_len;
    }
    while (pos < g_len && g_text[pos] != '\n') {
        pos++;
    }
    return pos;
}

static uint64_t ed_line_number(uint64_t pos)
{
    uint64_t line = 1;
    for (uint64_t i = 0; i < pos && i < g_len; ++i) {
        if (g_text[i] == '\n') {
            line++;
        }
    }
    return line;
}

static uint64_t ed_column(uint64_t pos)
{
    return pos - ed_line_start(pos);
}

static void ed_undo_snapshot(void)
{
    for (uint64_t i = 0; i < g_len; ++i) {
        g_undo[i] = g_text[i];
    }
    g_undo_len = g_len;
    g_undo_cursor = g_cursor;
    g_undo_valid = true;
}

static void ed_undo_restore(void)
{
    if (!g_undo_valid) {
        ed_set_msg("nada que deshacer");
        return;
    }
    for (uint64_t i = 0; i < g_undo_len; ++i) {
        g_text[i] = g_undo[i];
    }
    g_len = g_undo_len;
    g_cursor = g_undo_cursor;
    if (g_cursor > g_len) {
        g_cursor = g_len;
    }
    g_undo_valid = false;
    g_modified = true;
    ed_set_msg("deshecho");
}

static void ed_shift_right(uint64_t pos, uint64_t count)
{
    if (count == 0 || pos >= g_len) {
        return;
    }
    for (uint64_t i = g_len; i > pos; --i) {
        g_text[i + count - 1] = g_text[i - 1];
    }
}

static void ed_shift_left(uint64_t pos, uint64_t count)
{
    if (count == 0) {
        return;
    }
    for (uint64_t i = pos; i + count <= g_len; ++i) {
        g_text[i] = g_text[i + count];
    }
}

static bool ed_insert_char(char c)
{
    if (g_len + 1 >= AX_ED_MAX) {
        ed_set_msg("buffer lleno (64 KiB)");
        return false;
    }
    ed_shift_right(g_cursor, 1);
    g_text[g_cursor++] = c;
    g_len++;
    g_modified = true;
    return true;
}

static bool ed_insert_text(const char* text, uint64_t n)
{
    if (n == 0) {
        return true;
    }
    if (g_len + n >= AX_ED_MAX) {
        ed_set_msg("buffer lleno (64 KiB)");
        return false;
    }
    ed_shift_right(g_cursor, n);
    for (uint64_t i = 0; i < n; ++i) {
        g_text[g_cursor + i] = text[i];
    }
    g_cursor += n;
    g_len += n;
    g_modified = true;
    return true;
}

static void ed_backspace(void)
{
    if (g_cursor == 0) {
        return;
    }
    ed_shift_left(g_cursor - 1, 1);
    g_cursor--;
    g_len--;
    g_modified = true;
}

static void ed_delete_char(void)
{
    if (g_cursor >= g_len) {
        return;
    }
    ed_shift_left(g_cursor, 1);
    g_len--;
    g_modified = true;
}

static void ed_delete_line(void)
{
    uint64_t start = ed_line_start(g_cursor);
    uint64_t end = ed_line_end(g_cursor);
    if (end < g_len) {
        end++;              /* incluye el salto de linea */
    } else if (start > 0) {
        start--;            /* ultima linea: se lleva el salto anterior */
    }
    ed_shift_left(start, end - start);
    g_len -= end - start;
    if (g_cursor > g_len) {
        g_cursor = g_len;
    }
    g_cursor = ed_line_start(g_cursor);
    g_modified = true;
}

static void ed_yank_line(void)
{
    uint64_t start = ed_line_start(g_cursor);
    uint64_t end = ed_line_end(g_cursor);
    uint64_t n = end - start;
    if (n >= sizeof(g_yank)) {
        n = sizeof(g_yank) - 1;
    }
    for (uint64_t i = 0; i < n; ++i) {
        g_yank[i] = g_text[start + i];
    }
    g_yank[n] = '\0';
    g_yank_len = (int)n;
    g_yank_line = true;
    ed_set_msg("linea copiada");
}

static void ed_paste(void)
{
    if (g_yank_len <= 0) {
        ed_set_msg("portapapeles vacio");
        return;
    }
    ed_undo_snapshot();
    uint64_t pos = g_cursor;
    if (g_yank_line) {
        pos = ed_line_end(g_cursor);
        if (pos < g_len) {
            pos++;          /* pega debajo de la linea actual */
        }
        g_cursor = pos;
        ed_insert_text(g_yank, (uint64_t)g_yank_len);
        ed_insert_char('\n');
        g_cursor = pos;
    } else {
        ed_insert_text(g_yank, (uint64_t)g_yank_len);
    }
}

static void ed_move_vertical(int direction)
{
    uint64_t start = ed_line_start(g_cursor);
    uint64_t col = g_cursor - start;
    if (direction < 0) {
        if (start == 0) {
            return;
        }
        uint64_t prev_end = start - 1;
        uint64_t prev_start = ed_line_start(prev_end);
        g_cursor = prev_start + ed_min(col, prev_end - prev_start);
    } else {
        uint64_t end = ed_line_end(g_cursor);
        if (end >= g_len) {
            return;
        }
        uint64_t next_start = end + 1;
        uint64_t next_end = ed_line_end(next_start);
        g_cursor = next_start + ed_min(col, next_end - next_start);
    }
}

static void ed_move_page(int direction)
{
    if (direction < 0) {
        for (int i = 0; i < AX_ED_ROWS; ++i) {
            ed_move_vertical(-1);
        }
    } else {
        for (int i = 0; i < AX_ED_ROWS; ++i) {
            ed_move_vertical(1);
        }
    }
}

static void ed_move_word(int direction)
{
    if (direction > 0) {
        while (g_cursor < g_len && !ed_is_word(g_text[g_cursor])) {
            g_cursor++;
        }
        while (g_cursor < g_len && ed_is_word(g_text[g_cursor])) {
            g_cursor++;
        }
    } else {
        while (g_cursor > 0 && !ed_is_word(g_text[g_cursor - 1])) {
            g_cursor--;
        }
        while (g_cursor > 0 && ed_is_word(g_text[g_cursor - 1])) {
            g_cursor--;
        }
    }
}

static void ed_begin_insert(void)
{
    ed_undo_snapshot();
    ed_set_msg("");
}

/* ------------------------------------------------------------------ */
/* Entrada de teclado                                                  */
/* ------------------------------------------------------------------ */
static bool ed_try_char(char* out, uint32_t timeout_ms)
{
    if (ax_console_trygetc(out)) {
        return true;
    }
    uint64_t start = ax_uptime_ms();
    while (ax_uptime_ms() - start < timeout_ms) {
        ax_thread_sleep(4);
        if (ax_console_trygetc(out)) {
            return true;
        }
    }
    return ax_console_trygetc(out);
}

static int ed_read_key(void)
{
    char c = 0;
    if (g_pushback >= 0) {
        c = (char)g_pushback;
        g_pushback = -1;
    } else {
        while (!ax_console_trygetc(&c)) {
            ax_thread_sleep(8);
        }
    }

    if ((unsigned char)c != 0x1B) {
        if (c == '\r') {
            return '\n';
        }
        if ((unsigned char)c == 127) {
            return '\b';
        }
        return (int)(unsigned char)c;
    }

    /* ESC aislado o inicio de secuencia CSI/SS3. */
    char next = 0;
    if (!ed_try_char(&next, 35)) {
        return 27;
    }
    if (next != '[' && next != 'O') {
        /* No era una secuencia: devolvemos el byte leido para no perderlo.
           Es importante en vi, donde se escribe ESC y ':' casi seguidos. */
        g_pushback = (unsigned char)next;
        return 27;
    }

    int params[4] = {0, 0, 0, 0};
    int nparams = 0;
    int value = 0;
    bool have = false;

    for (int guard = 0; guard < 12; ++guard) {
        char ch = 0;
        if (!ed_try_char(&ch, 35)) {
            return 27;
        }
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            have = true;
            continue;
        }
        if (ch == ';') {
            if (nparams < 4) {
                params[nparams++] = value;
            }
            value = 0;
            have = false;
            continue;
        }
        if (have && nparams < 4) {
            params[nparams++] = value;
        }

        switch (ch) {
            case 'A': return EDK_UP;
            case 'B': return EDK_DOWN;
            case 'C': return EDK_RIGHT;
            case 'D': return EDK_LEFT;
            case 'H': return EDK_HOME;
            case 'F': return EDK_END;
            case '~': {
                int p = nparams > 0 ? params[0] : 0;
                if (p == 1 || p == 7) return EDK_HOME;
                if (p == 2) return EDK_INSERT;
                if (p == 3) return EDK_DELETE;
                if (p == 4 || p == 8) return EDK_END;
                if (p == 5) return EDK_PGUP;
                if (p == 6) return EDK_PGDN;
                return 0;
            }
            default:
                return 0;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Renderizado                                                         */
/* ------------------------------------------------------------------ */
static const char* ed_mode_name(int vi_mode)
{
    if (g_kind == AX_EDITOR_MODE_EDIT) {
        return "EDIT";
    }
    if (vi_mode == VI_INSERT) {
        return "INSERT";
    }
    if (vi_mode == VI_COMMAND) {
        return "CMD";
    }
    if (g_kind == AX_EDITOR_MODE_NVIM) {
        return "NVIM";
    }
    if (g_kind == AX_EDITOR_MODE_VIM) {
        return "VIM";
    }
    return "VI";
}

static void ed_render(int vi_mode)
{
    uint64_t line = ed_line_number(g_cursor);
    uint64_t col = ed_column(g_cursor);
    int number_width = g_show_numbers ? 5 : 0;

    if ((int)line - 1 < g_scroll) {
        g_scroll = (int)line - 1;
    }
    if ((int)line - 1 >= g_scroll + AX_ED_ROWS) {
        g_scroll = (int)line - AX_ED_ROWS;
    }
    if (g_scroll < 0) {
        g_scroll = 0;
    }

    ax_puts("\x1b[?25l");

    uint64_t pos = 0;
    for (int i = 0; i < g_scroll && pos < g_len; ++i) {
        pos = ed_line_end(pos);
        if (pos < g_len) {
            pos++;
        }
    }

    for (int row = 0; row < AX_ED_ROWS; ++row) {
        ax_printf("\x1b[%d;1H\x1b[K", row + 1);

        uint64_t start = pos;
        uint64_t end = ed_line_end(start);
        int out_col = 0;

        if (g_show_numbers) {
            uint64_t n = (uint64_t)g_scroll + (uint64_t)row + 1;
            char num[16];
            int digits = 0;
            if (n == 0) {
                num[digits++] = '0';
            } else {
                char rev[16];
                int rn = 0;
                while (n > 0 && rn < (int)sizeof(rev)) {
                    rev[rn++] = (char)('0' + (n % 10));
                    n /= 10;
                }
                while (rn > 0) {
                    num[digits++] = rev[--rn];
                }
            }
            for (int pad = digits; pad < 5; ++pad) {
                ax_putc(' ');
            }
            for (int d = 0; d < digits; ++d) {
                ax_putc(num[d]);
            }
            ax_putc(' ');
            out_col = 6;
        }

        for (uint64_t p = start; p < end; ++p) {
            char c = g_text[p];
            if (c == '\t') {
                int spaces = 4 - (out_col % 4);
                for (int s = 0; s < spaces && out_col < AX_ED_COLS - 1; ++s) {
                    ax_putc(' ');
                    out_col++;
                }
            } else {
                if ((unsigned char)c < 32 || (unsigned char)c >= 127) {
                    c = '.';
                }
                if (out_col < AX_ED_COLS - 1) {
                    ax_putc(c);
                    out_col++;
                }
            }
        }

        if (pos < g_len) {
            pos = end + 1;
        } else {
            pos = end;
        }
    }

    /* Barra de estado. */
    ax_printf("\x1b[%d;1H\x1b[7m", AX_ED_STATUS_ROW);
    ax_printf(" %s  %s  Ln %llu, Col %llu %s ",
              ed_mode_name(vi_mode), g_name,
              (unsigned long long)line, (unsigned long long)(col + 1),
              g_modified ? "[mod]" : "");
    ax_puts("\x1b[0m\x1b[K");

    /* Linea de mensajes. */
    ax_printf("\x1b[%d;1H\x1b[K", AX_ED_MSG_ROW);
    if (g_msg[0]) {
        ax_puts(g_msg);
    } else if (g_kind == AX_EDITOR_MODE_EDIT) {
        ax_puts("Ctrl+S guardar   Ctrl+Q salir   Ctrl+Z deshacer");
    } else {
        ax_puts("i insertar   : comandos   / buscar   u deshacer");
    }

    /* Cursor real. */
    int cursor_row = (int)line - g_scroll;
    int cursor_col = number_width + (int)col;
    if (cursor_row < 1) cursor_row = 1;
    if (cursor_row > AX_ED_ROWS) cursor_row = AX_ED_ROWS;
    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col >= AX_ED_COLS) cursor_col = AX_ED_COLS - 1;
    ax_printf("\x1b[%d;%dH\x1b[?25h", cursor_row, cursor_col + 1);
}

/* ------------------------------------------------------------------ */
/* Guardado y dialogos                                                 */
/* ------------------------------------------------------------------ */
static bool ed_save(void)
{
    if (!ax_fs_disk_mounted()) {
        ed_set_msg("no hay volumen AxFS montado (format <disco>)");
        return false;
    }
    int rc = ax_fs_disk_write(g_path, g_text, g_len);
    if (rc != 0) {
        ed_set_msg("error al guardar (AxFS)");
        return false;
    }
    g_modified = false;
    char msg[96];
    uint64_t n = 0;
    const char* prefix = "guardado: ";
    while (prefix[n]) {
        msg[n] = prefix[n];
        n++;
    }
    uint64_t size = g_len;
    char digits[24];
    int dn = 0;
    if (size == 0) {
        digits[dn++] = '0';
    }
    while (size && dn < (int)sizeof(digits)) {
        digits[dn++] = (char)('0' + (size % 10));
        size /= 10;
    }
    while (dn > 0 && n + 1 < sizeof(msg)) {
        msg[n++] = digits[--dn];
    }
    const char* suffix = " bytes";
    for (int i = 0; suffix[i] && n + 1 < sizeof(msg); ++i) {
        msg[n++] = suffix[i];
    }
    msg[n] = '\0';
    ed_set_msg(msg);
    return true;
}

static int ed_prompt(const char* label, char* out, int max)
{
    int n = 0;
    out[0] = '\0';
    ax_printf("\x1b[%d;1H\x1b[K%s\x1b[?25h", AX_ED_MSG_ROW, label);

    for (;;) {
        int key = ed_read_key();
        if (key == 27) {
            out[0] = '\0';
            return 0;
        }
        if (key == '\n') {
            out[n] = '\0';
            return 1;
        }
        if (key == '\b') {
            if (n > 0) {
                n--;
                out[n] = '\0';
            }
        } else if (key >= 32 && key < 127 && n + 1 < max) {
            out[n++] = (char)key;
            out[n] = '\0';
        }
        ax_printf("\x1b[%d;1H\x1b[K%s%s", AX_ED_MSG_ROW, label, out);
    }
}

static bool ed_confirm_save(void)
{
    ax_printf("\x1b[%d;1H\x1b[KGuardar cambios? (s/n/ESC): \x1b[?25h",
              AX_ED_MSG_ROW);
    for (;;) {
        int key = ed_read_key();
        if (key == 's' || key == 'S') {
            return ed_save();
        }
        if (key == 'n' || key == 'N') {
            return true;
        }
        if (key == 27) {
            return false;
        }
    }
}

static void ed_search(void)
{
    char needle[96];
    if (!ed_prompt("/", needle, sizeof(needle))) {
        ed_set_msg("");
        return;
    }
    if (!needle[0]) {
        ed_set_msg("");
        return;
    }
    uint64_t nlen = ed_strlen(needle);
    uint64_t found = (uint64_t)-1;

    for (uint64_t start = g_cursor + 1; start + nlen <= g_len; ++start) {
        uint64_t i = 0;
        while (i < nlen && g_text[start + i] == needle[i]) {
            i++;
        }
        if (i == nlen) {
            found = start;
            break;
        }
    }
    if (found == (uint64_t)-1) {
        for (uint64_t start = 0; start + nlen <= g_len; ++start) {
            uint64_t i = 0;
            while (i < nlen && g_text[start + i] == needle[i]) {
                i++;
            }
            if (i == nlen) {
                found = start;
                break;
            }
        }
    }
    if (found == (uint64_t)-1) {
        ed_set_msg("no encontrado");
    } else {
        g_cursor = found;
        ed_set_msg("encontrado");
    }
}

/* ------------------------------------------------------------------ */
/* Comandos de vi                                                      */
/* ------------------------------------------------------------------ */
static void ed_goto_line(uint64_t line)
{
    uint64_t pos = 0;
    uint64_t current = 1;
    while (pos < g_len && current < line) {
        if (g_text[pos] == '\n') {
            current++;
        }
        pos++;
    }
    g_cursor = pos;
}

static bool ed_command(char* cmd, int vi_mode, bool* quit)
{
    AX_UNUSED(vi_mode);
    while (*cmd == ' ' || *cmd == ':') {
        cmd++;
    }
    if (!*cmd) {
        return true;
    }
    if (ed_starts(cmd, "wq")) {
        if (ed_save()) {
            *quit = true;
        }
        return true;
    }
    if (ed_starts(cmd, "w")) {
        ed_save();
        return true;
    }
    if (ed_starts(cmd, "q!")) {
        *quit = true;
        return true;
    }
    if (ed_starts(cmd, "q")) {
        if (g_modified) {
            ed_set_msg("hay cambios sin guardar (usa :q!)");
        } else {
            *quit = true;
        }
        return true;
    }
    if (ed_starts(cmd, "set nu")) {
        g_show_numbers = true;
        ed_set_msg("numeros de linea activados");
        return true;
    }
    if (ed_starts(cmd, "set nonu")) {
        g_show_numbers = false;
        ed_set_msg("numeros de linea desactivados");
        return true;
    }
    if (cmd[0] >= '0' && cmd[0] <= '9') {
        uint64_t line = 0;
        while (*cmd >= '0' && *cmd <= '9') {
            line = line * 10 + (uint64_t)(*cmd - '0');
            cmd++;
        }
        if (line == 0) {
            line = 1;
        }
        ed_goto_line(line);
        ed_set_msg("ir a linea");
        return true;
    }
    ed_set_msg("comando no soportado");
    return true;
}

/* ------------------------------------------------------------------ */
/* Bucles de edicion                                                   */
/* ------------------------------------------------------------------ */
static void ed_run_edit(void)
{
    bool quit = false;
    ed_render(0);
    while (!quit) {
        int key = ed_read_key();
        switch (key) {
            case EDK_UP:    ed_move_vertical(-1); break;
            case EDK_DOWN:  ed_move_vertical(1); break;
            case EDK_LEFT:
                if (g_cursor > 0) g_cursor--;
                break;
            case EDK_RIGHT:
                if (g_cursor < g_len) g_cursor++;
                break;
            case EDK_HOME:  g_cursor = ed_line_start(g_cursor); break;
            case EDK_END:   g_cursor = ed_line_end(g_cursor); break;
            case EDK_PGUP:  ed_move_page(-1); break;
            case EDK_PGDN:  ed_move_page(1); break;
            case EDK_DELETE:
                ed_undo_snapshot();
                ed_delete_char();
                break;
            case '\n':
                ed_undo_snapshot();
                ed_insert_char('\n');
                break;
            case '\b':
                ed_undo_snapshot();
                ed_backspace();
                break;
            case 19:        /* Ctrl+S */
                ed_save();
                break;
            case 17:        /* Ctrl+Q */
                if (!g_modified || ed_confirm_save()) {
                    quit = true;
                }
                break;
            case 26:        /* Ctrl+Z */
                ed_undo_restore();
                break;
            case 27:
                if (!g_modified || ed_confirm_save()) {
                    quit = true;
                }
                break;
            case '\t':
                ed_undo_snapshot();
                ed_insert_char('\t');
                break;
            default:
                if (key >= 32 && key < 127) {
                    ed_undo_snapshot();
                    ed_insert_char((char)key);
                }
                break;
        }
        if (!quit) {
            ed_render(0);
        }
    }
}

static void ed_run_vi(void)
{
    bool quit = false;
    int mode = VI_NORMAL;
    int pending = 0;
    int insert_snapshot = 0;

    ed_render(mode);
    while (!quit) {
        int key = ed_read_key();

        if (mode == VI_INSERT) {
            if (key == 27) {
                mode = VI_NORMAL;
                if (g_cursor > 0 && g_text[g_cursor - 1] != '\n') {
                    g_cursor--;
                }
                insert_snapshot = 0;
                ed_set_msg("");
            } else if (key == '\n') {
                if (!insert_snapshot) { ed_undo_snapshot(); insert_snapshot = 1; }
                ed_insert_char('\n');
            } else if (key == '\b') {
                if (!insert_snapshot) { ed_undo_snapshot(); insert_snapshot = 1; }
                ed_backspace();
            } else if (key == EDK_DELETE) {
                if (!insert_snapshot) { ed_undo_snapshot(); insert_snapshot = 1; }
                ed_delete_char();
            } else if (key == EDK_UP) {
                ed_move_vertical(-1);
            } else if (key == EDK_DOWN) {
                ed_move_vertical(1);
            } else if (key == EDK_LEFT) {
                if (g_cursor > 0) g_cursor--;
            } else if (key == EDK_RIGHT) {
                if (g_cursor < g_len) g_cursor++;
            } else if (key == EDK_HOME) {
                g_cursor = ed_line_start(g_cursor);
            } else if (key == EDK_END) {
                g_cursor = ed_line_end(g_cursor);
            } else if (key == 19) {
                ed_save();
            } else if (key >= 32 && key < 127) {
                if (!insert_snapshot) { ed_undo_snapshot(); insert_snapshot = 1; }
                ed_insert_char((char)key);
            } else if (key == '\t') {
                if (!insert_snapshot) { ed_undo_snapshot(); insert_snapshot = 1; }
                ed_insert_char('\t');
            }
            ed_render(mode);
            continue;
        }

        if (mode == VI_COMMAND) {
            char cmd[96];
            if (ed_prompt(":", cmd, sizeof(cmd))) {
                ed_command(cmd, mode, &quit);
            } else {
                ed_set_msg("");
            }
            mode = VI_NORMAL;
            if (!quit) {
                ed_render(mode);
            }
            continue;
        }

        /* Modo normal. */
        if (pending == 'd') {
            pending = 0;
            if (key == 'd') {
                ed_undo_snapshot();
                ed_delete_line();
                ed_set_msg("linea borrada");
            }
            ed_render(mode);
            continue;
        }
        if (pending == 'y') {
            pending = 0;
            if (key == 'y') {
                ed_yank_line();
            }
            ed_render(mode);
            continue;
        }
        if (pending == 'g') {
            pending = 0;
            if (key == 'g') {
                g_cursor = 0;
                ed_set_msg("inicio del fichero");
            }
            ed_render(mode);
            continue;
        }

        switch (key) {
            case ':':
                mode = VI_COMMAND;
                break;
            case '/':
                ed_search();
                break;
            case 'i':
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            case 'a':
                if (g_cursor < g_len && g_text[g_cursor] != '\n') {
                    g_cursor++;
                }
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            case 'A':
                g_cursor = ed_line_end(g_cursor);
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            case 'I':
                g_cursor = ed_line_start(g_cursor);
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            case 'o': {
                uint64_t end = ed_line_end(g_cursor);
                ed_undo_snapshot();
                g_cursor = end;
                ed_insert_char('\n');
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            }
            case 'O': {
                uint64_t start = ed_line_start(g_cursor);
                ed_undo_snapshot();
                g_cursor = start;
                ed_insert_char('\n');
                g_cursor = start;
                ed_begin_insert();
                mode = VI_INSERT;
                insert_snapshot = 0;
                break;
            }
            case 'h':
                if (g_cursor > 0) g_cursor--;
                break;
            case 'l':
                if (g_cursor < g_len) g_cursor++;
                break;
            case 'j':
                ed_move_vertical(1);
                break;
            case 'k':
                ed_move_vertical(-1);
                break;
            case '0':
            case '^':
                g_cursor = ed_line_start(g_cursor);
                break;
            case '$':
                g_cursor = ed_line_end(g_cursor);
                break;
            case 'w':
                ed_move_word(1);
                break;
            case 'b':
                ed_move_word(-1);
                break;
            case 'G':
                g_cursor = g_len;
                break;
            case 'x':
                ed_undo_snapshot();
                ed_delete_char();
                break;
            case 'X':
                ed_undo_snapshot();
                ed_backspace();
                break;
            case 'd':
                pending = 'd';
                break;
            case 'y':
                pending = 'y';
                break;
            case 'g':
                pending = 'g';
                break;
            case 'p':
                ed_paste();
                break;
            case 'u':
                ed_undo_restore();
                break;
            case 19:
                ed_save();
                break;
            case 27:
                ed_set_msg("");
                break;
            default:
                break;
        }
        ed_render(mode);
    }
}

/* ------------------------------------------------------------------ */
/* Entrada publica                                                     */
/* ------------------------------------------------------------------ */
int ax_editor_run(const char* cwd, const char* path, int kind)
{
    AX_UNUSED(cwd);
    if (!path || !path[0]) {
        ax_puts("editor: falta el nombre del fichero\n");
        return AX_EINVAL;
    }
    if (!ax_fs_disk_mounted()) {
        ax_puts("editor: no hay volumen AxFS montado.\n"
                "        Usa 'disks' y 'format <n>' antes de editar.\n");
        return AX_ENOENT;
    }

    g_kind = kind;
    g_len = 0;
    g_cursor = 0;
    g_scroll = 0;
    g_modified = false;
    g_undo_valid = false;
    g_show_numbers = false;
    g_yank_len = 0;
    g_yank_line = false;
    g_msg[0] = '\0';

    uint64_t i = 0;
    while (path[i] && i + 1 < sizeof(g_path)) {
        g_path[i] = path[i];
        i++;
    }
    g_path[i] = '\0';

    const char* base = ed_basename(path);
    i = 0;
    while (base[i] && i + 1 < sizeof(g_name)) {
        g_name[i] = base[i];
        i++;
    }
    g_name[i] = '\0';

    uint64_t got = 0;
    int rc = ax_fs_disk_read(path, g_text, AX_ED_MAX - 1, &got);
    if (rc == 0) {
        g_len = got;
        if (got >= AX_ED_MAX - 1) {
            ed_set_msg("aviso: fichero truncado a 64 KiB");
        }
    } else if (rc == AX_ENOENT) {
        g_len = 0;
        ed_set_msg("fichero nuevo (Ctrl+S o :w para guardar)");
    } else {
        ax_printf("editor: no se pudo abrir '%s' (error %d)\n", path, rc);
        return rc;
    }
    g_text[g_len] = '\0';

    ax_console_clear();
    ax_puts("\x1b[2J\x1b[H");

    if (kind == AX_EDITOR_MODE_EDIT) {
        ed_run_edit();
    } else {
        ed_run_vi();
    }

    ax_puts("\x1b[?25h\x1b[0m\x1b[2J\x1b[H");
    ax_console_clear();
    return 0;
}
