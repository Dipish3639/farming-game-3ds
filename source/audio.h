// Tiny chiptune synthesizer: background music + sound effects, rendered to
// mono 16-bit PCM by the platform layer.
#pragma once
#include <stdint.h>

#define AUDIO_RATE 22050

enum { MUS_NONE = -1, MUS_DAY, MUS_NIGHT, NUM_SONGS };

enum {
    SFX_TILL,
    SFX_WATER,
    SFX_CUT,
    SFX_BREAK,
    SFX_PLANT,
    SFX_HARVEST,
    SFX_COIN,
    SFX_MENU_MOVE,
    SFX_MENU_OK,
    SFX_MENU_BACK,
    SFX_ERROR,
    SFX_STEP,
    SFX_REFILL,
    SFX_EAT,
    SFX_QUEST,
    SFX_SLEEP,
    SFX_PLACE,
    NUM_SFX
};

void audio_init(void);
void audio_render(int16_t *out, int samples);
void audio_music(int song);
void audio_sfx(int id);
void audio_set_music_enabled(int on);
