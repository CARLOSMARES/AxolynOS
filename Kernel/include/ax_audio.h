/*
 * AxolynOS - Driver de audio (Intel HDA / AC97 - esqueleto)
 */
#ifndef AX_AUDIO_H
#define AX_AUDIO_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ax_audio_init(void);
int  ax_audio_play(const void* data, uint64_t size);
int  ax_audio_set_volume(uint8_t vol);

#ifdef __cplusplus
}
#endif

#endif /* AX_AUDIO_H */
