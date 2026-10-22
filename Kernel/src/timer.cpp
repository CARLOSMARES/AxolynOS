/*
 * AxolynOS - Temporizador del kernel basado en el PIT 8253/8254.
 *
 * El PIT (canal 0) se programa en modo 3 (onda cuadrada) con un divisor que
 * da la frecuencia AX_TIMER_FREQ_HZ (100 Hz).  Cada interrupcion del
 * temporizador suma un tick; el planificador (otro modulo) recibe el aviso
 * desde aqui.
 */

#include "ax_types.h"
#include "ax_console.h"
#include "ax_sched.h"

/* ------------------------------------------------------------------ */
/* Referencia debil al planificador                                     */
/* ------------------------------------------------------------------ */
/*
 * ax_sched_tick() lo implementa el modulo del planificador.  La referencia
 * es debil para que el kernel pueda arrancar (y compilarse) aunque ese modulo
 * todavia no exista; si no esta, el simbolo vale NULL.
 */
extern void ax_sched_tick(void) __attribute__((weak));

/* ------------------------------------------------------------------ */
/* Puertos del PIT                                                      */
/* ------------------------------------------------------------------ */
#define AX_PIT_CHANNEL0 0x40
#define AX_PIT_COMMAND  0x43

/* Frecuencia base del PIT: 1.193182 MHz */
#define AX_PIT_BASE_FREQ 1193182u

/* ------------------------------------------------------------------ */
/* Estado del temporizador                                              */
/* ------------------------------------------------------------------ */
static volatile uint64_t ax_timer_tick_count = 0;

static inline void ax_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void ax_timer_init(void)
{
    /*
     * Divisor: 1193182 / 100 = 11931.82.  Usamos 11932, que da 99.9985 Hz
     * (un error del 0.0015 %, tres veces menor que con 11931) y por tanto
     * deriva de reloj minima.
     */
    uint16_t divisor = (uint16_t)((AX_PIT_BASE_FREQ + (AX_TIMER_FREQ_HZ / 2)) / AX_TIMER_FREQ_HZ);

    /* Byte de control: canal 0, acceso lobyte/hibyte, modo 3, contador
       binario.  0x36 = 0011_0110b. */
    ax_outb(AX_PIT_COMMAND, 0x36);
    ax_outb(AX_PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    ax_outb(AX_PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void ax_timer_on_tick(void)
{
    ax_timer_tick_count++;

    /* Avisamos al planificador para que despierte a los hilos que han
       expirado su temporizador. */
    if (ax_sched_tick) {
        ax_sched_tick();
    }
}

uint64_t ax_timer_ticks(void)
{
    return ax_timer_tick_count;
}

uint64_t ax_timer_ms(void)
{
    /* ticks * 1000 / frecuencia.  Se calcula con enteros de 64 bits para no
       perder precision en las primeras horas de actividad. */
    return (ax_timer_tick_count * 1000ull) / (uint64_t)AX_TIMER_FREQ_HZ;
}

/*
 * Tiempo de actividad del sistema.  La implementacion definitive vive en el
 * modulo del planificador (ax_sched.h); esta es una version debil que solo
 * actua si nadie mas la define.  Al ser debil, si el planificador declara su
 * propia version, el enlazador usara esa.
 */
__attribute__((weak)) uint64_t ax_uptime_ms(void)
{
    return ax_timer_ms();
}
