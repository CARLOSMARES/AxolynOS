/*
 * AxolynOS - Mecanismo de llamada al sistema (user mode -> kernel).
 *
 * Las aplicaciones de user mode invocan al kernel con la instruccion
 * `int 0x80` (vector AX_SYSCALL_VECTOR). El reparto de argumentos y el
 * valor de retorno son los del ABI de syscalls de x86-64:
 *
 *      RAX = numero de syscall
 *      RDI = arg1   RSI = arg2   RDX = arg3
 *      R10 = arg4   R8  = arg5   R9  = arg6
 *      RAX = valor de retorno
 *
 * El kernel restaura RBX, RBP y R12-R15 (registros callee-saved), por lo
 * que el codigo generado por el compilador puede conservarlos a traves de
 * la llamada. RCX, R11, R8, R9 y R10 si son caller-saved: el compilador
 * debe asumir que cambian, y por eso se declaran como clobber.
 *
 * CONVENCION DE RETORNO (identica en TODAS las syscalls de AxolynOS):
 *
 *      ret >= 0   -> exito. El valor es el resultado de la operacion
 *                   (bytes escritos, bytes leidos, fd, pid, ...).
 *      ret <  0   -> error. El valor es -errno, con los codigos definidos
 *                   en axos/abi.h (AX_ENOENT, AX_EINVAL, AX_ENOSPC, ...).
 *
 * Nunca se devuelve un errno positivo ni un valor de exito negativo.
 */
#ifndef AXOS_SYSCALL_H
#define AXOS_SYSCALL_H

#include "axos/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Atributos utiles                                                   */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
#  define AX_NORETURN   __attribute__((noreturn))
#  define AX_UNUSED     __attribute__((unused))
#  define AX_NOINLINE   __attribute__((noinline))
#else
#  define AX_NORETURN
#  define AX_UNUSED
#  define AX_NOINLINE
#endif

/*
 * Registros que la interrupcion puede dejar sucios. RCX y R11 son los
 * clasicos "caller-saved" que el hardware salva en la pila al entrar en
 * el manejador; R10, R8 y R9 se usan para los argumentos 4..6, asi que
 * su contenido previo no es significativo. "cc" porque la interrupcion
 * puede alterar las banderas.
 */
#define AX_SYSCALL_CLOBBERS "memory", "cc", "rcx", "r11", "r10", "r8", "r9"

/*
 * Nucleo de las syscalls: seis argumentos.
 *
 * Los argumentos 4, 5 y 6 se copian a R10, R8 y R9 justo antes de la
 * interrupcion. Se usan los operandos "rm" (y no registros fijos) porque
 * R10/R8/R9 estan declarados como clobber: asi el compilador garantiza
 * que jamas deja un valor vivo ahi y nunca genera una referencia que
 * solape con la interrupcion.
 *
 * El asm es `volatile` y declara clobber de memoria porque la llamada
 * puede modificar cualquier objeto visible por el compilador.
 */
static inline long ax_syscall6(long nr,
                                long a1, long a2, long a3,
                                long a4, long a5, long a6)
{
    long ret;

    __asm__ __volatile__(
        "movq %[in4], %%r10\n\t"
        "movq %[in5], %%r8\n\t"
        "movq %[in6], %%r9\n\t"
        "int $0x80"
        : "=a" (ret)
        : "a"  (nr),
          "D"  (a1),
          "S"  (a2),
          "d"  (a3),
          [in4] "rm" (a4),
          [in5] "rm" (a5),
          [in6] "rm" (a6)
        : AX_SYSCALL_CLOBBERS);

    return ret;
}

/* Variantes con menos argumentos: los sobrantes se pasan como 0. */
static inline long ax_syscall5(long nr, long a1, long a2, long a3, long a4, long a5)
{
    return ax_syscall6(nr, a1, a2, a3, a4, a5, 0);
}

static inline long ax_syscall4(long nr, long a1, long a2, long a3, long a4)
{
    return ax_syscall6(nr, a1, a2, a3, a4, 0, 0);
}

static inline long ax_syscall3(long nr, long a1, long a2, long a3)
{
    return ax_syscall6(nr, a1, a2, a3, 0, 0, 0);
}

static inline long ax_syscall2(long nr, long a1, long a2)
{
    return ax_syscall6(nr, a1, a2, 0, 0, 0, 0);
}

static inline long ax_syscall1(long nr, long a1)
{
    return ax_syscall6(nr, a1, 0, 0, 0, 0, 0);
}

static inline long ax_syscall0(long nr)
{
    return ax_syscall6(nr, 0, 0, 0, 0, 0, 0);
}

/*
 * ax_syscall_ret(nr, ...) - realization de una syscall con la convencion
 * de retorno de AxolynOS:
 *
 *      >= 0  -> exito, el valor devuelto es el resultado.
 *      <  0  -> error, el valor devuelto es -errno.
 *
 * Es un macro que selecciona automaticamente la variante de aridad
 * adecuada (ax_syscall0 .. ax_syscall6) segun cuantos argumentos se le
 * pasen. Devuelve el valor CRUDO de la syscall (sin reinterpretar).
 */
#define AX_SYSCALL_PICK_(_1, _2, _3, _4, _5, _6, _NAME, ...) _NAME
#define ax_syscall_ret(...)                                                   \
    AX_SYSCALL_PICK_(__VA_ARGS__,                                              \
                      ax_syscall6, ax_syscall5, ax_syscall4,                   \
                      ax_syscall3, ax_syscall2, ax_syscall1, ax_syscall0)      \
    (__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Ayudas sobre el valor devuelto                                     */
/* ------------------------------------------------------------------ */

/* 1 si la syscall fallo (ret < 0). */
static inline int ax_syscall_failed(long ret)
{
    return ret < 0 ? 1 : 0;
}

/* Codigo de error (-errno) o 0 si la syscall fue correcta. */
static inline int ax_syscall_errno(long ret)
{
    return ret < 0 ? (int)(-ret) : 0;
}

/* 1 si la syscall fue correcta (ret >= 0). */
static inline int ax_syscall_ok(long ret)
{
    return ret >= 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Llamadas especiales                                               */
/* ------------------------------------------------------------------ */

/*
 * Termina el proceso actual. AX_SYS_EXIT nunca retorna en el kernel,
 * pero si el kernel devolviera el control se entraria en un bucle
 * infinito para no seguir ejecutando codigo de usuario.
 */
AX_NORETURN static inline void ax_syscall_exit(int code)
{
    ax_syscall1(AX_SYS_EXIT, code);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_SYSCALL_H */
