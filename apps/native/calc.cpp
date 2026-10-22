/*
 * AxolynOS - Aplicacion "calc".
 *
 * Calculadora de linea de comandos para expresiones aritmeticas enteras.
 * Admite: + - * / % ( ) y numeros decimales o hexadecimales (0x...).
 *
 * Uso:
 *      calc "2 + 3 * (4 - 1)"     -> evalua la expresion
 *      calc                       -> modo REPL: lee lineas de la entrada
 *                                   estandar hasta EOF o "salir"
 *
 * Devuelve 0 si todas las expresiones se evaluaron bien y 1 si alguna
 * fallo (sintaxis incorrecta, division por cero, etc.).
 */
#include "axos/axos.hpp"

namespace {

/* Estado del analizador sintactico. */
const char* g_text = 0;
bool        g_error = false;

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return 0;
}

void skip_spaces()
{
    while (*g_text == ' ' || *g_text == '\t') {
        ++g_text;
    }
}

long parse_expression();

/* Un numero entero (decimal o hexadecimal). */
long parse_number()
{
    long value = 0;

    skip_spaces();

    if (g_text[0] == '0' && (g_text[1] == 'x' || g_text[1] == 'X')) {
        g_text += 2;
        if (!is_hex(*g_text)) {
            g_error = true;
            return 0;
        }
        while (is_hex(*g_text)) {
            value = value * 16 + hex_value(*g_text);
            ++g_text;
        }
        return value;
    }

    if (!is_digit(*g_text)) {
        g_error = true;
        return 0;
    }
    while (is_digit(*g_text)) {
        value = value * 10 + (*g_text - '0');
        ++g_text;
    }
    return value;
}

/* Un factor: un numero, un parentesis o un signo unario. */
long parse_factor()
{
    skip_spaces();

    if (*g_text == '(') {
        long value;
        ++g_text;
        value = parse_expression();
        skip_spaces();
        if (*g_text != ')') {
            g_error = true;
            return 0;
        }
        ++g_text;
        return value;
    }
    if (*g_text == '-') {
        ++g_text;
        return -parse_factor();
    }
    if (*g_text == '+') {
        ++g_text;
        return parse_factor();
    }
    return parse_number();
}

/* Un termino: factor (('*' | '/' | '%') factor)* */
long parse_term()
{
    long value = parse_factor();

    for (;;) {
        char op;
        long right;

        skip_spaces();
        op = *g_text;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++g_text;

        right = parse_factor();
        if (g_error) {
            return 0;
        }

        if (op == '*') {
            value *= right;
        } else if (op == '/') {
            if (right == 0) {
                g_error = true;
                return 0;
            }
            value /= right;
        } else {
            if (right == 0) {
                g_error = true;
                return 0;
            }
            value %= right;
        }
    }
    return value;
}

/* Una expresion: term (('+' | '-') term)* */
long parse_expression()
{
    long value = parse_term();

    for (;;) {
        char op;
        long right;

        skip_spaces();
        op = *g_text;
        if (op != '+' && op != '-') {
            break;
        }
        ++g_text;

        right = parse_term();
        if (g_error) {
            return 0;
        }
        if (op == '+') {
            value += right;
        } else {
            value -= right;
        }
    }
    return value;
}

/* Evalua una expresion completa. Devuelve false si hay un error. */
bool evaluate(const char* text, long* result)
{
    g_text  = text;
    g_error = false;

    *result = parse_expression();
    if (g_error) {
        return false;
    }

    skip_spaces();
    if (*g_text != '\0') {
        g_error = true;
        return false;
    }
    return true;
}

} /* namespace */

int main(int argc, char** argv)
{
    long result = 0;
    int  status = 0;

    /* Modo expresion unica: el primer argumento es la expresion. */
    if (argc > 1) {
        if (!evaluate(argv[1], &result)) {
            axos::println("calc: expresion invalida");
            return 1;
        }
        axos::print_int(result);
        axos::println("");
        return 0;
    }

    /* Modo REPL: se leen lineas de la entrada estandar. */
    axos::println("Calculadora de AxolynOS. Escribe una expresion o 'salir' para terminar.");

    for (;;) {
        char   line[256];
        long   n;

        axos::print("> ");
        n = axos::gets(line, sizeof(line));
        if (n < 0) {
            axos::println("");
            break;      /* fin de la entrada (EOF) */
        }
        if (n == 0) {
            break;
        }

        if (axos::strcmp(line, "salir") == 0 || axos::strcmp(line, "quit") == 0) {
            break;
        }
        if (line[0] == '\0') {
            continue;
        }

        if (!evaluate(line, &result)) {
            axos::println("error de sintaxis");
            status = 1;
            continue;
        }
        axos::print("= ");
        axos::print_int(result);
        axos::println("");
    }

    return status;
}
