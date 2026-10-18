// Tiny Harvest: walk between eight planter beds, plant, water and harvest.
#include "game.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_MAGIC 0x56524854u // "THRV"
#define SAVE_VERSION 1
#define FPS 60.0f
#define DAY_SECONDS 240.0f
#define WALK_SPEED 2.6f
#define ACT_FRAMES 20
#define START_COINS 10

typedef struct {
    const char *name;
    int seed, sell, grow, unlock;
} CropKind;

static const CropKind KINDS[NUM_CROP_KINDS] = {
    {"Turnip", 2, 6, 20, 0},       {"Carrot", 5, 15, 30, 40},     {"Strawberry", 10, 32, 40, 150},
    {"Tomato", 20, 64, 55, 400},   {"Pumpkin", 45, 140, 75, 1000}, {"Sunflower", 80, 300, 100, 2500},
};

// UI colours
#define C_INK RGB(0x3a2420)
#define C_INK_SOFT RGB(0x8a6040)
#define C_CREAM RGB(0xfff4e0)
#define C_PAPER RGB(0xf2e2c4)
#define C_OUT RGB(0x24151f)
#define C_GOLD RGB(0xffd84a)
#define C_GREEN RGB(0x4a9a3f)
#define C_RED RGB(0xc84a3e)

// ---------------------------------------------------------------------------
// Cosmetic state (not saved)

typedef struct {
    float x, y, vy;
    int img, life;
} Pop;

typedef struct {
    float x, y, speed;
    int w, seed;
} Cloud;

typedef struct {
    float x, y, vx, vy;
} Bug;

static Pop pops[8];
static Cloud clouds[6];
static Bug bugs[6];
static unsigned fx_seed = 12345;

static float frand(void)
{
    fx_seed = fx_seed * 1103515245u + 12345u;
    return ((fx_seed >> 8) & 0xFFFF) / 65535.0f;
}

static unsigned hash2(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void show_msg(Game *g, const char *m)
{
    snprintf(g->msg, sizeof(g->msg), "%s", m);
    g->msg_timer = 180;
}

static void spawn(Game *g, float x, float y, int n, u32 col, float speed, float grav, int life, int up)
{
    for (int k = 0; k < n; k++) {
        Particle *p = NULL;
        for (int i = 0; i < MAX_PARTICLES; i++)
            if (g->parts[i].life <= 0) {
                p = &g->parts[i];
                break;
            }
        if (!p) return;
        float a = frand() * 6.2832f, sp = speed * (0.35f + frand() * 0.65f);
        p->x = x + (frand() - 0.5f) * 8;
        p->y = y + (frand() - 0.5f) * 4;
        p->vx = cosf(a) * sp;
        p->vy = sinf(a) * sp * 0.5f - (up ? speed * 0.8f : 0);
        p->grav = grav;
        p->life = life + (int)(frand() * life * 0.5f);
        p->col = col;
        p->size = frand() < 0.35f ? 2 : 1;
        p->kind = 0;
    }
}

static void spawn_smoke(Game *g, float x, float y)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (g->parts[i].life <= 0) {
            Particle *p = &g->parts[i];
            p->x = x + (frand() - 0.5f) * 3;
            p->y = y;
            p->vx = 0.1f + g->wind * 0.25f + (frand() - 0.5f) * 0.1f;
            p->vy = -0.35f - frand() * 0.15f;
            p->grav = 0;
            p->life = 150;
            p->col = RGB(0xd8d4e0);
            p->size = 2 + (uint8_t)(frand() * 2);
            p->kind = 1;
            return;
        }
}

static void spawn_floater(Game *g, float x, float y, const char *text, u32 col)
{
    int best = 0;
    for (int i = 0; i < MAX_FLOATERS; i++)
        if (g->floats[i].life < g->floats[best].life) best = i;
    Floater *f = &g->floats[best];
    f->x = x;
    f->y = y;
    f->life = 80;
    f->col = col;
    snprintf(f->text, sizeof(f->text), "%.23s", text);
}

static void spawn_pop(float x, float y, int img)
{
    for (int i = 0; i < 8; i++)
        if (pops[i].life <= 0) {
            pops[i] = (Pop){x, y, -3.2f, img, 50};
            return;
        }
}

// ---------------------------------------------------------------------------
// Beds and the farmer

static int bed_x(int i) { return BED_X[i % BED_COLS]; }
static int bed_y(int i) { return BED_Y[i / BED_COLS]; }
static float stand_x(int i) { return bed_x(i) + 32.0f; }
static float stand_y(int i) { return bed_y(i) + 50.0f; }

static float stage_seconds(int kind)
{
    return KINDS[kind].grow / 4.0f;
}

static void set_target(Game *g, int t)
{
    if (t == g->target && g->path_len == 0) return;
    float tx = stand_x(t), ty = stand_y(t);
    g->path_len = 0;
    if (fabsf(ty - g->fy) > 4) {
        // Change rows through the nearest gap between beds.
        static const float gaps[5] = {40, 120, 200, 280, 360};
        float mid = (g->fx + tx) / 2, best = gaps[0];
        for (int i = 1; i < 5; i++)
            if (fabsf(gaps[i] - mid) < fabsf(best - mid)) best = gaps[i];
        g->path_x[g->path_len] = best, g->path_y[g->path_len++] = g->fy;
        g->path_x[g->path_len] = best, g->path_y[g->path_len++] = ty;
    }
    g->path_x[g->path_len] = tx, g->path_y[g->path_len++] = ty;
    g->target = t;
}

static int arrived(Game *g)
{
    return g->path_len == 0;
}

static void update_farmer(Game *g)
{
    if (g->path_len == 0) {
        g->idle++;
        if (g->act_timer > 0 || g->idle < 240) g->facing = 1; // look at the bed
        else g->facing = 0;                                    // then turn to the player
        return;
    }
    g->idle = 0;
    float dx = g->path_x[0] - g->fx, dy = g->path_y[0] - g->fy, d = sqrtf(dx * dx + dy * dy);
    if (d <= WALK_SPEED) {
        g->fx = g->path_x[0];
        g->fy = g->path_y[0];
        for (int i = 1; i < g->path_len; i++) g->path_x[i - 1] = g->path_x[i], g->path_y[i - 1] = g->path_y[i];
        g->path_len--;
        return;
    }
    g->fx += dx / d * WALK_SPEED;
    g->fy += dy / d * WALK_SPEED;
    g->facing = fabsf(dx) > fabsf(dy) ? (dx < 0 ? 2 : 3) : (dy < 0 ? 1 : 0);
    if (++g->walk_anim % 12 == 0) audio_sfx(SFX_STEP);
}

// ---------------------------------------------------------------------------
// Saving

static void save_game(Game *g)
{
    FILE *f = fopen(plat_save_path(), "wb");
    if (!f) return;
    fwrite(&g->s, sizeof(g->s), 1, f);
    fclose(f);
    g->dirty = 0;
    g->has_save = 1;
}

static int load_game(SaveData *out)
{
    FILE *f = fopen(plat_save_path(), "rb");
    if (!f) return 0;
    size_t ok = fread(out, sizeof(*out), 1, f);
    fclose(f);
    return ok == 1 && out->magic == SAVE_MAGIC && out->version == SAVE_VERSION && out->unlocked >= 1 &&
           out->unlocked <= NUM_CROP_KINDS;
}

static void new_save(SaveData *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->coins = START_COINS;
    s->unlocked = 1;
    s->music_on = 1;
    s->clock = 0.12f;
}

// ---------------------------------------------------------------------------
// Actions

enum { ACT_NONE, ACT_PLANT, ACT_WATER, ACT_HARVEST, ACT_WAIT, ACT_LOCKED, ACT_POOR };

static int bed_action(Game *g, int i)
{
    Bed *b = &g->s.beds[i];
    if (!b->kind) {
        if (g->s.sel >= g->s.unlocked) return ACT_LOCKED;
        return g->s.coins >= KINDS[g->s.sel].seed ? ACT_PLANT : ACT_POOR;
    }
    if (b->stage >= 4) return ACT_HARVEST;
    return b->watered ? ACT_WAIT : ACT_WATER;
}

static void do_action(Game *g)
{
    SaveData *s = &g->s;
    int i = g->target;
    Bed *b = &s->beds[i];
    float cx = bed_x(i) + 32, cy = bed_y(i) + 22;
    char buf[48];

    switch (bed_action(g, i)) {
    case ACT_PLANT:
        s->coins -= KINDS[s->sel].seed;
        b->kind = (uint8_t)(s->sel + 1);
        b->stage = 0;
        b->grow = 0;
        b->watered = g->rain > 0.5f;
        g->act_timer = ACT_FRAMES;
        g->act_tool = IMG_POUCH;
        spawn(g, cx, cy, 10, RGB(0xc8a070), 1.2f, 0.12f, 16, 1);
        audio_sfx(SFX_PLANT);
        g->dirty = 1;
        break;
    case ACT_WATER:
        b->watered = 1;
        g->act_timer = ACT_FRAMES;
        g->act_tool = IMG_CAN;
        for (int k = 0; k < 4; k++) spawn(g, cx - 18 + k * 12, cy - 6, 5, RGB(0x8ccaf8), 1.4f, 0.15f, 18, 1);
        audio_sfx(SFX_WATER);
        g->dirty = 1;
        break;
    case ACT_HARVEST: {
        const CropKind *k = &KINDS[b->kind - 1];
        int golden = frand() < 0.08f, earned = k->sell * (golden ? 5 : 1);
        s->coins += earned;
        s->harvested++;
        spawn_pop(cx - 16, cy - 20, IMG_ICON_0 + b->kind - 1);
        spawn(g, cx, cy, 14, golden ? C_GOLD : RGB(0x86c94a), 2.0f, 0.12f, 22, 1);
        snprintf(buf, sizeof(buf), golden ? "GOLDEN! +%dc" : "+%dc", earned);
        spawn_floater(g, cx, cy - 30, buf, C_GOLD);
        if (golden) show_msg(g, "A golden harvest! It's worth 5x!");
        b->kind = 0;
        b->stage = 0;
        b->watered = 0;
        g->act_timer = ACT_FRAMES;
        g->act_tool = IMG_BASKET;
        audio_sfx(SFX_HARVEST);
        audio_sfx(SFX_COIN);
        g->dirty = 1;
        if (s->unlocked < NUM_CROP_KINDS && s->coins >= KINDS[s->unlocked].unlock &&
            s->coins - earned < KINDS[s->unlocked].unlock) {
            snprintf(buf, sizeof(buf), "You can unlock %s seeds now!", KINDS[s->unlocked].name);
            show_msg(g, buf);
        }
        break;
    }
    case ACT_WAIT: {
        int left = (int)ceilf(KINDS[b->kind - 1].grow - b->grow);
        snprintf(buf, sizeof(buf), "Growing... ready in %ds", left);
        show_msg(g, buf);
        break;
    }
    case ACT_LOCKED:
        show_msg(g, "That seed is locked. Pick another with L/R.");
        audio_sfx(SFX_ERROR);
        break;
    case ACT_POOR:
        snprintf(buf, sizeof(buf), "Not enough coins for %s seeds.", KINDS[s->sel].name);
        show_msg(g, buf);
        audio_sfx(SFX_ERROR);
        break;
    }
}

static void try_unlock(Game *g, int kind)
{
    SaveData *s = &g->s;
    char buf[48];
    if (kind != s->unlocked) return; // unlock in order
    if (s->coins < KINDS[kind].unlock) {
        snprintf(buf, sizeof(buf), "%s needs %dc to unlock.", KINDS[kind].name, KINDS[kind].unlock);
        show_msg(g, buf);
        audio_sfx(SFX_ERROR);
        return;
    }
    s->coins -= KINDS[kind].unlock;
    s->unlocked++;
    s->sel = kind;
    snprintf(buf, sizeof(buf), "Unlocked %s seeds!", KINDS[kind].name);
    show_msg(g, buf);
    audio_sfx(SFX_QUEST);
    g->card_bounce = 30;
    g->dirty = 1;
}

static void select_seed(Game *g, int kind)
{
    if (kind < 0 || kind >= NUM_CROP_KINDS || kind > g->s.unlocked) return;
    if (kind != g->s.sel) audio_sfx(SFX_MENU_MOVE);
    g->s.sel = kind;
    g->card_bounce = 12;
}

// ---------------------------------------------------------------------------
// World simulation

static void update_world(Game *g)
{
    SaveData *s = &g->s;
    s->clock += 1.0f / (DAY_SECONDS * FPS);
    if (s->clock >= 1) s->clock -= 1;
    g->wind = 0.6f + 0.4f * sinf(g->frame * 0.004f) + 0.25f * sinf(g->frame * 0.013f);

    // Showers come and go; rain waters every planted bed.
    g->rain_timer -= 1.0f / FPS;
    if (g->rain_timer < 0) {
        float since = -g->rain_timer;
        g->rain = since < 3 ? since / 3 : since < 23 ? 1 : 1 - (since - 23) / 3;
        if (since > 26) {
            g->rain = 0;
            g->rain_timer = 100 + frand() * 80;
        }
        if (g->rain > 0.5f) {
            int any = 0;
            for (int i = 0; i < NUM_BEDS; i++)
                if (s->beds[i].kind && !s->beds[i].watered && s->beds[i].stage < 4) {
                    s->beds[i].watered = 1;
                    any = 1;
                }
            if (any) show_msg(g, "Rain! It waters your crops for you.");
        }
    }

    for (int i = 0; i < NUM_BEDS; i++) {
        Bed *b = &s->beds[i];
        if (!b->kind || !b->watered || b->stage >= 4) continue;
        b->grow += 1.0f / FPS;
        int stage = (int)(b->grow / stage_seconds(b->kind - 1));
        if (stage > 4) stage = 4;
        if (stage != b->stage) {
            b->stage = (uint8_t)stage;
            spawn(g, bed_x(i) + 32, bed_y(i) + 20, 6, stage == 4 ? C_GOLD : RGB(0xbce46a), 1.2f, 0.05f, 20, 1);
            if (stage == 4) g->dirty = 1;
        }
    }

    // Effects
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += p->grav;
        if (p->kind == 0) p->vx *= 0.97f;
    }
    for (int i = 0; i < MAX_FLOATERS; i++)
        if (g->floats[i].life > 0) {
            g->floats[i].life--;
            g->floats[i].y -= 0.4f;
        }
    for (int i = 0; i < 8; i++)
        if (pops[i].life > 0) {
            pops[i].life--;
            pops[i].y += pops[i].vy;
            pops[i].vy += 0.13f;
        }
    if (g->frame % 9 == 0) spawn_smoke(g, LIGHT_CHIMNEY_X, LIGHT_CHIMNEY_Y);

    for (int i = 0; i < 6; i++) {
        Cloud *c = &clouds[i];
        c->x += c->speed * (0.5f + g->wind);
        if (c->x > TOP_W + 60) c->x = -80 - frand() * 60;
    }
    for (int i = 0; i < 6; i++) {
        Bug *b = &bugs[i];
        b->vx += (frand() - 0.5f) * 0.25f;
        b->vy += (frand() - 0.5f) * 0.25f;
        b->vx *= 0.95f;
        b->vy *= 0.95f;
        b->x += b->vx;
        b->y += b->vy;
        if (b->x < 10 || b->x > TOP_W - 10) b->vx = -b->vx, b->x = b->x < 10 ? 10 : TOP_W - 10;
        if (b->y < 100 || b->y > 230) b->vy = -b->vy, b->y = b->y < 100 ? 100 : 230;
    }
}

// ---------------------------------------------------------------------------
// Update

static void update_play(Game *g, const Input *in)
{
    SaveData *s = &g->s;
    if (in->down & BTN_START) {
        g->mode = MODE_PAUSE;
        g->menu_sel = 0;
        audio_sfx(SFX_MENU_OK);
        return;
    }

    // Move between beds; holding a direction keeps walking.
    uint32_t dirs = in->held & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN);
    if (!dirs) g->repeat = 0;
    else if (g->repeat-- <= 0 || (in->down & dirs)) {
        int col = g->target % BED_COLS, row = g->target / BED_COLS;
        if (dirs & BTN_LEFT && col > 0) col--;
        if (dirs & BTN_RIGHT && col < BED_COLS - 1) col++;
        if (dirs & BTN_UP) row = 0;
        if (dirs & BTN_DOWN) row = NUM_BEDS / BED_COLS - 1;
        set_target(g, row * BED_COLS + col);
        g->repeat = 16;
    }

    int seeds = s->unlocked + (s->unlocked < NUM_CROP_KINDS); // unlocked + the next one
    if (in->down & BTN_L) select_seed(g, (s->sel + seeds - 1) % seeds);
    if (in->down & BTN_R) select_seed(g, (s->sel + 1) % seeds);
    if (in->down & BTN_X) try_unlock(g, s->unlocked);

    if (in->touch_down) {
        // Seed cards: 3 x 2 grid on the bottom screen.
        int col = (in->tx - 8) / 104, row = (in->ty - 42) / 86;
        if (in->tx >= 8 && in->ty >= 42 && col < 3 && row < 2 && (in->tx - 8) % 104 < 98) {
            int k = row * 3 + col;
            if (k == s->unlocked) try_unlock(g, k);
            else if (k < s->unlocked) select_seed(g, k);
            else {
                show_msg(g, "Unlock the seeds before this one first.");
                audio_sfx(SFX_ERROR);
            }
        }
    }

    if (in->down & BTN_A) g->pending = 1;
    if (g->pending && arrived(g) && g->act_timer == 0) {
        g->pending = 0;
        do_action(g);
    }
    if (g->act_timer > 0) g->act_timer--;
    update_farmer(g);

    if (g->dirty && ++g->save_timer > 8 * 60) {
        g->save_timer = 0;
        save_game(g);
    }
}

void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    SaveData tmp;
    g->has_save = load_game(&tmp);
    if (g->has_save) g->s = tmp;
    else new_save(&g->s);
    g->mode = MODE_TITLE;
    g->target = 1;
    g->fx = stand_x(1);
    g->fy = stand_y(1);
    g->facing = 0;
    g->idle = 1000;
    g->rain_timer = 70;
    for (int i = 0; i < 6; i++) {
        clouds[i] = (Cloud){frand() * TOP_W, 8 + frand() * 50, 0.08f + frand() * 0.12f, 30 + (int)(frand() * 40), i * 7 + 3};
        bugs[i] = (Bug){40 + frand() * 320, 110 + frand() * 110, 0, 0};
    }
    audio_set_music_enabled(g->s.music_on);
}

void game_shutdown(Game *g)
{
    if (g->mode != MODE_TITLE || g->dirty) save_game(g);
}

void game_update(Game *g, const Input *in)
{
    g->frame++;
    if (g->msg_timer > 0) g->msg_timer--;
    if (g->card_bounce > 0) g->card_bounce--;
    audio_music(g->s.clock > 0.68f && g->s.clock < 0.97f ? MUS_NIGHT : MUS_DAY);
    audio_set_music_enabled(g->s.music_on);
    update_world(g);

    switch (g->mode) {
    case MODE_TITLE:
        if ((in->down & (BTN_A | BTN_START)) || in->touch_down) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_OK);
            show_msg(g, g->has_save ? "Welcome back to your garden!" : "Walk to a bed and press A to plant!");
        }
        break;
    case MODE_PLAY: update_play(g, in); break;
    case MODE_PAUSE: {
        int pick = -1;
        if (in->down & BTN_UP) g->menu_sel = (g->menu_sel + 2) % 3, audio_sfx(SFX_MENU_MOVE);
        if (in->down & BTN_DOWN) g->menu_sel = (g->menu_sel + 1) % 3, audio_sfx(SFX_MENU_MOVE);
        if (in->down & BTN_A) pick = g->menu_sel;
        if (in->down & (BTN_B | BTN_START)) pick = 0;
        if (in->touch_down && in->tx >= 60 && in->tx < 260)
            for (int i = 0; i < 3; i++)
                if (in->ty >= 70 + i * 44 && in->ty < 104 + i * 44) pick = i;
        if (pick == 0) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
        } else if (pick == 1) {
            g->s.music_on = !g->s.music_on;
            g->dirty = 1;
            audio_sfx(SFX_MENU_OK);
        } else if (pick == 2) {
            save_game(g);
            g->quit = 1;
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Lighting

typedef struct {
    float t;
    u32 sky_top, sky_bottom;
    float r, gr, b, dark;
} Key;

static const Key KEYS[] = {
    {0.00f, 0x2c3470, 0xf0a070, 0.95f, 0.80f, 0.72f, 0.35f},
    {0.08f, 0x5aa0e8, 0xc4e8ff, 1.00f, 1.00f, 1.00f, 0.00f},
    {0.55f, 0x4a90e0, 0xa8d8ff, 1.00f, 1.00f, 0.98f, 0.00f},
    {0.64f, 0x4a4090, 0xffa06a, 1.00f, 0.80f, 0.68f, 0.15f},
    {0.71f, 0x1c1a50, 0x7a4a8a, 0.55f, 0.50f, 0.74f, 0.65f},
    {0.78f, 0x080a24, 0x1c2450, 0.36f, 0.40f, 0.66f, 1.00f},
    {0.95f, 0x080a24, 0x1c2450, 0.36f, 0.40f, 0.66f, 1.00f},
    {1.00f, 0x2c3470, 0xf0a070, 0.95f, 0.80f, 0.72f, 0.35f},
};

static u32 lerp_rgb(u32 a, u32 b, float t)
{
    int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
    int gg = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
    int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
    return RGB((u32)r << 16 | (u32)gg << 8 | (u32)bl);
}

static void current_light(Game *g, u32 *top, u32 *bottom, float *tr, float *tg, float *tb)
{
    float t = g->s.clock;
    int k = 0;
    while (k < 6 && KEYS[k + 1].t < t) k++;
    const Key *a = &KEYS[k], *b = &KEYS[k + 1];
    float u = (t - a->t) / (b->t - a->t);
    *top = lerp_rgb(a->sky_top, b->sky_top, u);
    *bottom = lerp_rgb(a->sky_bottom, b->sky_bottom, u);
    *tr = a->r + (b->r - a->r) * u;
    *tg = a->gr + (b->gr - a->gr) * u;
    *tb = a->b + (b->b - a->b) * u;
    g->dark = a->dark + (b->dark - a->dark) * u;
    if (g->rain > 0) {
        float r = g->rain;
        *top = lerp_rgb(*top, 0x5a6478, r * 0.75f);
        *bottom = lerp_rgb(*bottom, 0x8a94a8, r * 0.75f);
        *tr *= 1 - 0.3f * r;
        *tg *= 1 - 0.25f * r;
        *tb *= 1 - 0.1f * r;
    }
}

static u32 tint(u32 c, float r, float gg, float b)
{
    int cr = (int)(((c >> 16) & 255) * r), cg = (int)(((c >> 8) & 255) * gg), cb = (int)((c & 255) * b);
    return RGB((u32)(cr > 255 ? 255 : cr) << 16 | (u32)(cg > 255 ? 255 : cg) << 8 | (u32)(cb > 255 ? 255 : cb));
}

// ---------------------------------------------------------------------------
// Rendering: top screen

static void draw_cloud(Canvas *c, const Cloud *cl, u32 lit, u32 shadow)
{
    unsigned h = hash2(cl->seed, 9);
    int x = (int)cl->x, y = (int)cl->y, w = cl->w;
    for (int pass = 0; pass < 2; pass++) {
        u32 col = pass ? lit : shadow;
        int oy = pass ? -1 : 1;
        for (int i = 0; i < 5; i++) {
            int r = 5 + (int)((h >> (i * 3)) % 6);
            int px = x + i * w / 5, py = y - (i == 2 || i == 1 ? r / 2 : 0);
            cv_disc(c, px, py + oy, r, col);
        }
        cv_rect(c, x, y + oy, w, 5, col);
    }
}

static void draw_sky(Game *g, Canvas *c, float tr, float tg, float tb, u32 top, u32 bottom)
{
    for (int y = 0; y < 110; y++) cv_rect(c, 0, y, TOP_W, 1, lerp_rgb(top, bottom, y / 110.0f));
    // stars
    if (g->dark > 0.05f)
        for (int i = 0; i < 70; i++) {
            unsigned h = hash2(i, 77);
            int x = (int)(h % TOP_W), y = (int)((h >> 9) % 90);
            int tw = (int)(160 + 96 * sinf(g->frame * 0.05f + i));
            cv_pixel_alpha(c, x, y, RGB(0xfff6d8), (int)(g->dark * tw * (1 - g->rain)));
            if (i % 9 == 0) cv_pixel_alpha(c, x + 1, y, RGB(0xfff6d8), (int)(g->dark * tw * 0.5f));
        }
    // sun by day, moon by night, both arcing across the sky
    float t = g->s.clock;
    if (g->rain > 0.6f) {
        // hidden behind the rain clouds
    } else if (t > 0.0f && t < 0.68f) {
        float u = t / 0.68f;
        int x = (int)(30 + u * 340), y = (int)(70 - sinf(u * 3.1416f) * 58);
        cv_glow(c, x, y, 34, RGB(0xffd890), (int)(140 * (1 - g->rain)));
        cv_disc(c, x, y, 9, tint(RGB(0xffe8a0), 1, tg > 0.9f ? 1 : 0.9f, tb > 0.9f ? 1 : 0.75f));
        cv_disc(c, x - 2, y - 2, 5, RGB(0xfff8e0));
    } else {
        float u = (t < 0.68f ? t + 1 : t) - 0.68f;
        u /= 0.34f;
        int x = (int)(30 + u * 340), y = (int)(70 - sinf(u * 3.1416f) * 50);
        cv_glow(c, x, y, 26, RGB(0x8090c0), 90);
        cv_disc(c, x, y, 7, RGB(0xf0f0e0));
        cv_disc(c, x + 3, y - 2, 6, lerp_rgb(top, bottom, y / 110.0f));
    }
    u32 lit = tint(RGB(0xffffff), tr, tg, tb), shadow = tint(RGB(0xc8d0e8), tr, tg, tb);
    if (g->rain > 0) {
        lit = lerp_rgb(lit, 0x9aa0b0, g->rain);
        shadow = lerp_rgb(shadow, 0x707890, g->rain);
    }
    for (int i = 0; i < 6; i++) draw_cloud(c, &clouds[i], lit, shadow);
}

static void draw_windmill_blades(Game *g, Canvas *c, float tr, float tg, float tb)
{
    float a0 = g->frame * 0.012f * (0.5f + g->wind);
    u32 spar = tint(RGB(0x73422e), tr, tg, tb), sail = tint(RGB(0xecdcc0), tr, tg, tb);
    u32 lattice = tint(RGB(0x99603a), tr, tg, tb), out = RGB(0x24151f);
    float hx = LIGHT_HUB_X + 0.5f, hy = LIGHT_HUB_Y + 0.5f;
    for (int k = 0; k < 4; k++) {
        float a = a0 + k * 1.5708f, ux = cosf(a), uy = sinf(a), vx = -uy, vy = ux;
        float xs[4], ys[4];
        // sail
        xs[0] = hx + ux * 12, ys[0] = hy + uy * 12;
        xs[1] = hx + ux * 42, ys[1] = hy + uy * 42;
        xs[2] = hx + ux * 42 + vx * 9, ys[2] = hy + uy * 42 + vy * 9;
        xs[3] = hx + ux * 12 + vx * 8, ys[3] = hy + uy * 12 + vy * 8;
        cv_quad(c, xs, ys, sail);
        for (int r = 16; r <= 40; r += 6)
            cv_line(c, (int)(hx + ux * r), (int)(hy + uy * r), (int)(hx + ux * r + vx * 8), (int)(hy + uy * r + vy * 8), lattice);
        cv_line(c, (int)xs[1], (int)ys[1], (int)xs[2], (int)ys[2], out);
        cv_line(c, (int)xs[2], (int)ys[2], (int)xs[3], (int)ys[3], out);
        // spar
        xs[0] = hx - vx * 1.2f, ys[0] = hy - vy * 1.2f;
        xs[1] = hx + ux * 44 - vx * 1.2f, ys[1] = hy + uy * 44 - vy * 1.2f;
        xs[2] = hx + ux * 44 + vx * 1.2f, ys[2] = hy + uy * 44 + vy * 1.2f;
        xs[3] = hx + vx * 1.2f, ys[3] = hy + vy * 1.2f;
        cv_quad(c, xs, ys, spar);
    }
    cv_disc(c, LIGHT_HUB_X, LIGHT_HUB_Y, 3, out);
    cv_disc(c, LIGHT_HUB_X, LIGHT_HUB_Y, 2, spar);
}

static void draw_a_button(Canvas *c, int x, int y, u32 fill)
{
    cv_disc(c, x + 5, y + 5, 6, C_OUT);
    cv_disc(c, x + 5, y + 5, 5, fill);
    cv_text(c, x + 3, y + 1, "A", C_CREAM, 1);
}

static void draw_bubble(Canvas *c, int cx, int y, const char *text, int warn)
{
    int w = text_width(text, 1) + 22, x = cx - w / 2;
    if (x < 2) x = 2;
    if (x + w > TOP_W - 2) x = TOP_W - 2 - w;
    cv_rect(c, x + 1, y, w - 2, 15, C_OUT);
    cv_rect(c, x, y + 1, w, 13, C_OUT);
    cv_rect(c, x + 1, y + 1, w - 2, 13, warn ? RGB(0xf6d6c8) : C_CREAM);
    cv_rect(c, x + 1, y + 12, w - 2, 2, warn ? RGB(0xe0b0a0) : C_PAPER);
    // little tail pointing at the bed
    cv_rect(c, cx - 2, y + 15, 5, 1, C_OUT);
    cv_rect(c, cx - 1, y + 16, 3, 1, C_OUT);
    cv_rect(c, cx - 1, y + 15, 3, 1, warn ? RGB(0xe0b0a0) : C_PAPER);
    draw_a_button(c, x + 3, y + 2, warn ? C_RED : C_GREEN);
    cv_text(c, x + 17, y + 3, text, warn ? RGB(0x9a3020) : C_INK, 1);
}

static void draw_bed(Game *g, Canvas *c, int i)
{
    const Bed *b = &g->s.beds[i];
    int bx = bed_x(i), by = bed_y(i);
    cv_rect_alpha(c, bx + 2, by + 42, 62, 4, RGB(0x000000), 70); // shadow
    cv_art(c, b->watered && b->kind ? IMG_PLANTER_WET : IMG_PLANTER_DRY, bx, by, 0, g->pal);
    if (!b->kind) return;
    int img = IMG_CROP(b->kind - 1, b->stage), h = art_h(img);
    // two staggered rows: two plants at the back, three at the front
    static const int px[5] = {10, 32, 0, 21, 42}, py[5] = {24, 24, 35, 35, 35};
    for (int k = 0; k < 5; k++) {
        unsigned hh = hash2(i, k);
        float sway = g->wind * (b->stage >= 2 ? 2.2f : 0.8f) * sinf(g->frame * 0.05f + (hh % 100) * 0.06f + k * 1.3f);
        int x = bx + px[k], y = by + py[k] - h + (int)(hh % 2);
        if (b->stage == 4 && b->kind - 1 != 5) y -= ((g->frame + (int)(hh % 40)) / 30) % 2; // ripe bob
        cv_art_sway(c, img, x, y, sway, g->pal);
    }
    if (b->stage == 4) { // twinkles on ripe crops
        unsigned hh = hash2(i, g->frame / 20);
        if ((g->frame / 5) % 4 == 0) cv_art(c, IMG_STAR, bx + 8 + (int)(hh % 48), by + 2 + (int)((hh >> 8) % 20), 0, g->pal);
    } else if (!b->watered) {
        int bob = (int)(2 * sinf(g->frame * 0.12f + i));
        cv_art(c, IMG_DROP, bx + 54, by - 8 + bob, 0, g->pal);
    }
}

static void draw_farmer(Game *g, Canvas *c)
{
    int moving = g->path_len > 0, step = moving && (g->walk_anim / 8) % 2;
    int img, flip = 0;
    switch (g->facing) {
    case 1: img = IMG_FARMER_BACK0 + step; break;
    case 2: img = IMG_FARMER_SIDE0 + step, flip = 1; break;
    case 3: img = IMG_FARMER_SIDE0 + step; break;
    default: img = IMG_FARMER_FRONT0 + step; break;
    }
    if (moving && g->facing < 2 && (g->walk_anim / 16) % 2) flip = 1; // alternate legs
    int x = (int)g->fx - 9, y = (int)g->fy - 25;
    int bob = g->act_timer > 0 ? -(g->act_timer > ACT_FRAMES / 2) : 0;
    cv_rect_alpha(c, x + 3, y + 23, 12, 3, RGB(0x000000), 80);
    cv_art(c, img, x, y + bob, flip, g->pal);
    if (g->act_timer > 0) { // tool held up over the bed
        int lift = g->act_timer > ACT_FRAMES / 2 ? 4 : 0;
        cv_art(c, g->act_tool, x + 10, y - 2 - lift, 0, g->pal);
    }
}

static void draw_lights(Game *g, Canvas *c)
{
    float d = g->dark;
    if (d <= 0.02f) return;
    int s = (int)(d * 220);
    u32 warm = RGB(0xffa040);
    // windows light up
    for (int w = 0; w < 2; w++) {
        int x = w ? LIGHT_WINDOW1_X : LIGHT_WINDOW0_X, y = w ? LIGHT_WINDOW1_Y : LIGHT_WINDOW0_Y;
        for (int pane = 0; pane < 4; pane++) {
            int px = x - 9 + (pane & 1) * 10, py = y - 9 + (pane >> 1) * 10;
            cv_rect_alpha(c, px, py, 8, 8, RGB(0xffc860), (int)(d * 230));
        }
        cv_glow(c, x, y, 30, warm, s);
    }
    cv_rect_alpha(c, LIGHT_MILL_X - 3, LIGHT_MILL_Y - 4, 6, 8, RGB(0xffc860), (int)(d * 230));
    cv_glow(c, LIGHT_MILL_X, LIGHT_MILL_Y, 18, warm, s);
    cv_glow(c, LIGHT_LANTERN_X, LIGHT_LANTERN_Y, 22, warm, s);
    cv_glow(c, LIGHT_LAMP_X, LIGHT_LAMP_Y, 64, RGB(0xffb050), (int)(d * 170));
    cv_glow(c, LIGHT_LAMP_X, LIGHT_LAMP_Y, 10, RGB(0xfff0c0), s);
}

static void render_world(Game *g, Canvas *c, int show_ui)
{
    u32 top, bottom;
    float tr, tg, tb;
    current_light(g, &top, &bottom, &tr, &tg, &tb);
    for (int i = 1; i < 256; i++) g->pal[i] = tint(art_palette[i], tr, tg, tb);

    draw_sky(g, c, tr, tg, tb, top, bottom);
    cv_art(c, IMG_SCENE, 0, 0, 0, g->pal);
    draw_windmill_blades(g, c, tr, tg, tb);

    // chimney smoke
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->parts[i];
        if (p->life <= 0 || p->kind != 1) continue;
        int a = p->life * 160 / 150, r = p->size + (150 - p->life) / 30;
        u32 col = tint(p->col, tr, tg, tb);
        for (int y = -r; y <= r; y++)
            for (int x = -r; x <= r; x++)
                if (x * x + y * y <= r * r) cv_pixel_alpha(c, (int)p->x + x, (int)p->y + y, col, a);
    }

    // beds and farmer, back to front
    int farmer_row = g->fy > stand_y(BED_COLS) - 20 ? 1 : 0;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < BED_COLS; col++) draw_bed(g, c, row * BED_COLS + col);
        if (row == farmer_row) draw_farmer(g, c);
    }

    // particles and harvest pops
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->parts[i];
        if (p->life > 0 && p->kind == 0) cv_rect(c, (int)p->x, (int)p->y, p->size, p->size, p->col);
    }
    for (int i = 0; i < 8; i++)
        if (pops[i].life > 0) cv_art(c, pops[i].img, (int)pops[i].x, (int)pops[i].y, 0, g->pal);

    // butterflies by day, fireflies by night
    for (int i = 0; i < 6; i++) {
        int x = (int)bugs[i].x, y = (int)bugs[i].y;
        if (g->dark < 0.4f && g->rain < 0.3f && i < 3) {
            static const u32 wings[3] = {0xfff4e0, 0xffd84a, 0xf888b0};
            int flap = (g->frame / 4 + i) % 2;
            cv_rect(c, x, y, 1, 3, C_OUT);
            cv_rect(c, x - 2, y - flap, 2, 2, tint(RGB(wings[i]), tr, tg, tb));
            cv_rect(c, x + 1, y - flap, 2, 2, tint(RGB(wings[i]), tr, tg, tb));
        }
    }

    // rain
    if (g->rain > 0) {
        int n = (int)(170 * g->rain);
        for (int i = 0; i < n; i++) {
            int x = (int)((hash2(i, 7) % 440) + g->frame * 3) % 440 - 20;
            int y = (int)((hash2(i, 3) % 260) + g->frame * 8) % 260 - 10;
            cv_pixel_alpha(c, x + 1, y - 2, RGB(0xd8e8ff), 120);
            cv_pixel_alpha(c, x, y, RGB(0xd8e8ff), 200);
            cv_pixel_alpha(c, x, y + 1, RGB(0xd8e8ff), 190);
            cv_pixel_alpha(c, x - 1, y + 3, RGB(0xd8e8ff), 150);
            cv_pixel_alpha(c, x - 1, y + 4, RGB(0xd8e8ff), 90);
            if (y > 120 && (i + g->frame / 4) % 7 == 0) {
                cv_pixel_alpha(c, x - 2, y + 5, RGB(0xe0ecff), 120);
                cv_pixel_alpha(c, x + 1, y + 5, RGB(0xe0ecff), 120);
            }
        }
    }

    draw_lights(g, c);
    if (g->dark > 0.3f)
        for (int i = 0; i < 6; i++) {
            int on = (g->frame / 7 + i * 5) % 14 < 9;
            if (!on || g->rain > 0.3f) continue;
            cv_glow(c, (int)bugs[i].x, (int)bugs[i].y, 6, RGB(0xc0ff60), (int)(g->dark * 200));
            cv_pixel(c, (int)bugs[i].x, (int)bugs[i].y, RGB(0xf0ffc0));
        }

    // floating text
    for (int i = 0; i < MAX_FLOATERS; i++) {
        Floater *f = &g->floats[i];
        if (f->life > 0) cv_text_outline(c, (int)f->x - text_width(f->text, 1) / 2, (int)f->y, f->text, f->col, C_OUT, 1);
    }

    if (!show_ui) return;

    // what A will do here
    if (g->act_timer == 0) {
        int i = g->target, act = bed_action(g, i);
        const Bed *b = &g->s.beds[i];
        char buf[48];
        int warn = act == ACT_LOCKED || act == ACT_POOR;
        switch (act) {
        case ACT_PLANT: snprintf(buf, sizeof(buf), "Plant %s  %dc", KINDS[g->s.sel].name, KINDS[g->s.sel].seed); break;
        case ACT_WATER: snprintf(buf, sizeof(buf), "Water"); break;
        case ACT_HARVEST: snprintf(buf, sizeof(buf), "Harvest  +%dc", KINDS[b->kind - 1].sell); break;
        case ACT_WAIT: snprintf(buf, sizeof(buf), "Growing  %d%%", (int)(100 * b->grow / KINDS[b->kind - 1].grow)); break;
        case ACT_LOCKED: snprintf(buf, sizeof(buf), "Seed locked"); break;
        default: snprintf(buf, sizeof(buf), "Need %dc", KINDS[g->s.sel].seed); break;
        }
        int bob = (g->frame / 20) % 2;
        draw_bubble(c, bed_x(i) + 32, bed_y(i) - 20 - bob, buf, warn);
    }

    // coins pill (top left)
    char coins[16];
    snprintf(coins, sizeof(coins), "%d", g->s.coins);
    int w = text_width(coins, 1) + 26;
    cv_rect(c, 4, 4, w, 18, C_OUT);
    cv_rect(c, 5, 5, w - 2, 16, C_CREAM);
    cv_rect(c, 5, 18, w - 2, 3, C_PAPER);
    cv_art(c, IMG_COIN, 7, 7, 0, art_palette);
    cv_text(c, 23, 9, coins, C_INK, 1);

    if (g->msg_timer > 0) {
        int mw = text_width(g->msg, 1) + 16, mx = (TOP_W - mw) / 2;
        int y = SCREEN_H - 22 + (g->msg_timer > 170 ? (g->msg_timer - 170) * 2 : 0);
        cv_rect(c, mx + 1, y, mw - 2, 18, C_OUT);
        cv_rect(c, mx, y + 1, mw, 16, C_OUT);
        cv_rect(c, mx + 1, y + 1, mw - 2, 16, C_CREAM);
        cv_rect(c, mx + 1, y + 14, mw - 2, 3, C_PAPER);
        cv_text(c, mx + 8, y + 5, g->msg, C_INK, 1);
    }
}

// ---------------------------------------------------------------------------
// Rendering: bottom screen

static void draw_card(Game *g, Canvas *c, int k)
{
    const SaveData *s = &g->s;
    int x = 8 + (k % 3) * 104, y = 42 + (k / 3) * 86;
    int locked = k >= s->unlocked, next = k == s->unlocked, sel = k == s->sel;
    char buf[32];
    if (sel) { // glowing gold frame
        int pulse = (int)(3.5f + 1.5f * sinf(g->frame * 0.1f));
        cv_rect(c, x - pulse - 1, y - pulse - 1, 100 + pulse * 2, 82 + pulse * 2, RGB(0x6a4a14));
        cv_rect(c, x - pulse, y - pulse, 98 + pulse * 2, 80 + pulse * 2, RGB(0xffd84a));
        cv_rect(c, x - 2, y - 2, 102, 84, RGB(0xfff4a8));
    }
    cv_art(c, IMG_CARD, x, y, 0, art_palette);
    int bounce = sel && g->card_bounce > 0 ? -(int)(3 * sinf(g->card_bounce * 0.35f)) : 0;
    if (locked) {
        cv_art_mask(c, IMG_ICON_0 + k, x + 33, y + 8, RGB(0x5e5060), 256);
        cv_rect_alpha(c, x + 2, y + 2, 94, 76, RGB(0x3a2a30), 70);
        cv_art(c, IMG_LOCK, x + 43, y + 17, 0, art_palette);
        cv_text_center(c, x + 49, y + 44, KINDS[k].name, RGB(0x5e5060), 1);
        if (next) {
            int afford = s->coins >= KINDS[k].unlock;
            snprintf(buf, sizeof(buf), "Unlock %dc", KINDS[k].unlock);
            cv_text_center(c, x + 49, y + 58, buf, afford ? RGB(0x2f7a3f) : RGB(0x9a3020), 1);
            if (afford && (g->frame / 20) % 2) cv_text_center(c, x + 49, y + 68, "Tap or X!", C_INK, 1);
        } else cv_text_center(c, x + 49, y + 58, "Locked", RGB(0x5e5060), 1);
        return;
    }
    cv_art(c, IMG_ICON_0 + k, x + 33, y + 6 + bounce, 0, art_palette);
    cv_text_center(c, x + 49, y + 42, KINDS[k].name, C_INK, 1);
    snprintf(buf, sizeof(buf), "Seed %dc", KINDS[k].seed);
    cv_text_center(c, x + 49, y + 54, buf, C_INK_SOFT, 1);
    snprintf(buf, sizeof(buf), "Sells %dc  %ds", KINDS[k].sell, KINDS[k].grow);
    cv_text_center(c, x + 49, y + 65, buf, RGB(0x2f7a3f), 1);
}

static void render_bottom(Game *g, Canvas *c)
{
    const SaveData *s = &g->s;
    char buf[64];
    cv_art(c, IMG_BOARD, 0, 0, 0, art_palette);
    cv_art(c, IMG_HEADER, 8, 4, 0, art_palette);
    cv_art(c, IMG_COIN_BIG, 16, 12, 0, art_palette);
    snprintf(buf, sizeof(buf), "%d", s->coins);
    cv_text_shadow(c, 38, 11, buf, C_INK, RGB(0xd8c0a4), 2);
    snprintf(buf, sizeof(buf), "Harvested: %d", s->harvested);
    cv_text(c, 300 - text_width(buf, 1), 15, buf, C_INK_SOFT, 1);
    for (int k = 0; k < NUM_CROP_KINDS; k++) draw_card(g, c, k);

    cv_art(c, IMG_FOOTER, 8, 211, 0, art_palette);
    const Bed *b = &s->beds[g->target];
    if (g->mode == MODE_PLAY && b->kind && b->stage < 4 && b->watered)
        snprintf(buf, sizeof(buf), "%s: ready in %ds", KINDS[b->kind - 1].name, (int)ceilf(KINDS[b->kind - 1].grow - b->grow));
    else snprintf(buf, sizeof(buf), "A: Plant/Water/Harvest   L/R: Seed   +: Walk");
    cv_text_center(c, BOT_W / 2, 220, buf, C_INK, 1);
}

static void render_pause(Game *g, Canvas *top, Canvas *bot)
{
    static const char *opts[3] = {"Resume", NULL, "Save & Quit"};
    cv_rect_alpha(top, 0, 0, TOP_W, SCREEN_H, RGB(0x10081a), 120);
    cv_text_outline(top, (TOP_W - text_width("Paused", 3)) / 2, 90, "Paused", C_CREAM, C_OUT, 3);
    cv_art(bot, IMG_BOARD, 0, 0, 0, art_palette);
    for (int i = 0; i < 3; i++) {
        const char *t = i == 1 ? (g->s.music_on ? "Music: On" : "Music: Off") : opts[i];
        int y = 70 + i * 44;
        if (i == g->menu_sel) cv_rect(bot, 56, y - 4, 208, 42, C_GOLD);
        cv_rect(bot, 60, y, 200, 34, C_OUT);
        cv_rect(bot, 61, y + 1, 198, 32, C_CREAM);
        cv_rect(bot, 61, y + 27, 198, 6, C_PAPER);
        cv_text_center(bot, BOT_W / 2, y + 9, t, C_INK, 2);
    }
}

static void render_title(Game *g, Canvas *top, Canvas *bot)
{
    render_world(g, top, 0);
    int lw = art_w(IMG_LOGO), bob = (int)(3 * sinf(g->frame * 0.05f));
    cv_art(top, IMG_LOGO, (TOP_W - lw) / 2, 26 + bob, 0, art_palette);
    if ((g->frame / 30) % 2) cv_text_outline(top, (TOP_W - text_width("Press A to start", 1)) / 2, 92, "Press A to start", C_CREAM, C_OUT, 1);

    cv_art(bot, IMG_BOARD, 0, 0, 0, art_palette);
    cv_art(bot, IMG_HEADER, 8, 20, 0, art_palette);
    cv_text_center(bot, BOT_W / 2, 31, "A cozy little garden", C_INK, 1);
    for (int k = 0; k < NUM_CROP_KINDS; k++) {
        int x = 22 + k * 48, y = 84 + (int)(4 * sinf(g->frame * 0.06f + k));
        cv_art(bot, IMG_ICON_0 + k, x, y, 0, art_palette);
    }
    int w = 200, x = (BOT_W - w) / 2, y = 150;
    cv_rect(bot, x - 3, y - 3, w + 6, 46, C_GOLD);
    cv_rect(bot, x, y, w, 40, C_OUT);
    cv_rect(bot, x + 1, y + 1, w - 2, 38, C_CREAM);
    cv_rect(bot, x + 1, y + 31, w - 2, 8, C_PAPER);
    cv_text_center(bot, BOT_W / 2, y + 11, g->has_save ? "Continue" : "Start", C_INK, 2);
    cv_text_center(bot, BOT_W / 2, 214, "Plant, water, harvest - that's it!", C_CREAM, 1);
}

void game_render(Game *g, Canvas *top, Canvas *bot)
{
    if (g->mode == MODE_TITLE) {
        render_title(g, top, bot);
        return;
    }
    render_world(g, top, g->mode == MODE_PLAY);
    if (g->mode == MODE_PAUSE) render_pause(g, top, bot);
    else render_bottom(g, bot);
}
