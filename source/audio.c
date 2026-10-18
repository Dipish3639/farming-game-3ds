#include "audio.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

enum { W_SQUARE, W_TRI, W_SINE, W_NOISE };

typedef struct {
    int wave;
    float duty, vol;
    float attack, decay, sustain, release; // seconds, sustain is a level
    float vibrato;                          // relative pitch wobble
} Instr;

typedef struct {
    int active, gate, wave;
    float duty, vol;
    float attack, decay, sustain, release, vibrato;
    float f0, f1, sweep; // frequency sweep f0 -> f1 over `sweep` seconds (0 = none)
    float phase, env, t;
    uint32_t lfsr;
    float noise;
} Voice;

static void voice_start(Voice *v, const Instr *in, float f0, float f1, float sweep)
{
    v->active = 1;
    v->gate = 1;
    v->wave = in->wave;
    v->duty = in->duty;
    v->vol = in->vol;
    v->attack = in->attack;
    v->decay = in->decay;
    v->sustain = in->sustain;
    v->release = in->release;
    v->vibrato = in->vibrato;
    v->f0 = f0;
    v->f1 = f1;
    v->sweep = sweep;
    v->t = 0;
    if (!v->lfsr) v->lfsr = 0xACE1u;
}

static float sine_tab[256];

static float voice_sample(Voice *v)
{
    const float dt = 1.0f / AUDIO_RATE;
    if (!v->active) return 0;

    // envelope
    if (v->gate) {
        if (v->t < v->attack) v->env = v->t / v->attack;
        else if (v->t < v->attack + v->decay) v->env = 1.0f - (1.0f - v->sustain) * (v->t - v->attack) / v->decay;
        else v->env = v->sustain;
        if (v->env <= 0.0001f && v->t > v->attack) v->active = 0;
    } else {
        v->env -= dt / (v->release > 0.001f ? v->release : 0.001f);
        if (v->env <= 0) {
            v->env = 0;
            v->active = 0;
        }
    }

    float f = v->f0;
    if (v->sweep > 0) f = v->t < v->sweep ? v->f0 + (v->f1 - v->f0) * (v->t / v->sweep) : v->f1;
    if (v->vibrato > 0 && v->t > 0.15f) f *= 1.0f + v->vibrato * sine_tab[(int)(v->t * 5.5f * 256) & 255];
    v->t += dt;
    v->phase += f * dt;
    float out;
    if (v->phase >= 1.0f) {
        v->phase -= (float)(int)v->phase;
        if (v->wave == W_NOISE) {
            uint32_t bit = ((v->lfsr >> 0) ^ (v->lfsr >> 2) ^ (v->lfsr >> 3) ^ (v->lfsr >> 5)) & 1u;
            v->lfsr = (v->lfsr >> 1) | (bit << 15);
            v->noise = (v->lfsr & 1) ? 1.0f : -1.0f;
        }
    }
    switch (v->wave) {
    case W_SQUARE: out = v->phase < v->duty ? 1.0f : -1.0f; break;
    case W_TRI: out = 4.0f * fabsf(v->phase - 0.5f) - 1.0f; break;
    case W_SINE: out = sine_tab[(int)(v->phase * 256) & 255]; break;
    default: out = v->noise; break;
    }
    return out * v->env * v->vol;
}

// ---------------------------------------------------------------------------
// Music

static const Instr I_LEAD = {W_SQUARE, 0.25f, 0.11f, 0.004f, 0.20f, 0.55f, 0.08f, 0.004f};
static const Instr I_ARP = {W_SQUARE, 0.125f, 0.040f, 0.002f, 0.12f, 0.25f, 0.05f, 0};
static const Instr I_BASS = {W_TRI, 0.5f, 0.26f, 0.003f, 0.15f, 0.70f, 0.04f, 0};
static const Instr I_NLEAD = {W_SINE, 0.5f, 0.17f, 0.030f, 0.40f, 0.50f, 0.35f, 0.006f};
static const Instr I_NARP = {W_TRI, 0.5f, 0.11f, 0.005f, 0.35f, 0.25f, 0.20f, 0};
static const Instr I_NBASS = {W_TRI, 0.5f, 0.15f, 0.100f, 0.50f, 0.60f, 0.50f, 0};
static const Instr I_DRUM = {W_NOISE, 0.5f, 0.10f, 0.001f, 0.10f, 0.0f, 0.02f, 0};

#define MAX_TRACKS 4
#define MAX_STEPS 160
enum { EV_HOLD = -1, EV_OFF = -2, EV_KICK = -10, EV_SNARE = -11, EV_HAT = -12 };

typedef struct {
    const char *notes;
    const Instr *instr;
} TrackDef;

typedef struct {
    float bpm;
    TrackDef tracks[MAX_TRACKS];
} SongDef;

// Bars are 8 steps of eighth notes. "." holds, "-" releases, K/S/H are drums.
static const SongDef SONGS[NUM_SONGS] = {
    // Day: a bright, bouncy farm tune in C major
    {112,
     {{"E5 . G5 . C6 . B5 A5 | G5 . . D5 G5 . A5 B5 | C6 . B5 A5 E5 . A5 . | F5 . . . A5 G5 F5 E5 |"
       "E5 . G5 . C6 . D6 E6 | D6 . . B5 G5 . A5 B5 | C6 . A5 F5 D5 . B4 D5 | C5 . . . - . G4 . |"
       "A5 . A5 C6 . A5 G5 F5 | G5 . . D5 . G5 A5 B5 | B5 . G5 E5 . G5 B5 . | C6 . . B5 A5 . E5 . |"
       "F5 . A5 C6 F6 . E6 D6 | D6 . B5 G5 . D5 G5 B5 | A5 . F5 D5 B5 . G5 D5 | C6 . . . . . - . |",
       &I_LEAD},
      {"C3 . . C4 C3 . G3 . | G2 . . G3 G2 . D3 . | A2 . . A3 A2 . E3 . | F2 . . F3 F2 . C3 . |"
       "C3 . . C4 C3 . G3 . | G2 . . G3 G2 . D3 . | F2 . F3 . G2 . G3 . | C3 . . C4 C3 . G3 . |"
       "F2 . . F3 F2 . C3 . | G2 . . G3 G2 . D3 . | E3 . . E4 E3 . B3 . | A2 . . A3 A2 . E3 . |"
       "F2 . . F3 F2 . C3 . | G2 . . G3 G2 . D3 . | D3 . D4 . G2 . G3 . | C3 . . C4 C3 . G3 . |",
       &I_BASS},
      {"C4 G4 E5 G4 C4 G4 E5 G4 | G3 D4 B4 D4 G3 D4 B4 D4 | A3 E4 C5 E4 A3 E4 C5 E4 | F3 C4 A4 C4 F3 C4 A4 C4 |"
       "C4 G4 E5 G4 C4 G4 E5 G4 | G3 D4 B4 D4 G3 D4 B4 D4 | F3 C4 A4 C4 G3 D4 B4 D4 | C4 G4 E5 G4 C4 G4 E5 G4 |"
       "F3 C4 A4 C4 F3 C4 A4 C4 | G3 D4 B4 D4 G3 D4 B4 D4 | E3 B3 G4 B3 E3 B3 G4 B3 | A3 E4 C5 E4 A3 E4 C5 E4 |"
       "F3 C4 A4 C4 F3 C4 A4 C4 | G3 D4 B4 D4 G3 D4 B4 D4 | D4 A4 F4 A4 G3 D4 B4 D4 | C4 G4 E5 G4 C4 G4 E5 G4 |",
       &I_ARP},
      {"K . H . S . H H |", &I_DRUM}}},
    // Night: slow, dreamy seventh chords
    {76,
     {{"C6 . . . A5 . . . | B5 . . . G5 . . . | A5 . F5 . D5 . . . | E5 . . . . . . - |"
       "C6 . . . E6 . D6 . | B5 . . . G5 . . . | A5 . C6 . F5 . . . | G5 . . . . . . - |",
       &I_NLEAD},
      {"F3 C4 E4 A4 C5 A4 E4 C4 | E3 B3 D4 G4 B4 G4 D4 B3 | D3 A3 C4 F4 A4 F4 C4 A3 | C3 G3 B3 E4 G4 E4 B3 G3 |",
       &I_NARP},
      {"F2 . . . . . . . | E2 . . . . . . . | D2 . . . . . . . | C2 . . . . . . . |", &I_NBASS},
      {NULL, NULL}}},
};

typedef struct {
    int16_t ev[MAX_STEPS];
    int len;
    const Instr *instr;
} Track;

typedef struct {
    Track tracks[MAX_TRACKS];
    float step_samples;
} Song;

static Song songs[NUM_SONGS];
static float note_freq[128];

static int parse_note(const char *s, int n)
{
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; // A..G
    if (n == 1 && s[0] == '.') return EV_HOLD;
    if (n == 1 && s[0] == '-') return EV_OFF;
    if (n == 1 && s[0] == 'K') return EV_KICK;
    if (n == 1 && s[0] == 'S') return EV_SNARE;
    if (n == 1 && s[0] == 'H') return EV_HAT;
    if (s[0] < 'A' || s[0] > 'G') return EV_HOLD;
    int semi = base[s[0] - 'A'], i = 1;
    if (s[i] == '#') semi++, i++;
    else if (s[i] == 'b') semi--, i++;
    int oct = s[i] - '0';
    return 12 * (oct + 1) + semi;
}

static void parse_track(Track *t, const TrackDef *d)
{
    t->len = 0;
    t->instr = d->instr;
    if (!d->notes) return;
    const char *p = d->notes;
    while (*p && t->len < MAX_STEPS) {
        while (*p == ' ' || *p == '|') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ' && *p != '|') p++;
        t->ev[t->len++] = (int16_t)parse_note(s, (int)(p - s));
    }
}

// ---------------------------------------------------------------------------
// Sound effects: up to 4 tone segments played back to back.

typedef struct {
    int wave;
    float duty, f0, f1, dur, vol;
} Seg;

typedef struct {
    Seg seg[4];
} SfxDef;

#define SQ(f0, f1, d, v) {W_SQUARE, 0.5f, f0, f1, d, v}
#define PU(f0, f1, d, v) {W_SQUARE, 0.25f, f0, f1, d, v}
#define TR(f0, f1, d, v) {W_TRI, 0.5f, f0, f1, d, v}
#define SN(f0, f1, d, v) {W_SINE, 0.5f, f0, f1, d, v}
#define NZ(f0, f1, d, v) {W_NOISE, 0.5f, f0, f1, d, v}

static const SfxDef SFX[NUM_SFX] = {
    [SFX_TILL] = {{NZ(2400, 500, 0.06f, 0.40f), TR(130, 55, 0.08f, 0.55f)}},
    [SFX_WATER] = {{NZ(9000, 4000, 0.10f, 0.16f), NZ(6000, 3000, 0.12f, 0.12f), SN(900, 1500, 0.05f, 0.10f)}},
    [SFX_CUT] = {{NZ(11000, 6000, 0.05f, 0.25f), NZ(7000, 3500, 0.05f, 0.18f)}},
    [SFX_BREAK] = {{NZ(1800, 500, 0.05f, 0.45f), SQ(220, 80, 0.07f, 0.22f), NZ(900, 300, 0.10f, 0.30f)}},
    [SFX_PLANT] = {{SN(380, 700, 0.05f, 0.30f), SN(620, 950, 0.06f, 0.22f)}},
    [SFX_HARVEST] = {{PU(660, 660, 0.05f, 0.16f), PU(880, 880, 0.05f, 0.16f), PU(1320, 1320, 0.10f, 0.16f)}},
    [SFX_COIN] = {{SQ(988, 988, 0.06f, 0.14f), SQ(1319, 1319, 0.22f, 0.14f)}},
    [SFX_MENU_MOVE] = {{PU(1200, 1200, 0.025f, 0.10f)}},
    [SFX_MENU_OK] = {{PU(880, 880, 0.04f, 0.13f), PU(1320, 1320, 0.07f, 0.13f)}},
    [SFX_MENU_BACK] = {{PU(660, 660, 0.04f, 0.13f), PU(440, 440, 0.07f, 0.13f)}},
    [SFX_ERROR] = {{SQ(160, 150, 0.08f, 0.14f), SQ(110, 100, 0.13f, 0.14f)}},
    [SFX_STEP] = {{NZ(1600, 700, 0.025f, 0.05f)}},
    [SFX_REFILL] = {{SN(300, 900, 0.08f, 0.25f), SN(500, 1100, 0.08f, 0.20f), SN(700, 1400, 0.09f, 0.15f)}},
    [SFX_EAT] = {{NZ(3000, 900, 0.05f, 0.28f), NZ(2600, 800, 0.05f, 0.22f), NZ(2200, 600, 0.07f, 0.18f)}},
    [SFX_QUEST] = {{PU(523, 523, 0.09f, 0.15f), PU(659, 659, 0.09f, 0.15f), PU(784, 784, 0.09f, 0.15f), PU(1047, 1047, 0.32f, 0.16f)}},
    [SFX_SLEEP] = {{SN(784, 784, 0.22f, 0.20f), SN(659, 659, 0.22f, 0.20f), SN(523, 523, 0.50f, 0.20f)}},
    [SFX_PLACE] = {{TR(320, 180, 0.06f, 0.45f), PU(700, 700, 0.04f, 0.10f)}},
};

typedef struct {
    int id, seg;
    float t, phase, noise;
    uint32_t lfsr;
} SfxVoice;

#define NUM_SFX_VOICES 4
static SfxVoice sfxv[NUM_SFX_VOICES];

static float sfx_sample(SfxVoice *s)
{
    const float dt = 1.0f / AUDIO_RATE;
    if (s->id < 0) return 0;
    const Seg *g = &SFX[s->id].seg[s->seg];
    if (s->seg >= 4 || g->dur <= 0) {
        s->id = -1;
        return 0;
    }
    float u = s->t / g->dur;
    float f = g->f0 + (g->f1 - g->f0) * u;
    float amp = g->vol * (1.0f - 0.6f * u);
    if (s->t < 0.002f) amp *= s->t / 0.002f;
    if (g->dur - s->t < 0.004f) amp *= (g->dur - s->t) / 0.004f;
    s->phase += f * dt;
    if (s->phase >= 1.0f) {
        s->phase -= (float)(int)s->phase;
        uint32_t bit = (s->lfsr ^ (s->lfsr >> 2) ^ (s->lfsr >> 3) ^ (s->lfsr >> 5)) & 1u;
        s->lfsr = (s->lfsr >> 1) | (bit << 15);
        s->noise = (s->lfsr & 1) ? 1.0f : -1.0f;
    }
    float out;
    switch (g->wave) {
    case W_SQUARE: out = s->phase < g->duty ? 1.0f : -1.0f; break;
    case W_TRI: out = 4.0f * fabsf(s->phase - 0.5f) - 1.0f; break;
    case W_SINE: out = sine_tab[(int)(s->phase * 256) & 255]; break;
    default: out = s->noise; break;
    }
    s->t += dt;
    if (s->t >= g->dur) {
        s->t = 0;
        s->seg++;
        if (s->seg >= 4 || SFX[s->id].seg[s->seg].dur <= 0) s->id = -1;
    }
    return out * amp;
}

// ---------------------------------------------------------------------------
// Mixer

static Voice mus_voice[MAX_TRACKS];
static int cur_song = MUS_NONE, next_song = MUS_NONE, music_on = 1;
static int step_idx;
static float step_acc, music_gain, lp;

void audio_init(void)
{
    for (int i = 0; i < 256; i++) sine_tab[i] = sinf(i * 6.2831853f / 256);
    for (int i = 0; i < 128; i++) note_freq[i] = 440.0f * powf(2.0f, (i - 69) / 12.0f);
    for (int s = 0; s < NUM_SONGS; s++) {
        songs[s].step_samples = AUDIO_RATE * 60.0f / SONGS[s].bpm / 2.0f;
        for (int t = 0; t < MAX_TRACKS; t++) parse_track(&songs[s].tracks[t], &SONGS[s].tracks[t]);
    }
    for (int i = 0; i < NUM_SFX_VOICES; i++) {
        sfxv[i].id = -1;
        sfxv[i].lfsr = 0xBEEFu + (uint32_t)i;
    }
}

void audio_music(int song)
{
    if (song == next_song) return;
    next_song = song;
}

void audio_set_music_enabled(int on)
{
    music_on = on;
}

void audio_sfx(int id)
{
    if (id < 0 || id >= NUM_SFX) return;
    // Reuse a voice already playing this effect, else a free one, else the oldest.
    int pick = -1;
    for (int i = 0; i < NUM_SFX_VOICES && pick < 0; i++)
        if (sfxv[i].id == id) pick = i;
    for (int i = 0; i < NUM_SFX_VOICES && pick < 0; i++)
        if (sfxv[i].id < 0) pick = i;
    if (pick < 0) {
        static int rr;
        pick = rr++ % NUM_SFX_VOICES;
    }
    sfxv[pick].id = id;
    sfxv[pick].seg = 0;
    sfxv[pick].t = 0;
}

static void trigger_step(void)
{
    Song *s = &songs[cur_song];
    for (int t = 0; t < MAX_TRACKS; t++) {
        Track *tr = &s->tracks[t];
        if (!tr->len) continue;
        int ev = tr->ev[step_idx % tr->len];
        Voice *v = &mus_voice[t];
        if (ev >= 0) voice_start(v, tr->instr, note_freq[ev], note_freq[ev], 0);
        else if (ev == EV_OFF) v->gate = 0;
        else if (ev == EV_KICK) {
            Instr k = {W_SINE, 0.5f, 0.42f, 0.001f, 0.14f, 0, 0.02f, 0};
            voice_start(v, &k, 150, 45, 0.12f);
        } else if (ev == EV_SNARE) {
            Instr sn = *tr->instr;
            sn.vol = 0.10f;
            sn.decay = 0.12f;
            voice_start(v, &sn, 5000, 3000, 0.1f);
        } else if (ev == EV_HAT) {
            Instr h = *tr->instr;
            h.vol = 0.045f;
            h.decay = 0.035f;
            voice_start(v, &h, 11000, 11000, 0);
        }
    }
}

void audio_render(int16_t *out, int samples)
{
    for (int i = 0; i < samples; i++) {
        // Fade out the old song before switching.
        if (next_song != cur_song) {
            music_gain -= 1.0f / (AUDIO_RATE * 0.4f);
            if (music_gain <= 0 || cur_song == MUS_NONE) {
                music_gain = 0;
                cur_song = next_song;
                step_idx = 0;
                step_acc = 0;
                memset(mus_voice, 0, sizeof(mus_voice));
                if (cur_song != MUS_NONE) trigger_step();
            }
        } else if (music_gain < 1.0f) {
            music_gain += 1.0f / (AUDIO_RATE * 0.4f);
            if (music_gain > 1.0f) music_gain = 1.0f;
        }

        float mus = 0;
        if (cur_song != MUS_NONE) {
            step_acc += 1.0f;
            if (step_acc >= songs[cur_song].step_samples) {
                step_acc -= songs[cur_song].step_samples;
                step_idx++;
                trigger_step();
            }
            for (int t = 0; t < MAX_TRACKS; t++) mus += voice_sample(&mus_voice[t]);
        }
        float mix = (music_on ? mus * music_gain : 0);
        for (int v = 0; v < NUM_SFX_VOICES; v++) mix += sfx_sample(&sfxv[v]);

        // Gentle low-pass to take the edge off the raw square waves.
        lp += (mix - lp) * 0.55f;
        float o = lp * 0.9f;
        if (o > 1.0f) o = 1.0f;
        if (o < -1.0f) o = -1.0f;
        out[i] = (int16_t)(o * 32000.0f);
    }
}
