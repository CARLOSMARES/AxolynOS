/*
 * AxolynOS - Driver de audio (esqueleto)
 */
#include "ax_audio.h"
#include "ax_console.h"

void ax_audio_init(void)
{
    ax_printf("[audio] inicializando driver de audio...\n");
}

int ax_audio_play(const void* data, uint64_t size)
{
    (void)data; (void)size;
    return -1;
}

int ax_audio_set_volume(uint8_t vol)
{
    (void)vol;
    return 0;
}
