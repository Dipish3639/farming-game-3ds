#include "game.h"
#include "audio.h"
#include "sprites.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_MAGIC 0x4D524146u // "FARM"
#define SAVE_VERSION 2

#define FRAMES_PER_MIN 30 // 10 in-game minutes every 5 seconds
#define START_MIN (6 * 60)
#define END_MIN (26 * 60) // 2:00 AM
#define NIGHT_MIN (18 * 60)
#define MAX_ENERGY 150
#define DAYS_PER_SEASON 28
#define WALK_SPEED 2.0f
#define RUN_SPEED 3.1f
#define SWING_FRAMES 14

#define HOUSE_X 4
#define HOUSE_Y 2
#define HOUSE_W 5
#define HOUSE_H 4
#define DOOR_X (HOUSE_X + 2)
#define DOOR_Y (HOUSE_Y + HOUSE_H - 1)
#define BIN_X 10
#define BIN_Y 5
#define SHOP_X 14
#define SHOP_Y 4

// Palette for UI
#define C_INK RGB(0x3a2414)
#define C_INK_SOFT RGB(0x8a5a30)
#define C_PAPER RGB(0xf6e7c1)
#define C_PAPER2 RGB(0xe9cf98)
#define C_WOOD RGB(0x8a5430)
#define C_WOOD_D RGB(0x5a3418)
#define C_HILITE RGB(0xffd84a)
#define C_WHITE RGB(0xffffff)
#define C_SHADOW RGB(0x2b1d14)
#define C_GREEN RGB(0x3aa040)
#define C_BG RGB(0xc89a60)
#define C_BG_LINE RGB(0xb88a50)

static const int DIR_DX[4] = {0, 0, -1, 1}, DIR_DY[4] = {1, -1, 0, 0};

// ---------------------------------------------------------------------------
// Goals (the built-in tutorial)

enum { Q_CUT, Q_TILL, Q_PLANT, Q_WATER, Q_SLEEP, Q_HARVEST, Q_SHIP, Q_BUY, Q_EARN, Q_SPRINKLER };

typedef struct {
    int type, target, reward;
    const char *title, *hint;
} Quest;

static const Quest QUESTS[] = {
    {Q_CUT, 3, 30, "Clear 3 weeds", "Walk up to a weed and press A."},
    {Q_TILL, 4, 40, "Dig 4 patches of soil", "Face empty grass and press A."},
    {Q_PLANT, 4, 40, "Plant 4 seeds", "Press A on the dug-up soil."},
    {Q_WATER, 4, 40, "Water your seeds", "Press A on seeds showing a water drop."},
    {Q_SLEEP, 1, 50, "Go to bed", "Watered crops grow overnight. A at your door."},
    {Q_HARVEST, 4, 100, "Harvest 4 crops", "Water every day. Ripe crops sparkle - press A!"},
    {Q_SHIP, 1, 50, "Sell your harvest", "Press A at the shipping bin by your house."},
    {Q_BUY, 1, 50, "Buy more seeds", "Press A at the striped seed stall."},
    {Q_HARVEST, 25, 250, "Harvest 25 crops", "Dig a bigger field - hold A to work faster!"},
    {Q_EARN, 2000, 400, "Earn 2,000G in total", "Ship crops in the bin to earn gold."},
    {Q_SPRINKLER, 1, 300, "Place a sprinkler", "Buy one at the stall, select it, then press A."},
    {Q_EARN, 10000, 2000, "Earn 10,000G in total", "Melons and pumpkins sell for the most."},
};
#define NUM_QUESTS (int)(sizeof(QUESTS) / sizeof(QUESTS[0]))

// ---------------------------------------------------------------------------
// Helpers

static uint32_t rng_next(SaveData *s)
{
    uint32_t x = s->rng ? s->rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

static int rng_range(SaveData *s, int n)
{
    return (int)(rng_next(s) % (uint32_t)n);
}

static unsigned fx_seed = 12345;
static float frand(void) // cosmetic randomness, not saved
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

static Tile *tile_at(Game *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return NULL;
    return &g->s.map[y][x];
}

static int can_capacity(const SaveData *s)
{
    return s->can_lvl ? 80 : 40;
}

static void show_msg(Game *g, const char *m)
{
    snprintf(g->msg, sizeof(g->msg), "%s", m);
    g->msg_timer = 150;
}

static int is_solid(Game *g, int x, int y)
{
    Tile *t = tile_at(g, x, y);
    if (!t) return 1;
    if (t->ground == G_WATER) return 1;
    return t->obj != O_NONE;
}

static int crop_ripe(const Tile *t)
{
    return t->crop && !t->dead && t->growth >= CROPS[t->crop - 1].days;
}

static int crop_stage(const Tile *t)
{
    const CropDef *c = &CROPS[t->crop - 1];
    if (t->growth == 0) return STAGE_SEED;
    if (t->growth * 3 < c->days) return STAGE_SPROUT;
    if (t->growth * 3 < c->days * 2) return STAGE_YOUNG;
    return STAGE_MATURE;
}

static void facing_tile(Game *g, int *tx, int *ty)
{
    int x = (int)floorf(g->s.px / TILE), y = (int)floorf((g->s.py - 4) / TILE);
    *tx = x + DIR_DX[g->s.dir];
    *ty = y + DIR_DY[g->s.dir];
}

static int in_season(const SaveData *s, int crop)
{
    return (CROPS[crop].seasons & SB(s->season)) != 0;
}

// Backpack contents (seeds, sprinklers and crops; tools are always at hand).
static int owned_items(Game *g, int *out)
{
    int n = 0;
    for (int i = IT_SPRINKLER; i < NUM_ITEMS; i++)
        if (g->s.inv[i] > 0) out[n++] = i;
    return n;
}

static void fix_selection(Game *g)
{
    if (g->s.sel >= 0 && g->s.inv[g->s.sel] > 0) return;
    int items[NUM_ITEMS], n = owned_items(g, items);
    int best = n ? items[0] : -1;
    // Prefer another item of the same kind (e.g. the next seed packet).
    for (int i = 0; i < n; i++)
        if (IS_SEED(g->s.sel) == IS_SEED(items[i]) && IS_CROP(g->s.sel) == IS_CROP(items[i])) {
            best = items[i];
            break;
        }
    g->s.sel = best;
}

static void cycle_selection(Game *g, int dir)
{
    int items[NUM_ITEMS], n = owned_items(g, items), cur = 0;
    if (!n) return;
    for (int i = 0; i < n; i++)
        if (items[i] == g->s.sel) cur = i;
    g->s.sel = items[(cur + dir + n) % n];
    audio_sfx(SFX_MENU_MOVE);
}

// ---------------------------------------------------------------------------
// Effects

static void spawn_particles(Game *g, float x, float y, int n, u32 col, float speed, float grav, int life)
{
    for (int k = 0; k < n; k++) {
        Particle *p = NULL;
        for (int i = 0; i < MAX_PARTICLES; i++)
            if (g->parts[i].life <= 0) {
                p = &g->parts[i];
                break;
            }
        if (!p) return;
        float a = frand() * 6.2831f, sp = speed * (0.4f + frand() * 0.6f);
        p->x = x + (frand() - 0.5f) * 6;
        p->y = y + (frand() - 0.5f) * 4;
        p->vx = cosf(a) * sp;
        p->vy = sinf(a) * sp * 0.6f - speed * 0.7f;
        p->grav = grav;
        p->life = life + (int)(frand() * life * 0.5f);
        p->col = col;
        p->size = frand() < 0.4f ? 2 : 1;
    }
}

static void spawn_floater(Game *g, float x, float y, const char *text, u32 col)
{
    int best = 0;
    for (int i = 0; i < MAX_FLOATERS; i++) {
        if (g->floats[i].life < g->floats[best].life) best = i;
        // Stack above recent text instead of overlapping it.
        if (g->floats[i].life > 45 && fabsf(g->floats[i].x - x) < 40 && fabsf(g->floats[i].y - y) < 9) y = g->floats[i].y - 10;
    }
    Floater *f = &g->floats[best];
    f->x = x;
    f->y = y;
    f->life = 70;
    f->col = col;
    snprintf(f->text, sizeof(f->text), "%s", text);
}

static void spawn_pop(Game *g, float x, float y, int item)
{
    for (int i = 0; i < MAX_POPS; i++)
        if (g->pops[i].life <= 0) {
            g->pops[i] = (ItemPop){x, y, -3.0f, item, 34};
            return;
        }
}

static void update_effects(Game *g)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += p->grav;
        p->vx *= 0.97f;
    }
    for (int i = 0; i < MAX_FLOATERS; i++)
        if (g->floats[i].life > 0) {
            g->floats[i].life--;
            g->floats[i].y -= 0.45f;
        }
    for (int i = 0; i < MAX_POPS; i++) {
        ItemPop *p = &g->pops[i];
        if (p->life <= 0) continue;
        p->life--;
        if (p->life > 14) {
            p->y += p->vy;
            p->vy += 0.22f;
        } else { // fly into the player's backpack
            p->x += (g->s.px - 8 - p->x) * 0.3f;
            p->y += (g->s.py - 20 - p->y) * 0.3f;
        }
    }
}

enum { CRIT_NONE, CRIT_BUTTERFLY, CRIT_FIREFLY, CRIT_LEAF };

static int critter_kind(const SaveData *s)
{
    if (s->weather != WEATHER_SUN || s->season == SEASON_WINTER) return CRIT_NONE;
    if (s->minutes >= NIGHT_MIN + 60) return s->season == SEASON_SUMMER ? CRIT_FIREFLY : CRIT_NONE;
    return s->season == SEASON_FALL ? CRIT_LEAF : CRIT_BUTTERFLY;
}

// Butterflies by day, fireflies on summer nights, leaves in fall.
static void update_critters(Game *g)
{
    int kind = critter_kind(&g->s);
    for (int i = 0; i < MAX_CRITTERS; i++) {
        Critter *c = &g->critters[i];
        float dx = c->x - (g->camx + TOP_W / 2), dy = c->y - (g->camy + SCREEN_H / 2);
        if (!kind) {
            c->t = 0;
            continue;
        }
        if (c->t <= 0 || dx * dx + dy * dy > 300.0f * 300.0f) {
            static const u32 wings[4] = {RGB(0xffffff), RGB(0xf2d34b), RGB(0xf090c0), RGB(0x90c8ff)};
            c->x = g->camx + frand() * TOP_W;
            c->y = g->camy + (kind == CRIT_LEAF ? -10 : frand() * SCREEN_H);
            c->vx = c->vy = 0;
            c->t = 300 + (int)(frand() * 600);
            c->col = kind == CRIT_LEAF ? (frand() < 0.5f ? RGB(0xe0782a) : RGB(0xc04a1a)) : wings[i % 4];
        }
        c->t--;
        if (kind == CRIT_LEAF) {
            c->vx = sinf((g->frame + i * 40) * 0.05f) * 0.8f;
            c->vy = 0.6f;
            if (c->y > g->camy + SCREEN_H + 10) c->t = 0;
        } else {
            c->vx += (frand() - 0.5f) * 0.3f;
            c->vy += (frand() - 0.5f) * 0.3f;
            c->vx *= 0.94f;
            c->vy *= 0.94f;
        }
        c->x += c->vx;
        c->y += c->vy;
    }
}

// ---------------------------------------------------------------------------
// Goals

// Most goals look at the farm itself, so doing steps early or in a
// different order (dig, plant, water one tile at a time) still counts.
static int quest_progress(Game *g)
{
    SaveData *s = &g->s;
    const Quest *q = &QUESTS[s->quest];
    int n = 0;
    switch (q->type) {
    case Q_TILL:
    case Q_PLANT:
    case Q_WATER:
    case Q_SPRINKLER:
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                Tile *t = &s->map[y][x];
                int crop = t->crop && !t->dead;
                n += q->type == Q_TILL ? t->ground == G_TILLED
                     : q->type == Q_PLANT ? crop
                     : q->type == Q_WATER ? crop && t->watered
                                          : t->obj == O_SPRINKLER;
            }
        return n;
    case Q_SHIP: {
        if (s->total_earned > 0) return 1;
        for (int c = 0; c < NUM_CROPS; c++)
            if (s->shipped[c]) return 1;
        return 0;
    }
    case Q_BUY: return s->bought;
    case Q_EARN: return s->total_earned;
    default: return s->quest_prog;
    }
}

static void quest_advance(Game *g)
{
    SaveData *s = &g->s;
    while (s->quest < NUM_QUESTS) {
        const Quest *q = &QUESTS[s->quest];
        if (quest_progress(g) < q->target) return;
        char buf[48];
        s->gold += q->reward;
        snprintf(buf, sizeof(buf), "Goal complete! +%dG", q->reward);
        show_msg(g, buf);
        spawn_floater(g, s->px, s->py - 26, buf, C_HILITE);
        spawn_particles(g, s->px, s->py - 10, 16, C_HILITE, 2.2f, 0.06f, 30);
        audio_sfx(SFX_QUEST);
        s->quest++;
        s->quest_prog = 0;
    }
}

static void quest_event(Game *g, int type, int amount)
{
    SaveData *s = &g->s;
    if (s->quest >= NUM_QUESTS || QUESTS[s->quest].type != type) return;
    s->quest_prog += amount;
    quest_advance(g);
}

// Nearest tile relevant to the current goal, for the guide arrow.
static int quest_target(Game *g, int *ox, int *oy)
{
    SaveData *s = &g->s;
    if (s->quest >= NUM_QUESTS) return 0;
    int type = QUESTS[s->quest].type;
    if (type == Q_SLEEP) {
        *ox = DOOR_X;
        *oy = DOOR_Y;
        return 1;
    }
    if (type == Q_SHIP) {
        *ox = BIN_X;
        *oy = BIN_Y;
        return 1;
    }
    if (type == Q_BUY) {
        *ox = SHOP_X + 1;
        *oy = SHOP_Y;
        return 1;
    }
    if (type != Q_CUT && type != Q_PLANT && type != Q_WATER && type != Q_HARVEST) return 0;
    int px = (int)(s->px / TILE), py = (int)(s->py / TILE), best = 1 << 30;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Tile *t = &s->map[y][x];
            int ok = type == Q_CUT ? t->obj == O_WEED
                     : type == Q_PLANT ? (t->ground == G_TILLED && !t->crop && t->obj == O_NONE)
                     : type == Q_WATER ? (t->crop && !t->dead && !t->watered && !crop_ripe(t))
                                       : crop_ripe(t);
            if (!ok) continue;
            int d = (x - px) * (x - px) + (y - py) * (y - py);
            if (d < best) {
                best = d;
                *ox = x;
                *oy = y;
            }
        }
    return best < (1 << 30);
}

// ---------------------------------------------------------------------------
// World generation

static void gen_map(Game *g)
{
    SaveData *s = &g->s;
    memset(s->map, 0, sizeof(s->map));

    for (int x = 0; x < MAP_W; x++) {
        s->map[0][x].obj = O_TREE;
        s->map[1][x].obj = O_TREE;
        s->map[MAP_H - 1][x].obj = O_TREE;
    }
    for (int y = 0; y < MAP_H; y++) {
        s->map[y][0].obj = O_TREE;
        s->map[y][MAP_W - 1].obj = O_TREE;
    }

    for (int x = 2; x <= 20; x++) s->map[HOUSE_Y + HOUSE_H][x].ground = G_DIRT;
    for (int x = 3; x <= 18; x++) s->map[HOUSE_Y + HOUSE_H + 1][x].ground = G_DIRT;
    for (int y = HOUSE_Y + HOUSE_H; y <= 10; y++) s->map[y][DOOR_X].ground = G_DIRT;

    for (int j = 0; j < HOUSE_H; j++)
        for (int i = 0; i < HOUSE_W; i++) s->map[HOUSE_Y + j][HOUSE_X + i].obj = O_HOUSE;
    s->map[BIN_Y][BIN_X].obj = O_BIN;
    s->map[BIN_Y][BIN_X + 1].obj = O_BIN;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 3; i++) s->map[SHOP_Y + j][SHOP_X + i].obj = O_SHOP;

    for (int x = 9; x <= 22; x++) s->map[9][x].obj = O_FENCE;
    for (int y = 2; y <= 9; y++) s->map[y][22].obj = O_FENCE;
    s->map[6][22].obj = O_NONE;
    s->map[7][22].obj = O_NONE;

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            float fx = (x - 39.5f) / 5.5f, fy = (y - 22.5f) / 3.6f;
            if (fx * fx + fy * fy <= 1.0f) {
                s->map[y][x].ground = G_WATER;
                s->map[y][x].obj = O_NONE;
            }
        }

    for (int y = 2; y < 14; y++)
        for (int x = 26; x < MAP_W - 1; x++)
            if (rng_range(s, 100) < 18 + (x - 26) * 2) s->map[y][x].obj = O_TREE;
    for (int x = 25; x < 30; x++) s->map[7][x].obj = O_NONE;

    for (int y = 10; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            Tile *t = &s->map[y][x];
            if (t->ground != G_GRASS || t->obj != O_NONE) continue;
            int r = rng_range(s, 100);
            if (r < 14) t->obj = O_WEED;
            else if (r < 19) t->obj = O_STONE;
            else if (r < 23) t->obj = O_TWIG;
            else if (r < 25 && x > 30) t->obj = O_TREE;
        }
    // A clear starter patch with a few weeds to practise on
    for (int y = 10; y <= 15; y++)
        for (int x = 3; x <= 11; x++) s->map[y][x].obj = O_NONE;
    s->map[11][4].obj = O_WEED;
    s->map[11][8].obj = O_WEED;
    s->map[12][10].obj = O_WEED;
    s->map[11][11].obj = O_STONE;
}

static void update_camera(Game *g, int snap)
{
    float tx = g->s.px - TOP_W / 2, ty = g->s.py - 8 - SCREEN_H / 2;
    if (snap) {
        g->camx = tx;
        g->camy = ty;
    } else {
        g->camx += (tx - g->camx) * 0.2f;
        g->camy += (ty - g->camy) * 0.2f;
    }
    if (g->camx < 0) g->camx = 0;
    if (g->camy < 0) g->camy = 0;
    if (g->camx > MAP_W * TILE - TOP_W) g->camx = MAP_W * TILE - TOP_W;
    if (g->camy > MAP_H * TILE - SCREEN_H) g->camy = MAP_H * TILE - SCREEN_H;
}

static void new_game(Game *g)
{
    SaveData *s = &g->s;
    int music = s->music_on;
    memset(s, 0, sizeof(*s));
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->rng = 0xC0FFEE11u;
    gen_map(g);
    s->day = 1;
    s->season = SEASON_SPRING;
    s->year = 1;
    s->minutes = START_MIN;
    s->gold = 500;
    s->energy = MAX_ENERGY;
    s->water = can_capacity(s);
    s->inv[IT_HOE] = s->inv[IT_CAN] = s->inv[IT_SCYTHE] = s->inv[IT_PICK] = 1;
    s->inv[IT_SEED0 + 0] = 15; // parsnip seeds
    s->sel = IT_SEED0;
    s->px = DOOR_X * TILE + 8;
    s->py = (DOOR_Y + 1) * TILE + 14;
    s->dir = DIR_DOWN;
    s->music_on = music;
    spr_set_season(s->season);
    g->loaded_season = s->season;
    update_camera(g, 1);
    show_msg(g, "Welcome to your farm! Follow the goals below.");
}

// ---------------------------------------------------------------------------
// Save / load

static int save_game(Game *g)
{
    FILE *f = fopen(plat_save_path(), "wb");
    if (!f) return 0;
    size_t ok = fwrite(&g->s, sizeof(g->s), 1, f);
    fclose(f);
    if (ok == 1) g->has_save = 1;
    return ok == 1;
}

static int load_game(SaveData *out)
{
    FILE *f = fopen(plat_save_path(), "rb");
    if (!f) return 0;
    size_t ok = fread(out, sizeof(*out), 1, f);
    fclose(f);
    return ok == 1 && out->magic == SAVE_MAGIC && out->version == SAVE_VERSION;
}

// ---------------------------------------------------------------------------
// Day cycle

static void water_tile(Game *g, int x, int y)
{
    Tile *t = tile_at(g, x, y);
    if (t && t->ground == G_TILLED) t->watered = 1;
}

static void end_day(Game *g, int passed_out)
{
    SaveData *s = &g->s;

    g->sum_day = s->day;
    g->sum_earn = 0;
    for (int i = 0; i < NUM_CROPS; i++) {
        g->sum_items[i] = s->shipped[i];
        g->sum_earn += s->shipped[i] * CROPS[i].sell_price;
        s->shipped[i] = 0;
    }
    s->gold += g->sum_earn;
    s->total_earned += g->sum_earn;
    g->passed_out = passed_out;

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Tile *t = &s->map[y][x];
            if (t->crop && !t->dead && t->watered && t->growth < CROPS[t->crop - 1].days) t->growth++;
        }

    s->day++;
    if (s->day > DAYS_PER_SEASON) {
        s->day = 1;
        s->season = (s->season + 1) % 4;
        if (s->season == SEASON_SPRING) s->year++;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                Tile *t = &s->map[y][x];
                if (t->crop && !in_season(s, t->crop - 1)) t->dead = 1;
            }
    }

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Tile *t = &s->map[y][x];
            if (t->ground == G_TILLED && !t->crop && !t->watered && rng_range(s, 100) < 8) t->ground = G_GRASS;
            t->watered = 0;
        }

    if (s->season != SEASON_WINTER)
        for (int i = 0; i < 16; i++) {
            int x = 1 + rng_range(s, MAP_W - 2), y = 10 + rng_range(s, MAP_H - 11);
            Tile *t = &s->map[y][x];
            if (t->ground == G_GRASS && t->obj == O_NONE) t->obj = O_WEED;
        }

    int r = rng_range(s, 100);
    if (s->day == 1 || (s->year == 1 && s->season == SEASON_SPRING && s->day <= 3)) s->weather = WEATHER_SUN;
    else if (s->season == SEASON_WINTER) s->weather = r < 25 ? WEATHER_SNOW : WEATHER_SUN;
    else s->weather = r < (s->season == SEASON_SUMMER ? 12 : 22) ? WEATHER_RAIN : WEATHER_SUN;

    if (s->weather == WEATHER_RAIN)
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) water_tile(g, x, y);

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (s->map[y][x].obj == O_SPRINKLER)
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) water_tile(g, x + i, y + j);

    s->minutes = START_MIN;
    s->energy = passed_out ? MAX_ENERGY / 2 : MAX_ENERGY;
    s->px = DOOR_X * TILE + 8;
    s->py = (DOOR_Y + 1) * TILE + 14;
    s->dir = DIR_DOWN;
    g->swing = 0;
    g->msg_timer = 0;
    memset(g->parts, 0, sizeof(g->parts));
    memset(g->floats, 0, sizeof(g->floats));
    memset(g->pops, 0, sizeof(g->pops));
    update_camera(g, 1);

    if (g->loaded_season != s->season) {
        spr_set_season(s->season);
        g->loaded_season = s->season;
    }
    quest_event(g, Q_SLEEP, 1);
    quest_advance(g); // earning goals
    save_game(g);
    g->mode = MODE_SUMMARY;
    if (g->sum_earn > 0) audio_sfx(SFX_COIN);
}

static void start_sleep(Game *g, int passed_out)
{
    g->fade_dir = 1;
    g->fade_pending = passed_out ? 2 : 1;
    audio_sfx(SFX_SLEEP);
}

// ---------------------------------------------------------------------------
// The smart action button: works out what A does on the tile in front of you.

enum {
    ACT_NONE,
    ACT_SLEEP,
    ACT_SHIP,
    ACT_SHOP,
    ACT_HARVEST,
    ACT_CLEAR_DEAD,
    ACT_CUT,
    ACT_BREAK,
    ACT_REFILL,
    ACT_PLANT,
    ACT_WATER,
    ACT_TILL,
    ACT_PLACE,
    ACT_PICKUP,
    // "can't do that" hints
    ACT_NEED_SEEDS,
    ACT_OFF_SEASON,
    ACT_EMPTY_CAN,
    ACT_NO_CROPS,
};

typedef struct {
    int kind, tool, item, tx, ty;
    const char *label;
} Action;

static int pick_seed(Game *g)
{
    SaveData *s = &g->s;
    if (IS_SEED(s->sel) && s->inv[s->sel] > 0 && in_season(s, s->sel - IT_SEED0)) return s->sel;
    for (int c = 0; c < NUM_CROPS; c++)
        if (s->inv[IT_SEED0 + c] > 0 && in_season(s, c)) return IT_SEED0 + c;
    return -1;
}

static int have_any_seeds(Game *g)
{
    for (int c = 0; c < NUM_CROPS; c++)
        if (g->s.inv[IT_SEED0 + c] > 0) return 1;
    return 0;
}

static int have_crops(Game *g)
{
    for (int c = 0; c < NUM_CROPS; c++)
        if (g->s.inv[IT_CROP0 + c] > 0) return 1;
    return 0;
}

static Action get_action(Game *g)
{
    SaveData *s = &g->s;
    Action a = {ACT_NONE, -1, -1, 0, 0, NULL};
    facing_tile(g, &a.tx, &a.ty);
    Tile *t = tile_at(g, a.tx, a.ty);
    int placing = s->sel == IT_SPRINKLER && s->inv[IT_SPRINKLER] > 0;
    if (!t) return a;

    if (a.tx == DOOR_X && a.ty == DOOR_Y) {
        a.kind = ACT_SLEEP;
        a.label = "Sleep";
    } else if (t->obj == O_BIN) {
        a.kind = have_crops(g) ? ACT_SHIP : ACT_NO_CROPS;
        a.label = have_crops(g) ? "Ship crops" : "Nothing to ship";
    } else if (t->obj == O_SHOP) {
        a.kind = ACT_SHOP;
        a.label = "Shop";
    } else if (t->obj == O_WEED || t->obj == O_TWIG) {
        a.kind = ACT_CUT;
        a.tool = IT_SCYTHE;
        a.label = t->obj == O_WEED ? "Cut weeds" : "Clear twig";
    } else if (t->obj == O_STONE) {
        a.kind = ACT_BREAK;
        a.tool = IT_PICK;
        a.label = "Break stone";
    } else if (t->obj == O_SPRINKLER) {
        a.kind = ACT_PICKUP;
        a.label = "Pick up";
    } else if (t->obj != O_NONE) {
        return a;
    } else if (t->ground == G_WATER) {
        if (s->water < can_capacity(s)) {
            a.kind = ACT_REFILL;
            a.tool = IT_CAN;
            a.label = "Refill can";
        }
    } else if (t->crop) {
        if (t->dead) {
            a.kind = ACT_CLEAR_DEAD;
            a.tool = IT_SCYTHE;
            a.label = "Clear";
        } else if (crop_ripe(t)) {
            a.kind = ACT_HARVEST;
            a.label = "Harvest";
        } else if (!t->watered) {
            a.kind = s->water > 0 ? ACT_WATER : ACT_EMPTY_CAN;
            a.tool = IT_CAN;
            a.label = s->water > 0 ? "Water" : "Can empty";
        }
    } else if (placing) {
        a.kind = ACT_PLACE;
        a.label = "Place sprinkler";
    } else if (t->ground == G_TILLED) {
        a.item = pick_seed(g);
        if (a.item >= 0) {
            a.kind = ACT_PLANT;
            a.label = "Plant";
        } else {
            a.kind = have_any_seeds(g) ? ACT_OFF_SEASON : ACT_NEED_SEEDS;
            a.label = have_any_seeds(g) ? "Wrong season" : "Need seeds";
        }
    } else {
        a.kind = ACT_TILL;
        a.tool = IT_HOE;
        a.label = "Dig";
    }
    return a;
}

static int is_hint(int kind)
{
    return kind >= ACT_NEED_SEEDS;
}

static int repeatable(int kind)
{
    return kind == ACT_TILL || kind == ACT_PLANT || kind == ACT_WATER || kind == ACT_CUT || kind == ACT_BREAK ||
           kind == ACT_HARVEST || kind == ACT_CLEAR_DEAD;
}

static int spend_energy(Game *g, int cost)
{
    if (g->s.energy < cost) {
        show_msg(g, "You're exhausted! Eat a crop (Y) or go to bed.");
        audio_sfx(SFX_ERROR);
        return 0;
    }
    g->s.energy -= cost;
    return 1;
}

static void start_swing(Game *g, int tool)
{
    g->swing = SWING_FRAMES;
    g->swing_tool = tool;
}

static int tillable(const Tile *t)
{
    return t && (t->ground == G_GRASS || t->ground == G_DIRT) && t->obj == O_NONE && !t->crop;
}

static void do_action(Game *g, const Action *a)
{
    SaveData *s = &g->s;
    Tile *t = tile_at(g, a->tx, a->ty);
    float cx = a->tx * TILE + 8, cy = a->ty * TILE + 10;
    char buf[64];

    switch (a->kind) {
    case ACT_SLEEP:
        g->mode = MODE_ASK_SLEEP;
        g->menu_sel = 0;
        audio_sfx(SFX_MENU_OK);
        break;
    case ACT_SHOP:
        g->mode = MODE_SHOP;
        g->shop_sel = 0;
        g->shop_scroll = 0;
        audio_sfx(SFX_MENU_OK);
        break;
    case ACT_SHIP: {
        int total = 0, count = 0;
        for (int c = 0; c < NUM_CROPS; c++) {
            int n = s->inv[IT_CROP0 + c];
            s->shipped[c] += n;
            total += n * CROPS[c].sell_price;
            count += n;
            s->inv[IT_CROP0 + c] = 0;
        }
        snprintf(buf, sizeof(buf), "+%dG", total);
        spawn_floater(g, cx, cy - 14, buf, C_HILITE);
        snprintf(buf, sizeof(buf), "Shipped %d crops. You'll get %dG overnight!", count, total);
        show_msg(g, buf);
        spawn_particles(g, cx + 8, cy - 4, 10, RGB(0xf2c030), 1.8f, 0.08f, 24);
        audio_sfx(SFX_COIN);
        fix_selection(g);
        break;
    }
    case ACT_HARVEST: {
        int c = t->crop - 1, n = CROPS[c].yield + (rng_range(s, 10) == 0 ? 1 : 0);
        s->inv[IT_CROP0 + c] += n;
        s->harvested += n;
        if (CROPS[c].regrow) t->growth = (uint8_t)(CROPS[c].days - CROPS[c].regrow);
        else {
            t->crop = 0;
            t->growth = 0;
        }
        spawn_pop(g, cx - 8, cy - 12, IT_CROP0 + c);
        spawn_particles(g, cx, cy - 4, 8, RGB(0x6ac050), 1.6f, 0.1f, 18);
        snprintf(buf, sizeof(buf), "+%d %s", n, CROPS[c].name);
        spawn_floater(g, cx, cy - 18, buf, C_WHITE);
        if (s->sel < 0) s->sel = IT_CROP0 + c;
        audio_sfx(SFX_HARVEST);
        quest_event(g, Q_HARVEST, n);
        break;
    }
    case ACT_CUT:
        if (!spend_energy(g, 1)) return;
        t->obj = O_NONE;
        spawn_particles(g, cx, cy, 10, RGB(0x4fae3a), 1.8f, 0.12f, 18);
        audio_sfx(SFX_CUT);
        start_swing(g, a->tool);
        quest_event(g, Q_CUT, 1);
        break;
    case ACT_CLEAR_DEAD:
        t->crop = 0;
        t->dead = 0;
        t->growth = 0;
        spawn_particles(g, cx, cy, 8, RGB(0xa07a48), 1.6f, 0.12f, 18);
        audio_sfx(SFX_CUT);
        start_swing(g, a->tool);
        break;
    case ACT_BREAK:
        if (!spend_energy(g, 2)) return;
        t->obj = O_NONE;
        spawn_particles(g, cx, cy, 14, RGB(0x9c9a98), 2.2f, 0.15f, 20);
        audio_sfx(SFX_BREAK);
        start_swing(g, a->tool);
        break;
    case ACT_PICKUP:
        t->obj = O_NONE;
        s->inv[IT_SPRINKLER]++;
        if (s->sel < 0) s->sel = IT_SPRINKLER;
        audio_sfx(SFX_PLACE);
        break;
    case ACT_REFILL:
        s->water = can_capacity(s);
        spawn_particles(g, cx, cy, 12, RGB(0x9fd3ff), 1.8f, 0.12f, 20);
        show_msg(g, "Watering can refilled!");
        audio_sfx(SFX_REFILL);
        start_swing(g, a->tool);
        break;
    case ACT_TILL: {
        if (!spend_energy(g, s->hoe_lvl ? 3 : 2)) return;
        int n = s->hoe_lvl ? 3 : 1;
        for (int i = 0; i < n; i++) {
            Tile *u = tile_at(g, a->tx + DIR_DX[s->dir] * i, a->ty + DIR_DY[s->dir] * i);
            if (!tillable(u)) break;
            u->ground = G_TILLED;
            u->watered = s->weather == WEATHER_RAIN;
            spawn_particles(g, cx + DIR_DX[s->dir] * i * TILE, cy + DIR_DY[s->dir] * i * TILE, 8, RGB(0x8e5c34), 1.6f, 0.15f, 16);
        }
        audio_sfx(SFX_TILL);
        start_swing(g, a->tool);
        break;
    }
    case ACT_PLANT: {
        int c = a->item - IT_SEED0;
        t->crop = (uint8_t)(c + 1);
        t->growth = 0;
        t->dead = 0;
        s->inv[a->item]--;
        spawn_particles(g, cx, cy, 5, RGB(0xc48a4a), 1.0f, 0.1f, 12);
        audio_sfx(SFX_PLANT);
        if (s->inv[a->item] == 0 && s->sel == a->item) fix_selection(g);
        break;
    }
    case ACT_WATER: {
        if (!spend_energy(g, 1)) return;
        int n = s->can_lvl ? 3 : 1;
        for (int i = 0; i < n && s->water > 0; i++) {
            Tile *u = tile_at(g, a->tx + DIR_DX[s->dir] * i, a->ty + DIR_DY[s->dir] * i);
            if (!u || u->ground != G_TILLED) break;
            u->watered = 1;
            s->water--;
            spawn_particles(g, cx + DIR_DX[s->dir] * i * TILE, cy + DIR_DY[s->dir] * i * TILE, 8, RGB(0x9fd3ff), 1.4f, 0.12f, 16);
        }
        audio_sfx(SFX_WATER);
        start_swing(g, a->tool);
        break;
    }
    case ACT_PLACE:
        t->obj = O_SPRINKLER;
        if (t->ground == G_TILLED) t->ground = G_DIRT;
        t->watered = 0;
        s->inv[IT_SPRINKLER]--;
        show_msg(g, "Sprinkler placed! It waters 8 tiles every morning.");
        audio_sfx(SFX_PLACE);
        fix_selection(g);
        break;
    case ACT_NEED_SEEDS:
        show_msg(g, "You're out of seeds! Buy more at the seed stall.");
        audio_sfx(SFX_ERROR);
        break;
    case ACT_OFF_SEASON:
        snprintf(buf, sizeof(buf), "Your seeds won't grow in %s. Visit the stall!", SEASON_NAMES[s->season]);
        show_msg(g, buf);
        audio_sfx(SFX_ERROR);
        break;
    case ACT_EMPTY_CAN:
        show_msg(g, "Your watering can is empty. Refill it at the pond!");
        audio_sfx(SFX_ERROR);
        break;
    case ACT_NO_CROPS:
        show_msg(g, "Harvest some crops first, then ship them here.");
        audio_sfx(SFX_ERROR);
        break;
    default: break;
    }
    quest_advance(g);
}

static void eat_selected(Game *g)
{
    SaveData *s = &g->s;
    if (!IS_CROP(s->sel) || s->inv[s->sel] <= 0) {
        show_msg(g, "Select a crop in your backpack to eat it.");
        audio_sfx(SFX_ERROR);
        return;
    }
    if (s->energy >= MAX_ENERGY) {
        show_msg(g, "You're not hungry right now.");
        return;
    }
    int c = s->sel - IT_CROP0, gain = CROPS[c].sell_price / 3;
    if (gain < 15) gain = 15;
    if (gain > 60) gain = 60;
    s->energy += gain;
    if (s->energy > MAX_ENERGY) s->energy = MAX_ENERGY;
    s->inv[s->sel]--;
    char buf[32];
    snprintf(buf, sizeof(buf), "+%d energy", gain);
    spawn_floater(g, s->px, s->py - 24, buf, RGB(0x8af07a));
    audio_sfx(SFX_EAT);
    fix_selection(g);
}

// ---------------------------------------------------------------------------
// Shop

enum { SHOP_ITEM, SHOP_HOE, SHOP_CAN };
typedef struct {
    int kind, item, price;
} ShopEntry;

static int shop_entries(Game *g, ShopEntry *out)
{
    SaveData *s = &g->s;
    int n = 0;
    for (int c = 0; c < NUM_CROPS; c++)
        if (in_season(s, c)) out[n++] = (ShopEntry){SHOP_ITEM, IT_SEED0 + c, CROPS[c].seed_price};
    out[n++] = (ShopEntry){SHOP_ITEM, IT_SPRINKLER, SPRINKLER_PRICE};
    if (!s->can_lvl) out[n++] = (ShopEntry){SHOP_CAN, IT_CAN, CAN_UPGRADE_PRICE};
    if (!s->hoe_lvl) out[n++] = (ShopEntry){SHOP_HOE, IT_HOE, HOE_UPGRADE_PRICE};
    return n;
}

static void shop_entry_name(const ShopEntry *e, char *buf, int len)
{
    if (e->kind == SHOP_CAN) snprintf(buf, len, "Big Watering Can");
    else if (e->kind == SHOP_HOE) snprintf(buf, len, "Steel Hoe");
    else item_name(e->item, buf, len);
}

static void shop_buy(Game *g, int count)
{
    ShopEntry e[NUM_ITEMS + 4];
    int n = shop_entries(g, e);
    if (g->shop_sel >= n) return;
    ShopEntry *it = &e[g->shop_sel];
    SaveData *s = &g->s;
    char name[32], buf[64];
    if (it->kind != SHOP_ITEM) count = 1;
    if (s->gold < it->price) {
        show_msg(g, "Not enough gold.");
        audio_sfx(SFX_ERROR);
        return;
    }
    int bought = 0;
    while (bought < count && s->gold >= it->price) {
        s->gold -= it->price;
        bought++;
    }
    shop_entry_name(it, name, sizeof(name));
    if (it->kind == SHOP_CAN) {
        s->can_lvl = 1;
        s->water = can_capacity(s);
        show_msg(g, "Big can! Holds 80 water and waters 3 tiles.");
    } else if (it->kind == SHOP_HOE) {
        s->hoe_lvl = 1;
        show_msg(g, "Steel hoe! Digs 3 tiles at once.");
    } else {
        s->inv[it->item] += bought;
        s->sel = it->item;
        snprintf(buf, sizeof(buf), "Bought %d %s", bought, name);
        show_msg(g, buf);
    }
    audio_sfx(SFX_COIN);
    s->bought += bought;
    quest_advance(g);
    n = shop_entries(g, e);
    if (g->shop_sel >= n) g->shop_sel = n - 1;
}

#define SHOP_ROWS 7
#define SHOP_ROW_Y 32
#define SHOP_ROW_H 24

static void update_shop(Game *g, const Input *in)
{
    ShopEntry e[NUM_ITEMS + 4];
    int n = shop_entries(g, e);
    if (in->down & (BTN_B | BTN_START)) {
        g->mode = MODE_PLAY;
        audio_sfx(SFX_MENU_BACK);
        return;
    }
    if (in->down & BTN_UP) {
        g->shop_sel = (g->shop_sel + n - 1) % n;
        audio_sfx(SFX_MENU_MOVE);
    }
    if (in->down & BTN_DOWN) {
        g->shop_sel = (g->shop_sel + 1) % n;
        audio_sfx(SFX_MENU_MOVE);
    }
    if (in->down & BTN_A) shop_buy(g, 1);
    if (in->down & BTN_X) shop_buy(g, 5);
    if (in->touch_down) {
        if (in->tx >= 262 && in->ty < 26) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
            return;
        }
        int row = (in->ty - SHOP_ROW_Y) / SHOP_ROW_H;
        if (in->ty >= SHOP_ROW_Y && row < SHOP_ROWS) {
            int idx = g->shop_scroll + row;
            if (idx < n) {
                if (idx == g->shop_sel) shop_buy(g, 1);
                else {
                    g->shop_sel = idx;
                    audio_sfx(SFX_MENU_MOVE);
                }
            }
        }
    }
    if (g->shop_sel < g->shop_scroll) g->shop_scroll = g->shop_sel;
    if (g->shop_sel >= g->shop_scroll + SHOP_ROWS) g->shop_scroll = g->shop_sel - SHOP_ROWS + 1;
    if (g->msg_timer > 0) g->msg_timer--;
}

// ---------------------------------------------------------------------------
// Update

#define INV_COLS 8
#define INV_ROWS 2
#define INV_X 10
#define INV_Y 120
#define INV_STEP 38

static int inventory_scroll(Game *g)
{
    int items[NUM_ITEMS], n = owned_items(g, items), idx = 0;
    for (int i = 0; i < n; i++)
        if (items[i] == g->s.sel) idx = i;
    int per = INV_COLS * INV_ROWS;
    return idx >= per ? (idx / INV_COLS - INV_ROWS + 1) * INV_COLS : 0;
}

static void inventory_touch(Game *g, int tx, int ty)
{
    if (tx < INV_X || ty < INV_Y) return;
    int col = (tx - INV_X) / INV_STEP, row = (ty - INV_Y) / INV_STEP;
    if (col >= INV_COLS || row >= INV_ROWS) return;
    int items[NUM_ITEMS], n = owned_items(g, items);
    int idx = inventory_scroll(g) + row * INV_COLS + col;
    if (idx < n) {
        g->s.sel = items[idx];
        audio_sfx(SFX_MENU_MOVE);
    }
}

static int box_free(Game *g, float x, float y)
{
    int x0 = (int)floorf((x - 5) / TILE), x1 = (int)floorf((x + 4.99f) / TILE);
    int y0 = (int)floorf((y - 6) / TILE), y1 = (int)floorf((y - 0.01f) / TILE);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (is_solid(g, tx, ty)) return 0;
    return 1;
}

static void update_play(Game *g, const Input *in)
{
    SaveData *s = &g->s;
    if (in->down & BTN_START) {
        g->mode = MODE_PAUSE;
        g->menu_sel = 0;
        audio_sfx(SFX_MENU_OK);
        return;
    }
    if (in->touch_down) inventory_touch(g, in->tx, in->ty);
    if (in->down & BTN_L) cycle_selection(g, -1);
    if (in->down & BTN_R) cycle_selection(g, 1);

    if (++g->minute_acc >= FRAMES_PER_MIN) {
        g->minute_acc = 0;
        s->minutes++;
        if (s->minutes == 24 * 60) show_msg(g, "It's midnight! Head to bed before 2AM.");
        if (s->minutes >= END_MIN) {
            start_sleep(g, 1);
            return;
        }
    }
    if (g->msg_timer > 0) g->msg_timer--;

    if (in->held & BTN_A) g->a_hold++;
    else g->a_hold = 0;

    if (g->swing > 0) {
        g->swing--;
        g->moving = 0;
        return;
    }

    int dx = 0, dy = 0;
    if (in->held & BTN_UP) dy--;
    if (in->held & BTN_DOWN) dy++;
    if (in->held & BTN_LEFT) dx--;
    if (in->held & BTN_RIGHT) dx++;
    if (dx || dy) {
        if (dx && !dy) s->dir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
        else if (dy && !dx) s->dir = dy < 0 ? DIR_UP : DIR_DOWN;
        else {
            int vert_ok = (dy < 0 && s->dir == DIR_UP) || (dy > 0 && s->dir == DIR_DOWN);
            int hori_ok = (dx < 0 && s->dir == DIR_LEFT) || (dx > 0 && s->dir == DIR_RIGHT);
            if (!vert_ok && !hori_ok) s->dir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
        }
        int run = (in->held & BTN_B) != 0;
        float sp = run ? RUN_SPEED : WALK_SPEED;
        if (dx && dy) sp *= 0.7071f;
        float nx = s->px + dx * sp, ny = s->py + dy * sp;
        if (box_free(g, nx, s->py)) s->px = nx;
        if (box_free(g, s->px, ny)) s->py = ny;
        g->moving = 1;
        g->anim += run ? 2 : 1;
        if (++g->step_timer >= (run ? 9 : 13)) {
            g->step_timer = 0;
            audio_sfx(SFX_STEP);
        }
    } else {
        g->moving = 0;
        g->anim = 0;
    }

    if (in->down & BTN_A) {
        Action a = get_action(g);
        if (a.kind != ACT_NONE) do_action(g, &a);
    } else if (g->a_hold > 10) { // hold A to keep working
        Action a = get_action(g);
        if (repeatable(a.kind)) do_action(g, &a);
    }
    if (in->down & BTN_Y) eat_selected(g);
}

static int menu_nav(Game *g, const Input *in, int count)
{
    if (in->down & BTN_UP) {
        g->menu_sel = (g->menu_sel + count - 1) % count;
        audio_sfx(SFX_MENU_MOVE);
    }
    if (in->down & BTN_DOWN) {
        g->menu_sel = (g->menu_sel + 1) % count;
        audio_sfx(SFX_MENU_MOVE);
    }
    return (in->down & BTN_A) != 0;
}

#define MENU_BTN_X 50
#define MENU_BTN_W 220
#define MENU_BTN_Y 46
#define MENU_BTN_H 30
#define MENU_BTN_GAP 37

static int menu_touch(const Input *in, int count)
{
    if (!in->touch_down || in->tx < MENU_BTN_X || in->tx >= MENU_BTN_X + MENU_BTN_W) return -1;
    for (int i = 0; i < count; i++) {
        int y = MENU_BTN_Y + i * MENU_BTN_GAP;
        if (in->ty >= y && in->ty < y + MENU_BTN_H) return i;
    }
    return -1;
}

#define HELP_PAGES 3

static void open_help(Game *g, int return_mode)
{
    g->mode = MODE_HELP;
    g->help_page = 0;
    g->help_return = return_mode;
    audio_sfx(SFX_MENU_OK);
}

void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    spr_init();
    SaveData tmp;
    g->has_save = load_game(&tmp);
    g->s.music_on = g->has_save ? tmp.music_on : 1;
    g->mode = MODE_TITLE;
    g->loaded_season = SEASON_SPRING;
    audio_set_music_enabled(g->s.music_on);
}

static void update_music(Game *g)
{
    int song = MUS_DAY;
    if (g->mode == MODE_SUMMARY || g->fade_dir > 0) song = MUS_NONE;
    else if (g->mode != MODE_TITLE && g->mode != MODE_HELP && g->s.minutes >= NIGHT_MIN) song = MUS_NIGHT;
    audio_music(song);
    audio_set_music_enabled(g->s.music_on);
}

void game_update(Game *g, const Input *in)
{
    g->frame++;
    update_music(g);
    update_effects(g);
    if (g->mode != MODE_TITLE && g->mode != MODE_HELP) update_critters(g);

    // Fade to black and back around sleeping.
    if (g->fade_dir > 0) {
        g->fade += 9;
        if (g->fade >= 255) {
            g->fade = 255;
            g->fade_dir = -1;
            end_day(g, g->fade_pending == 2);
        }
        return;
    }
    if (g->fade_dir < 0) {
        g->fade -= 12;
        if (g->fade <= 0) {
            g->fade = 0;
            g->fade_dir = 0;
        }
    }

    int pick;
    switch (g->mode) {
    case MODE_TITLE: {
        int count = g->has_save ? 3 : 2;
        pick = menu_nav(g, in, count) ? g->menu_sel : menu_touch(in, count);
        if (pick < 0) break;
        if (pick == count - 1) {
            open_help(g, MODE_TITLE);
            break;
        }
        audio_sfx(SFX_MENU_OK);
        SaveData tmp;
        if (g->has_save && pick == 0 && load_game(&tmp)) {
            g->s = tmp;
            spr_set_season(g->s.season);
            g->loaded_season = g->s.season;
            fix_selection(g);
            show_msg(g, "Welcome back!");
        } else new_game(g);
        g->mode = MODE_PLAY;
        g->fade = 255;
        g->fade_dir = -1;
        update_camera(g, 1);
        break;
    }
    case MODE_PLAY:
        update_play(g, in);
        update_camera(g, 0);
        break;
    case MODE_SHOP:
        update_shop(g, in);
        break;
    case MODE_HELP:
        if (in->down & (BTN_B | BTN_START) || (in->down & BTN_A && g->help_page == HELP_PAGES - 1)) {
            g->mode = g->help_return;
            audio_sfx(SFX_MENU_BACK);
        } else if (in->down & (BTN_A | BTN_R | BTN_RIGHT) || in->touch_down) {
            g->help_page = (g->help_page + 1) % HELP_PAGES;
            audio_sfx(SFX_MENU_MOVE);
        } else if (in->down & (BTN_L | BTN_LEFT) && g->help_page > 0) {
            g->help_page--;
            audio_sfx(SFX_MENU_MOVE);
        }
        break;
    case MODE_PAUSE:
        if (in->down & (BTN_B | BTN_START)) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
            break;
        }
        pick = menu_nav(g, in, 5) ? g->menu_sel : menu_touch(in, 5);
        if (pick == 0) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
        } else if (pick == 1) open_help(g, MODE_PAUSE);
        else if (pick == 2) {
            g->s.music_on = !g->s.music_on;
            audio_sfx(SFX_MENU_OK);
        } else if (pick == 3) {
            show_msg(g, save_game(g) ? "Game saved." : "Could not save!");
            audio_sfx(SFX_MENU_OK);
            g->mode = MODE_PLAY;
        } else if (pick == 4) {
            save_game(g);
            g->quit = 1;
        }
        break;
    case MODE_ASK_SLEEP:
        if (in->down & BTN_B) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
            break;
        }
        pick = menu_nav(g, in, 2) ? g->menu_sel : menu_touch(in, 2);
        if (pick == 0) {
            g->mode = MODE_PLAY;
            start_sleep(g, 0);
        } else if (pick == 1) {
            g->mode = MODE_PLAY;
            audio_sfx(SFX_MENU_BACK);
        }
        break;
    case MODE_SUMMARY:
        if ((in->down & BTN_A) || in->touch_down) {
            g->mode = MODE_PLAY;
            g->fade = 255;
            g->fade_dir = -1;
            audio_sfx(SFX_MENU_OK);
            if (g->passed_out) show_msg(g, "You passed out... Get to bed before 2AM!");
            else if (g->s.day == 1) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s has arrived! New seeds at the stall.", SEASON_NAMES[g->s.season]);
                show_msg(g, buf);
            } else if (g->s.weather == WEATHER_RAIN) show_msg(g, "It's raining - your crops are watered today!");
            else show_msg(g, "Good morning! Don't forget to water your crops.");
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering: world

static void draw_fence(Canvas *c, Game *g, int tx, int ty, int sx, int sy)
{
    u32 dark = RGB(0x5a3418), mid = RGB(0x9a6332), light = RGB(0xc48a4a);
    Tile *l = tile_at(g, tx - 1, ty), *r = tile_at(g, tx + 1, ty);
    Tile *u = tile_at(g, tx, ty - 1), *d = tile_at(g, tx, ty + 1);
    int hl = l && l->obj == O_FENCE, hr = r && r->obj == O_FENCE;
    int vu = u && u->obj == O_FENCE, vd = d && d->obj == O_FENCE;
    if (hl || hr) {
        int x0 = hl ? 0 : 6, x1 = hr ? 16 : 10;
        cv_rect(c, sx + x0, sy + 5, x1 - x0, 3, dark);
        cv_rect(c, sx + x0, sy + 5, x1 - x0, 2, mid);
        cv_rect(c, sx + x0, sy + 10, x1 - x0, 3, dark);
        cv_rect(c, sx + x0, sy + 10, x1 - x0, 2, mid);
    }
    if (vu || vd) cv_rect(c, sx + 7, sy + (vu ? 0 : 6), 3, (vd ? 16 : 10) - (vu ? 0 : 6), dark);
    cv_rect(c, sx + 5, sy + 1, 6, 14, dark);
    cv_rect(c, sx + 6, sy + 2, 4, 12, mid);
    cv_rect(c, sx + 6, sy + 2, 1, 12, light);
}

static void draw_house(Canvas *c, int sx, int sy, int night)
{
    int w = HOUSE_W * TILE, h = HOUSE_H * TILE;
    u32 ink = RGB(0x2b1d14), wall = RGB(0xd8a868), wall_d = RGB(0xb07e44);
    u32 roof = RGB(0xb83a3a), roof_d = RGB(0x7a2424), roof_l = RGB(0xe05a4a);
    int roof_h = 30;
    cv_rect(c, sx + 58, sy - 4, 10, 14, ink);
    cv_rect(c, sx + 59, sy - 3, 8, 13, RGB(0x8a8a8a));
    cv_rect(c, sx + 59, sy - 3, 8, 2, RGB(0xaaaaaa));
    cv_rect(c, sx + 3, sy + roof_h - 2, w - 6, h - roof_h + 2, ink);
    cv_rect(c, sx + 4, sy + roof_h - 2, w - 8, h - roof_h + 1, wall);
    for (int y = sy + roof_h + 3; y < sy + h - 1; y += 5) cv_rect(c, sx + 4, y, w - 8, 1, wall_d);
    for (int i = 0; i < roof_h; i++) {
        int inset = (roof_h - i) / 3;
        cv_rect(c, sx + inset - 1, sy + i, w - 2 * inset + 2, 1, ink);
        cv_rect(c, sx + inset, sy + i, w - 2 * inset, 1, (i % 6 == 5) ? roof_d : roof);
    }
    cv_rect(c, sx + 10, sy + 1, w - 20, 2, roof_l);
    cv_rect(c, sx - 1, sy + roof_h - 2, w + 2, 3, ink);
    cv_rect(c, sx, sy + roof_h - 2, w, 1, roof_d);
    int dx = sx + 2 * TILE + 2, dy = sy + h - 24;
    cv_rect(c, dx - 1, dy - 1, 14, 25, ink);
    cv_rect(c, dx, dy, 12, 24, RGB(0x7a4a24));
    cv_rect(c, dx + 2, dy + 2, 8, 9, RGB(0x8e5a2e));
    cv_rect(c, dx + 2, dy + 13, 8, 9, RGB(0x8e5a2e));
    cv_rect(c, dx + 9, dy + 12, 2, 2, RGB(0xf2d34b));
    for (int i = 0; i < 2; i++) {
        int wx = sx + (i ? w - 26 : 12), wy = sy + roof_h + 6;
        cv_rect(c, wx - 1, wy - 1, 16, 14, ink);
        cv_rect(c, wx, wy, 14, 12, night ? RGB(0xffd870) : RGB(0x9fd3ff));
        cv_rect(c, wx + 1, wy + 1, 4, 3, night ? RGB(0xfff0b0) : RGB(0xe8f6ff));
        cv_rect(c, wx + 6, wy, 2, 12, ink);
        cv_rect(c, wx, wy + 5, 14, 2, ink);
        cv_rect(c, wx - 2, wy + 12, 18, 3, RGB(0x6b4220));
        cv_rect(c, wx - 1, wy + 13, 3, 1, RGB(0xe84a8a));
        cv_rect(c, wx + 5, wy + 13, 3, 1, RGB(0xf2d34b));
        cv_rect(c, wx + 11, wy + 13, 3, 1, RGB(0xe84a8a));
    }
}

static void draw_bin(Canvas *c, int sx, int sy, int open)
{
    u32 ink = RGB(0x2b1d14);
    cv_rect(c, sx + 1, sy - 2, 30, 18, ink);
    cv_rect(c, sx + 2, sy + 2, 28, 13, RGB(0x9a6332));
    for (int x = sx + 9; x < sx + 30; x += 7) cv_rect(c, x, sy + 2, 1, 13, RGB(0x6b4220));
    cv_rect(c, sx + 2, sy + 8, 28, 1, RGB(0x6b4220));
    if (open) {
        cv_rect(c, sx + 1, sy - 8, 30, 6, ink);
        cv_rect(c, sx + 2, sy - 7, 28, 4, RGB(0xc48a4a));
    } else {
        cv_rect(c, sx, sy - 3, 32, 5, ink);
        cv_rect(c, sx + 1, sy - 2, 30, 3, RGB(0xc48a4a));
    }
    cv_rect(c, sx + 14, sy + 4, 4, 2, RGB(0xf2d34b));
}

static void draw_shop(Canvas *c, int sx, int sy)
{
    u32 ink = RGB(0x2b1d14);
    cv_rect(c, sx + 2, sy + 4, 4, 26, ink);
    cv_rect(c, sx + 42, sy + 4, 4, 26, ink);
    cv_rect(c, sx + 3, sy + 4, 2, 26, RGB(0x9a6332));
    cv_rect(c, sx + 43, sy + 4, 2, 26, RGB(0x9a6332));
    cv_rect(c, sx, sy + 16, 48, 16, ink);
    cv_rect(c, sx + 1, sy + 17, 46, 14, RGB(0xb07a44));
    cv_rect(c, sx + 1, sy + 17, 46, 3, RGB(0xd8a868));
    static const u32 goods[3] = {RGB(0xe03040), RGB(0xf2d34b), RGB(0x7ac850)};
    for (int i = 0; i < 3; i++) {
        int gx = sx + 6 + i * 13;
        cv_rect(c, gx, sy + 11, 10, 6, ink);
        cv_rect(c, gx + 1, sy + 12, 8, 4, goods[i]);
        cv_rect(c, gx + 2, sy + 12, 2, 1, C_WHITE);
    }
    cv_rect(c, sx - 2, sy - 6, 52, 12, ink);
    for (int i = 0; i < 50; i++) {
        u32 col = ((i / 6) % 2) ? RGB(0xf4f0e0) : RGB(0x3c9a5a);
        cv_rect(c, sx - 1 + i, sy - 5, 1, 10, col);
    }
    for (int i = 0; i < 50; i += 6) cv_rect(c, sx - 1 + i, sy + 5, 6, 2, ((i / 6) % 2) ? RGB(0xf4f0e0) : RGB(0x3c9a5a));
    cv_rect(c, sx + 10, sy + 22, 28, 9, ink);
    cv_rect(c, sx + 11, sy + 23, 26, 7, C_PAPER);
    cv_text(c, sx + 13, sy + 23, "SEED", C_INK, 1);
}

static void draw_shore(Canvas *c, Game *g, int tx, int ty, int sx, int sy)
{
    u32 sand = RGB(0xe4c890), foam = RGB(0xcfe8ff);
    Tile *n;
    if ((n = tile_at(g, tx, ty - 1)) && n->ground != G_WATER) {
        cv_rect(c, sx, sy, 16, 2, sand);
        cv_rect(c, sx, sy + 2, 16, 1, foam);
    }
    if ((n = tile_at(g, tx, ty + 1)) && n->ground != G_WATER) {
        cv_rect(c, sx, sy + 13, 16, 1, foam);
        cv_rect(c, sx, sy + 14, 16, 2, sand);
    }
    if ((n = tile_at(g, tx - 1, ty)) && n->ground != G_WATER) {
        cv_rect(c, sx, sy, 2, 16, sand);
        cv_rect(c, sx + 2, sy, 1, 16, foam);
    }
    if ((n = tile_at(g, tx + 1, ty)) && n->ground != G_WATER) {
        cv_rect(c, sx + 13, sy, 1, 16, foam);
        cv_rect(c, sx + 14, sy, 2, 16, sand);
    }
}

// Small "water me" drop bubble.
static void draw_drop(Canvas *c, int x, int y)
{
    u32 ink = RGB(0x1e3a6a), w = RGB(0x5aaaf0), hi = RGB(0xd8f0ff);
    cv_rect(c, x + 2, y, 1, 1, ink);
    cv_rect(c, x + 1, y + 1, 3, 1, ink);
    cv_rect(c, x, y + 2, 5, 4, ink);
    cv_rect(c, x + 1, y + 6, 3, 1, ink);
    cv_rect(c, x + 2, y + 1, 1, 1, w);
    cv_rect(c, x + 1, y + 2, 3, 4, w);
    cv_rect(c, x + 1, y + 3, 1, 1, hi);
}

static void draw_star(Canvas *c, int x, int y, u32 col)
{
    cv_rect(c, x, y - 2, 1, 5, col);
    cv_rect(c, x - 2, y, 5, 1, col);
    cv_pixel(c, x, y, C_WHITE);
}

// Round "A" button glyph used in prompts.
static void draw_a_button(Canvas *c, int x, int y, u32 fill)
{
    static const char *disc[11] = {"...###...", ".#######.", ".#######.", "#########", "#########", "#########",
                                   "#########", "#########", ".#######.", ".#######.", "...###..."};
    for (int j = 0; j < 11; j++)
        for (int i = 0; i < 9; i++)
            if (disc[j][i] == '#') cv_pixel(c, x + i, y + j, fill);
    cv_text(c, x + 2, y + 2, "A", C_WHITE, 1);
}

typedef struct {
    int base, kind, x, y;
} Drawable;
enum { D_TREE, D_HOUSE, D_BIN, D_SHOP, D_PLAYER };

static void draw_player(Canvas *c, Game *g, int camx, int camy)
{
    SaveData *s = &g->s;
    int px = (int)s->px - camx, py = (int)s->py - camy;
    int di = s->dir == DIR_DOWN ? 0 : s->dir == DIR_UP ? 1 : 2;
    int flip = s->dir == DIR_LEFT;
    static const int cycle[4] = {0, 1, 0, 2};
    int frame = g->moving ? cycle[(g->anim / 7) % 4] : 0;
    int bob = g->moving && (g->anim / 7) % 2 ? -1 : 0;

    cv_rect_alpha(c, px - 5, py - 2, 10, 3, RGB(0x000000), 70);

    int tool_x = 0, tool_y = 0, show_tool = g->swing > 0 && g->swing_tool >= 0;
    if (show_tool) {
        int up = g->swing > SWING_FRAMES / 2;
        switch (s->dir) {
        case DIR_DOWN: tool_x = px - 4; tool_y = up ? py - 26 : py - 10; break;
        case DIR_UP: tool_x = px - 10; tool_y = up ? py - 26 : py - 22; break;
        case DIR_LEFT: tool_x = up ? px - 12 : px - 20; tool_y = up ? py - 26 : py - 14; break;
        default: tool_x = up ? px - 4 : px + 4; tool_y = up ? py - 26 : py - 14; break;
        }
        if (s->dir == DIR_UP) cv_sprite(c, &S_ICON[g->swing_tool], tool_x, tool_y, flip);
    }
    cv_sprite(c, &S_PLAYER[di][frame], px - 8, py - 16 + bob, flip);
    if (show_tool && s->dir != DIR_UP) cv_sprite(c, &S_ICON[g->swing_tool], tool_x, tool_y, flip);
}

static void render_world(Game *g, Canvas *c)
{
    SaveData *s = &g->s;
    int camx = (int)g->camx, camy = (int)g->camy;
    int night = s->minutes >= NIGHT_MIN + 30;

    int t0x = camx / TILE, t0y = camy / TILE;
    int t1x = t0x + TOP_W / TILE + 1, t1y = t0y + SCREEN_H / TILE + 1;
    if (t1x >= MAP_W) t1x = MAP_W - 1;
    if (t1y >= MAP_H) t1y = MAP_H - 1;

    // Ground + flat objects
    for (int ty = t0y; ty <= t1y; ty++)
        for (int tx = t0x; tx <= t1x; tx++) {
            Tile *t = &s->map[ty][tx];
            int sx = tx * TILE - camx, sy = ty * TILE - camy;
            unsigned h = hash2(tx, ty);
            switch (t->ground) {
            case G_DIRT: cv_sprite(c, &S_DIRT[h & 1], sx, sy, 0); break;
            case G_TILLED: cv_sprite(c, t->watered ? &S_WATERED : &S_TILLED, sx, sy, 0); break;
            case G_WATER:
                cv_sprite(c, &S_WATER[(g->frame / 24 + (int)(h & 3)) & 3], sx, sy, 0);
                draw_shore(c, g, tx, ty, sx, sy);
                break;
            default: cv_sprite(c, &S_GRASS[(h % 13 == 0) ? 3 : (int)(h % 3)], sx, sy, 0); break;
            }
            switch (t->obj) {
            case O_WEED: cv_sprite(c, &S_WEED, sx, sy, (h >> 4) & 1); break;
            case O_STONE: cv_sprite(c, &S_STONE, sx, sy - 2, (h >> 4) & 1); break;
            case O_TWIG: cv_sprite(c, &S_TWIG, sx, sy - 2, (h >> 4) & 1); break;
            case O_SPRINKLER: cv_sprite(c, &S_SPRINKLER, sx, sy, 0); break;
            case O_FENCE: draw_fence(c, g, tx, ty, sx, sy); break;
            default: break;
            }
            if (t->crop) {
                if (t->dead) cv_sprite(c, &S_DEAD, sx, sy - 2, 0);
                else if (crop_ripe(t)) {
                    int bob = ((g->frame + (int)(h & 63)) / 20) % 2;
                    cv_sprite(c, &S_RIPE[t->crop - 1], sx, sy - 3 - bob, 0);
                } else cv_sprite(c, &S_STAGE[crop_stage(t)], sx, sy - 2, 0);
            }
        }

    // Target cursor
    Action act = {ACT_NONE, -1, -1, 0, 0, NULL};
    if (g->mode == MODE_PLAY) {
        act = get_action(g);
        int sx = act.tx * TILE - camx, sy = act.ty * TILE - camy;
        int on = act.kind != ACT_NONE && !is_hint(act.kind);
        u32 col = on ? C_HILITE : C_WHITE;
        int a = on ? 240 : 90, pulse = on && (g->frame / 15) % 2 ? 1 : 0;
        int x0 = sx - pulse, y0 = sy - pulse, x1 = sx + 15 + pulse, y1 = sy + 15 + pulse;
        cv_rect_alpha(c, x0, y0, 4, 2, col, a);
        cv_rect_alpha(c, x0, y0, 2, 4, col, a);
        cv_rect_alpha(c, x1 - 3, y0, 4, 2, col, a);
        cv_rect_alpha(c, x1 - 1, y0, 2, 4, col, a);
        cv_rect_alpha(c, x0, y1 - 1, 4, 2, col, a);
        cv_rect_alpha(c, x0, y1 - 3, 2, 4, col, a);
        cv_rect_alpha(c, x1 - 3, y1 - 1, 4, 2, col, a);
        cv_rect_alpha(c, x1 - 1, y1 - 3, 2, 4, col, a);
    }

    // Y-sorted tall things
    static Drawable list[1024];
    int n = 0;
    for (int ty = t0y; ty <= t1y + 1 && ty < MAP_H; ty++)
        for (int tx = t0x; tx <= t1x; tx++)
            if (s->map[ty][tx].obj == O_TREE && n < 1000) list[n++] = (Drawable){(ty + 1) * TILE, D_TREE, tx, ty};
    list[n++] = (Drawable){(HOUSE_Y + HOUSE_H) * TILE, D_HOUSE, HOUSE_X, HOUSE_Y};
    list[n++] = (Drawable){(BIN_Y + 1) * TILE, D_BIN, BIN_X, BIN_Y};
    list[n++] = (Drawable){(SHOP_Y + 2) * TILE, D_SHOP, SHOP_X, SHOP_Y};
    list[n++] = (Drawable){(int)s->py, D_PLAYER, 0, 0};
    for (int i = 1; i < n; i++) {
        Drawable d = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].base > d.base) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = d;
    }

    for (int i = 0; i < n; i++) {
        Drawable *d = &list[i];
        int sx = d->x * TILE - camx, sy = d->y * TILE - camy;
        switch (d->kind) {
        case D_TREE: {
            unsigned h = hash2(d->x, d->y);
            cv_rect_alpha(c, sx + 2, sy + 12, 12, 4, RGB(0x000000), 60);
            cv_sprite(c, &S_TREE, sx, sy + TILE - S_TREE.h, (h >> 3) & 1);
            break;
        }
        case D_HOUSE: draw_house(c, sx, sy, night); break;
        case D_BIN: draw_bin(c, sx, sy, act.kind == ACT_SHIP); break;
        case D_SHOP: draw_shop(c, sx, sy); break;
        case D_PLAYER: draw_player(c, g, camx, camy); break;
        }
    }

    // Crop status markers: drops on thirsty crops, twinkles on ripe ones
    for (int ty = t0y; ty <= t1y; ty++)
        for (int tx = t0x; tx <= t1x; tx++) {
            Tile *t = &s->map[ty][tx];
            if (!t->crop || t->dead) continue;
            int sx = tx * TILE - camx, sy = ty * TILE - camy;
            unsigned h = hash2(tx, ty);
            if (crop_ripe(t)) {
                int ph = (g->frame / 5 + (int)(h % 24)) % 24;
                if (ph < 4) draw_star(c, sx + 3 + (int)(h % 10), sy - 2 + (int)((h >> 5) % 8), ph < 2 ? C_HILITE : C_WHITE);
            } else if (!t->watered) {
                int bob = ((g->frame + (int)(h & 31)) / 16) % 2;
                draw_drop(c, sx + 10, sy - 8 - bob);
            }
        }

    // Particles, item pops and critters
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->parts[i];
        if (p->life > 0) cv_rect(c, (int)p->x - camx, (int)p->y - camy, p->size, p->size, p->col);
    }
    for (int i = 0; i < MAX_POPS; i++) {
        ItemPop *p = &g->pops[i];
        if (p->life > 0) cv_sprite(c, &S_ICON[p->item], (int)p->x - camx, (int)p->y - camy, 0);
    }
    int kind = critter_kind(s);
    for (int i = 0; i < MAX_CRITTERS && kind != CRIT_FIREFLY; i++) {
        Critter *k = &g->critters[i];
        if (k->t <= 0) continue;
        int x = (int)k->x - camx, y = (int)k->y - camy;
        if (kind == CRIT_LEAF) {
            cv_rect(c, x, y, 2, 1, k->col);
            cv_rect(c, x + 1, y + 1, 2, 1, k->col);
        } else {
            int flap = (g->frame / 4 + i) % 2;
            cv_rect(c, x, y, 1, 2, RGB(0x2b1d14));
            cv_rect(c, x - 2, y - flap, 2, 2, k->col);
            cv_rect(c, x + 1, y - flap, 2, 2, k->col);
        }
    }

    // Prompt bubble: "(A) Harvest" above the target
    if (act.kind != ACT_NONE && act.label && g->swing == 0) {
        int sx = act.tx * TILE - camx + 8, sy = act.ty * TILE - camy - 6;
        int w = text_width(act.label, 1) + 18, bx = sx - w / 2, by = sy - 16;
        // Keep the bubble off the farmer: below the target when facing down.
        if (s->dir == DIR_DOWN || by < 2) by = act.ty * TILE - camy + 18;
        if (by > SCREEN_H - 17) by = act.ty * TILE - camy - 22;
        if (bx < 2) bx = 2;
        if (bx + w > TOP_W - 2) bx = TOP_W - 2 - w;
        int hint = is_hint(act.kind);
        cv_panel(c, bx, by, w, 15, hint ? RGB(0xf0d8d0) : C_PAPER, C_WOOD_D);
        draw_a_button(c, bx + 3, by + 2, hint ? RGB(0xb05040) : C_GREEN);
        cv_text(c, bx + 14, by + 4, act.label, hint ? RGB(0x9a3020) : C_INK, 1);
    }
}

// Bouncing arrow that points at the current goal.
static void render_goal_arrow(Game *g, Canvas *c)
{
    int tx, ty;
    if (g->mode != MODE_PLAY || !quest_target(g, &tx, &ty)) return;
    int ftx, fty;
    facing_tile(g, &ftx, &fty);
    if (ftx == tx && fty == ty) return; // already there; the prompt takes over
    int camx = (int)g->camx, camy = (int)g->camy;
    int x = tx * TILE + 8 - camx, y = ty * TILE - camy - 10 + ((g->frame / 8) % 2) * 2;
    u32 fill = RGB(0xffd84a), ink = RGB(0x5a3418);
    int ty_screen = ty * TILE - camy;
    if (x >= 6 && x < TOP_W - 6 && ty_screen >= -8 && ty_screen < SCREEN_H - 8) {
        int up = y < 16; // no room above: point up at it from below instead
        if (up) y = ty_screen + TILE + 12 - ((g->frame / 8) % 2) * 2;
        for (int i = 0; i < 6; i++) {
            int row = up ? y + 8 - i : y - 8 + i;
            cv_rect(c, x - 6 + i - 1, row, 2 * (6 - i) + 2, 1, ink);
            cv_rect(c, x - 6 + i, row, 2 * (6 - i), 1, fill);
        }
        cv_rect(c, x - 3, up ? y + 9 : y - 15, 7, 8, ink);
        cv_rect(c, x - 2, up ? y + 9 : y - 14, 5, 7, fill);
        return;
    }
    // Off screen: a marker on the screen edge pointing toward the goal
    float dx = (float)(x - TOP_W / 2), dy = (float)(y - SCREEN_H / 2);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1) return;
    float kx = fabsf(dx) > 0.01f ? (TOP_W / 2 - 14) / fabsf(dx) : 1e9f;
    float ky = fabsf(dy) > 0.01f ? (SCREEN_H / 2 - 14) / fabsf(dy) : 1e9f;
    float k = kx < ky ? kx : ky;
    int ex = TOP_W / 2 + (int)(dx * k), ey = SCREEN_H / 2 + (int)(dy * k);
    float ux = dx / len, uy = dy / len;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 9; i++) {
            int w = (int)((9 - i) * 0.55f) + (pass ? 0 : 1);
            int cx = ex + (int)(ux * (i - 4)), cy = ey + (int)(uy * (i - 4));
            cv_rect(c, cx - w, cy - w, 2 * w + 1, 2 * w + 1, pass ? fill : ink);
        }
}

static void render_floaters(Game *g, Canvas *c)
{
    for (int i = 0; i < MAX_FLOATERS; i++) {
        Floater *f = &g->floats[i];
        if (f->life <= 0) continue;
        int x = (int)f->x - (int)g->camx - text_width(f->text, 1) / 2, y = (int)f->y - (int)g->camy;
        cv_text_shadow(c, x, y, f->text, f->col, C_SHADOW, 1);
    }
}

static void render_weather(Game *g, Canvas *c)
{
    if (g->s.weather == WEATHER_RAIN) {
        for (int i = 0; i < 90; i++) {
            int x = (int)((hash2(i, 7) % 440) + g->frame * 2) % 440 - 20;
            int y = (int)((hash2(i, 3) % 260) + g->frame * 7) % 260 - 10;
            cv_rect_alpha(c, x, y, 1, 5, RGB(0xb8d8ff), 170);
            cv_rect_alpha(c, x + 1, y + 4, 1, 2, RGB(0xb8d8ff), 120);
        }
    } else if (g->s.weather == WEATHER_SNOW) {
        for (int i = 0; i < 70; i++) {
            int wob = (int)((g->frame / 10 + i) % 4);
            wob = wob == 3 ? 1 : wob;
            int x = (int)((hash2(i, 9) % 420) + wob) % 420 - 10;
            int y = (int)((hash2(i, 5) % 260) + g->frame) % 260 - 10;
            cv_rect(c, x, y, 2, 2, RGB(0xffffff));
        }
    }
}

// Darkens the world for rain and nightfall in a single pass.
static void render_night(Game *g, Canvas *c)
{
    int m = g->s.minutes;
    float t = m <= 17 * 60 ? 0 : (m - 17 * 60) / 240.0f;
    if (t > 1) t = 1;
    float r = 1 - t * 0.55f, gr = 1 - t * 0.45f, b = 1 - t * 0.16f;
    if (g->s.weather == WEATHER_RAIN) {
        r *= 0.80f;
        gr *= 0.84f;
        b *= 0.94f;
    }
    if (t > 0 || g->s.weather == WEATHER_RAIN) cv_tint(c, (int)(r * 256), (int)(gr * 256), (int)(b * 256));
    if (critter_kind(&g->s) != CRIT_FIREFLY) return;
    for (int i = 0; i < MAX_CRITTERS; i++) {
        Critter *k = &g->critters[i];
        if (k->t <= 0 || ((g->frame / 9 + i * 5) % 12) < 3) continue;
        int x = (int)k->x - (int)g->camx, y = (int)k->y - (int)g->camy;
        cv_rect_alpha(c, x - 1, y - 1, 3, 3, RGB(0xd8ff70), 90);
        cv_pixel(c, x, y, RGB(0xf0ffb0));
    }
}

static void format_time(int minutes, char *buf, int len)
{
    int h = (minutes / 60) % 24, m = minutes % 60;
    const char *ap = h < 12 ? "AM" : "PM";
    int h12 = h % 12 == 0 ? 12 : h % 12;
    snprintf(buf, len, "%d:%02d %s", h12, (m / 10) * 10, ap);
}

static void render_clock_box(Game *g, Canvas *c)
{
    char t[16], d[24];
    format_time(g->s.minutes, t, sizeof(t));
    snprintf(d, sizeof(d), "%s %d", WEEKDAY_NAMES[(g->s.day - 1) % 7], g->s.day);
    cv_panel(c, TOP_W - 78, 4, 74, 30, C_PAPER, C_WOOD_D);
    cv_text(c, TOP_W - 72, 9, d, C_INK, 1);
    cv_text(c, TOP_W - 72, 21, t, g->s.minutes >= 24 * 60 ? RGB(0xc03020) : C_INK, 1);
}

static void render_toast(Game *g, Canvas *c)
{
    if (g->msg_timer <= 0 || !g->msg[0]) return;
    int w = text_width(g->msg, 1) + 20;
    int x = (TOP_W - w) / 2, y = SCREEN_H - 28;
    int slide = g->msg_timer > 140 ? (g->msg_timer - 140) * 3 : 0;
    cv_panel(c, x, y + slide, w, 22, C_PAPER, C_WOOD_D);
    cv_text(c, x + 10, y + 8 + slide, g->msg, C_INK, 1);
}

static void render_dialog(Canvas *c, const char *title, const char *const *opts, int count, int sel)
{
    int w = 240, h = 34 + count * 18;
    int x = (TOP_W - w) / 2, y = (SCREEN_H - h) / 2;
    cv_rect_alpha(c, 0, 0, TOP_W, SCREEN_H, RGB(0x000000), 100);
    cv_panel(c, x, y, w, h, C_PAPER, C_WOOD_D);
    cv_text_center(c, TOP_W / 2, y + 10, title, C_INK, 1);
    for (int i = 0; i < count; i++) {
        int oy = y + 28 + i * 18;
        if (i == sel) cv_rect(c, x + 30, oy - 3, w - 60, 13, C_PAPER2);
        cv_text_center(c, TOP_W / 2, oy, opts[i], C_INK, 1);
        if (i == sel) cv_text(c, x + 36, oy, ">", C_INK, 1);
    }
}

// ---------------------------------------------------------------------------
// Rendering: bottom screen

static void draw_bar(Canvas *c, int x, int y, int w, int val, int max, u32 col, const char *label, const char *value)
{
    cv_text(c, x, y + 2, label, C_INK, 1);
    int bx = x + text_width(label, 1) + 6;
    int bw = w - (bx - x);
    cv_rect(c, bx, y, bw, 11, C_SHADOW);
    cv_rect(c, bx + 1, y + 1, bw - 2, 9, RGB(0x6b5038));
    int fill = max > 0 ? (bw - 2) * val / max : 0;
    if (fill < 0) fill = 0;
    cv_rect(c, bx + 1, y + 1, fill, 9, col);
    cv_rect_alpha(c, bx + 1, y + 1, fill, 2, C_WHITE, 90);
    cv_text_shadow(c, bx + (bw - text_width(value, 1)) / 2, y + 2, value, C_WHITE, C_SHADOW, 1);
}

static void draw_weather_icon(Canvas *c, int x, int y, int weather)
{
    if (weather == WEATHER_SUN) {
        cv_rect(c, x + 3, y + 3, 8, 8, RGB(0xf2a020));
        cv_rect(c, x + 4, y + 4, 6, 6, RGB(0xf2d34b));
        cv_rect(c, x + 6, y, 2, 2, RGB(0xf2a020));
        cv_rect(c, x + 6, y + 12, 2, 2, RGB(0xf2a020));
        cv_rect(c, x, y + 6, 2, 2, RGB(0xf2a020));
        cv_rect(c, x + 12, y + 6, 2, 2, RGB(0xf2a020));
    } else {
        cv_rect(c, x + 2, y + 2, 10, 5, RGB(0x8a98a8));
        cv_rect(c, x + 1, y + 4, 12, 4, RGB(0xb0bcc8));
        u32 drop = weather == WEATHER_RAIN ? RGB(0x3f7fd6) : RGB(0xffffff);
        cv_rect(c, x + 3, y + 10, 1, 3, drop);
        cv_rect(c, x + 7, y + 11, 1, 3, drop);
        cv_rect(c, x + 11, y + 10, 1, 3, drop);
    }
}

static void bottom_background(Canvas *c)
{
    cv_clear(c, C_BG);
    for (int y = 0; y < SCREEN_H; y += 8) cv_rect(c, 0, y, BOT_W, 1, C_BG_LINE);
}

// Describes the crop in front of the player for the info panel.
static void facing_info(Game *g, char *buf, int len)
{
    int tx, ty;
    facing_tile(g, &tx, &ty);
    Tile *t = tile_at(g, tx, ty);
    buf[0] = 0;
    if (!t || !t->crop) return;
    const CropDef *cd = &CROPS[t->crop - 1];
    int left = cd->days - t->growth;
    if (t->dead) snprintf(buf, len, "%s: withered (wrong season)", cd->name);
    else if (left <= 0) snprintf(buf, len, "%s: ripe! Press A to harvest", cd->name);
    else snprintf(buf, len, "%s: %d day%s to go, %s", cd->name, left, left == 1 ? "" : "s",
                  t->watered ? "watered today" : "needs water!");
}

static void render_hud(Game *g, Canvas *c)
{
    SaveData *s = &g->s;
    char buf[80], t[16];
    bottom_background(c);

    // Date / time / gold
    cv_panel(c, 4, 4, BOT_W - 8, 36, C_PAPER, C_WOOD_D);
    snprintf(buf, sizeof(buf), "%s %d  -  Year %d", SEASON_NAMES[s->season], s->day, s->year);
    cv_text(c, 12, 10, buf, C_INK, 1);
    format_time(s->minutes, t, sizeof(t));
    snprintf(buf, sizeof(buf), "%s  %s", WEEKDAY_NAMES[(s->day - 1) % 7], t);
    cv_text(c, 12, 24, buf, C_INK, 1);
    draw_weather_icon(c, 116, 21, s->weather);
    snprintf(buf, sizeof(buf), "%dG", s->gold);
    int gw = text_width(buf, 2);
    cv_rect(c, BOT_W - 26 - gw, 15, 12, 12, RGB(0xa86a10));
    cv_rect(c, BOT_W - 25 - gw, 16, 10, 10, RGB(0xf2c030));
    cv_rect(c, BOT_W - 22 - gw, 18, 3, 6, RGB(0xfff0a0));
    cv_text(c, BOT_W - 12 - gw, 15, buf, C_INK, 2);

    // Energy / water
    cv_panel(c, 4, 43, BOT_W - 8, 23, C_PAPER, C_WOOD_D);
    snprintf(buf, sizeof(buf), "%d", s->energy);
    draw_bar(c, 12, 49, 140, s->energy, MAX_ENERGY, s->energy > 30 ? RGB(0x5ac83a) : RGB(0xe0503a), "ENERGY", buf);
    snprintf(buf, sizeof(buf), "%d/%d", s->water, can_capacity(s));
    draw_bar(c, 164, 49, 144, s->water, can_capacity(s), RGB(0x3f8fe6), "WATER", buf);

    // Current goal
    cv_panel(c, 4, 69, BOT_W - 8, 36, RGB(0xfff4d0), RGB(0xb07a20));
    if (s->quest < NUM_QUESTS) {
        const Quest *q = &QUESTS[s->quest];
        int prog = quest_progress(g);
        cv_rect(c, 11, 76, 7, 7, RGB(0xb07a20));
        cv_rect(c, 12, 77, 5, 5, RGB(0xf2c030));
        snprintf(buf, sizeof(buf), "GOAL: %s", q->title);
        cv_text(c, 22, 76, buf, C_INK, 1);
        if (q->target > 1) {
            snprintf(buf, sizeof(buf), "%d/%d", prog > q->target ? q->target : prog, q->target);
            cv_text(c, BOT_W - 14 - text_width(buf, 1), 76, buf, RGB(0xb07a20), 1);
        }
        cv_text(c, 22, 90, q->hint, C_INK_SOFT, 1);
    } else {
        cv_text(c, 14, 76, "ALL GOALS COMPLETE!", C_INK, 1);
        cv_text(c, 14, 90, "You're a master farmer. Keep growing!", C_INK_SOFT, 1);
    }

    // Backpack
    cv_panel(c, 4, INV_Y - 12, BOT_W - 8, INV_ROWS * INV_STEP + 14, C_PAPER2, C_WOOD_D);
    cv_text(c, 10, INV_Y - 9, "BACKPACK", C_INK_SOFT, 1);
    cv_text(c, BOT_W - 12 - text_width("TAP OR L/R TO SELECT", 1), INV_Y - 9, "TAP OR L/R TO SELECT", C_INK_SOFT, 1);
    int items[NUM_ITEMS], n = owned_items(g, items), scroll = inventory_scroll(g);
    for (int r = 0; r < INV_ROWS; r++)
        for (int col = 0; col < INV_COLS; col++) {
            int idx = scroll + r * INV_COLS + col;
            int x = INV_X + col * INV_STEP, y = INV_Y + r * INV_STEP;
            cv_rect(c, x, y, 36, 36, RGB(0xc8a468));
            cv_rect(c, x + 1, y + 1, 34, 34, RGB(0xf0d8a4));
            if (idx >= n) continue;
            int it = items[idx];
            if (it == s->sel) {
                cv_frame(c, x - 1, y - 1, 38, 38, RGB(0xd03020));
                cv_frame(c, x, y, 36, 36, RGB(0xd03020));
                cv_rect(c, x + 2, y + 2, 32, 32, RGB(0xfff0c8));
            }
            cv_sprite_scaled(c, &S_ICON[it], x + 2, y + 2, 2);
            snprintf(buf, sizeof(buf), "%d", s->inv[it]);
            cv_text_shadow(c, x + 35 - text_width(buf, 1), y + 27, buf, C_WHITE, C_SHADOW, 1);
        }
    if (!n) cv_text_center(c, BOT_W / 2, INV_Y + 32, "Empty - buy seeds at the stall!", C_INK_SOFT, 1);

    // Info / controls
    int iy = INV_Y + INV_ROWS * INV_STEP + 4;
    cv_panel(c, 4, iy, BOT_W - 8, SCREEN_H - iy - 4, C_PAPER, C_WOOD_D);
    facing_info(g, buf, sizeof(buf));
    if (!buf[0] && s->sel >= 0) {
        char nm[32];
        item_name(s->sel, nm, sizeof(nm));
        if (IS_CROP(s->sel)) snprintf(buf, sizeof(buf), "%s: sells for %dG. Y to eat.", nm, CROPS[s->sel - IT_CROP0].sell_price);
        else if (IS_SEED(s->sel)) {
            const CropDef *cd = &CROPS[s->sel - IT_SEED0];
            snprintf(buf, sizeof(buf), "%s: %d days%s", nm, cd->days, cd->regrow ? ", regrows" : "");
        } else snprintf(buf, sizeof(buf), "Sprinkler: select it, then A on grass.");
    }
    cv_text(c, 12, iy + 9, buf, C_INK, 1);
    cv_text(c, 12, iy + 25, "A: Do it   B: Run   Y: Eat   START: Menu", C_INK_SOFT, 1);
}

static void render_shop_bottom(Game *g, Canvas *c)
{
    SaveData *s = &g->s;
    char buf[64], name[32];
    ShopEntry e[NUM_ITEMS + 4];
    int n = shop_entries(g, e);

    cv_clear(c, RGB(0x3c9a5a));
    for (int x = 0; x < BOT_W; x += 24) cv_rect(c, x, 0, 12, SCREEN_H, RGB(0x358a50));
    cv_panel(c, 2, 2, BOT_W - 4, 26, C_PAPER, C_WOOD_D);
    cv_text(c, 10, 8, "SEED STALL", C_INK, 2);
    snprintf(buf, sizeof(buf), "%dG", s->gold);
    cv_text(c, 250 - text_width(buf, 1), 12, buf, C_INK, 1);
    cv_panel(c, 262, 5, 52, 20, RGB(0xe05a4a), C_WOOD_D);
    cv_text(c, 270, 11, "CLOSE", C_WHITE, 1);

    cv_panel(c, 2, SHOP_ROW_Y - 2, BOT_W - 4, SHOP_ROWS * SHOP_ROW_H + 4, C_PAPER, C_WOOD_D);
    for (int r = 0; r < SHOP_ROWS; r++) {
        int idx = g->shop_scroll + r;
        if (idx >= n) break;
        int y = SHOP_ROW_Y + r * SHOP_ROW_H;
        ShopEntry *it = &e[idx];
        if (idx == g->shop_sel) {
            cv_rect(c, 6, y, BOT_W - 12, SHOP_ROW_H - 1, C_HILITE);
            cv_frame(c, 6, y, BOT_W - 12, SHOP_ROW_H - 1, RGB(0xc08a20));
        }
        int icon = it->kind == SHOP_ITEM && IS_SEED(it->item) ? IT_CROP0 + it->item - IT_SEED0 : it->item;
        cv_sprite(c, &S_ICON[icon], 12, y + 3, 0);
        shop_entry_name(it, name, sizeof(name));
        cv_text(c, 34, y + 4, name, C_INK, 1);
        if (it->kind == SHOP_ITEM && IS_SEED(it->item)) {
            const CropDef *cd = &CROPS[it->item - IT_SEED0];
            snprintf(buf, sizeof(buf), "%d days%s, sells %dG", cd->days, cd->regrow ? " +regrows" : "", cd->sell_price * cd->yield);
        } else if (it->item == IT_SPRINKLER) snprintf(buf, sizeof(buf), "Waters 8 tiles around it");
        else if (it->kind == SHOP_CAN) snprintf(buf, sizeof(buf), "80 water, waters 3 tiles");
        else snprintf(buf, sizeof(buf), "Digs 3 tiles in a row");
        cv_text(c, 34, y + 13, buf, RGB(0x8a6a40), 1);
        snprintf(buf, sizeof(buf), "%dG", it->price);
        cv_text(c, BOT_W - 14 - text_width(buf, 1), y + 8, buf, s->gold >= it->price ? C_INK : RGB(0xc03020), 1);
        if (it->kind == SHOP_ITEM) {
            snprintf(buf, sizeof(buf), "x%d", s->inv[it->item]);
            cv_text(c, 214, y + 8, buf, RGB(0x8a6a40), 1);
        }
    }
    if (g->shop_scroll > 0) cv_text(c, BOT_W - 12, SHOP_ROW_Y, "^", C_INK, 1);
    if (g->shop_scroll + SHOP_ROWS < n) cv_text(c, BOT_W - 12, SHOP_ROW_Y + SHOP_ROWS * SHOP_ROW_H - 10, "V", C_INK, 1);
    cv_text_center(c, BOT_W / 2, SCREEN_H - 34, "A/Tap: Buy 1   X: Buy 5   B: Leave", C_WHITE, 1);
    if (g->msg_timer > 0) {
        cv_panel(c, 10, SCREEN_H - 22, BOT_W - 20, 18, C_PAPER, C_WOOD_D);
        cv_text_center(c, BOT_W / 2, SCREEN_H - 16, g->msg, C_INK, 1);
    }
}

static void render_menu_buttons(Canvas *c, const char *const *opts, int count, int sel)
{
    for (int i = 0; i < count; i++) {
        int y = MENU_BTN_Y + i * MENU_BTN_GAP;
        cv_panel(c, MENU_BTN_X, y, MENU_BTN_W, MENU_BTN_H, i == sel ? C_HILITE : C_PAPER, C_WOOD_D);
        cv_text_center(c, BOT_W / 2, y + 8, opts[i], C_INK, 2);
    }
}

static void render_title(Game *g, Canvas *top, Canvas *bot)
{
    for (int y = 0; y < SCREEN_H; y++) {
        int t = y * 256 / SCREEN_H;
        u32 r = 0x70 + (0xc8 - 0x70) * t / 256, gg = 0xb8 + (0xe8 - 0xb8) * t / 256, b = 0xf0;
        cv_rect(top, 0, y, TOP_W, 1, RGB((r << 16) | (gg << 8) | b));
    }
    for (int i = 0; i < 4; i++) {
        int x = (int)((i * 130 + g->frame / 3) % 520) - 60, y = 20 + i * 17 % 50;
        cv_rect(top, x, y + 4, 50, 10, C_WHITE);
        cv_rect(top, x + 10, y, 26, 18, C_WHITE);
    }
    for (int x = 0; x < TOP_W; x++) {
        int h1 = 150 + (int)(10 * sinf(x * 0.02f)), h2 = 170 + (int)(8 * sinf(x * 0.035f + 1));
        cv_rect(top, x, h1, 1, SCREEN_H - h1, RGB(0x55a428));
        cv_rect(top, x, h2, 1, SCREEN_H - h2, RGB(0x6abe30));
    }
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 13; i++) {
            int x = 8 + i * 30 + row * 15, y = 192 + row * 22;
            int bob = ((g->frame + i * 9) / 20) % 2;
            cv_sprite(top, &S_TILLED, x, y, 0);
            cv_sprite(top, &S_RIPE[(i + row * 5) % NUM_CROPS], x, y - 3 - bob, 0);
        }
    cv_sprite(top, &S_TREE, 20, 150, 0);
    cv_sprite(top, &S_TREE, 360, 152, 1);
    // The farmer strolls across the field
    int fx = (g->frame / 2) % (TOP_W + 40) - 20, step = (g->frame / 8) % 2;
    cv_sprite(top, &S_PLAYER[2][step], fx, 172 - step, 0);
    const char *t1 = "PIXEL FARM";
    int wob = (g->frame / 30) % 2;
    cv_text_shadow(top, (TOP_W - text_width(t1, 5)) / 2, 46 + wob, t1, RGB(0xffe070), RGB(0x5a3418), 5);
    cv_text_center(top, TOP_W / 2, 98, "A COZY LITTLE FARMING GAME", RGB(0x3a2414), 1);
    if ((g->frame / 30) % 2) cv_text_center(top, TOP_W / 2, 124, "PRESS A", C_WHITE, 2);

    bottom_background(bot);
    cv_text_center(bot, BOT_W / 2, 16, "WELCOME, FARMER!", C_INK, 2);
    const char *opts_save[] = {"Continue", "New Farm", "How to Play"};
    const char *opts_new[] = {"New Farm", "How to Play"};
    if (g->has_save) render_menu_buttons(bot, opts_save, 3, g->menu_sel);
    else render_menu_buttons(bot, opts_new, 2, g->menu_sel);
    cv_panel(bot, 8, 168, BOT_W - 16, 64, C_PAPER, C_WOOD_D);
    cv_text(bot, 16, 175, "Move: Circle Pad      Run: hold B", C_INK, 1);
    cv_text(bot, 16, 187, "A: does the right job for the tile", C_INK, 1);
    cv_text(bot, 16, 199, "   in front of you - dig, plant,", C_INK, 1);
    cv_text(bot, 16, 211, "   water, harvest, ship, shop, sleep", C_INK, 1);
}

static const char *HELP_TITLES[HELP_PAGES] = {"THE BASICS", "GROWING CROPS", "GOLD & SEASONS"};
static const char *HELP_TEXT[HELP_PAGES][9] = {
    {"Move with the Circle Pad. Hold B to run.", "", "Just walk up to something and press A.",
     "The A button picks the right tool for you:", " - weeds & twigs: cut them", " - stones: break them",
     " - empty grass: dig it into soil", "", "Hold A to keep working tile after tile!"},
    {"1. Dig: press A on empty grass.", "2. Plant: press A on the dug soil.", "3. Water: press A on seeds with a drop.",
     "   Refill your can at the pond.", "4. Sleep: press A at your house door.", "   Watered crops grow one day each night.",
     "5. Harvest: ripe crops bob and sparkle.", "   Press A on them to pick them!", ""},
    {"Ship crops at the bin next to your house.", "You get paid overnight.", "",
     "Buy seeds and upgrades at the seed stall.", "Each season lasts 28 days. Crops die", "when their season ends - check the stall!",
     "Rain waters every crop for you.", "Eat crops (Y) to get energy back.", "Be in bed before 2 AM or you'll pass out!"},
};

static void render_help(Game *g, Canvas *top, Canvas *bot)
{
    for (int y = 0; y < SCREEN_H; y += 16)
        for (int x = 0; x < TOP_W; x += 16) cv_sprite(top, &S_GRASS[hash2(x, y) % 3], x, y, 0);
    cv_panel(top, 16, 10, TOP_W - 32, SCREEN_H - 20, C_PAPER, C_WOOD_D);
    char buf[32];
    snprintf(buf, sizeof(buf), "HOW TO PLAY  %d/%d", g->help_page + 1, HELP_PAGES);
    cv_text_center(top, TOP_W / 2, 20, HELP_TITLES[g->help_page], C_INK, 2);
    cv_text_center(top, TOP_W / 2, 40, buf, C_INK_SOFT, 1);
    for (int i = 0; i < 9; i++) cv_text(top, 36, 60 + i * 18, HELP_TEXT[g->help_page][i], C_INK, 1);

    bottom_background(bot);
    cv_panel(bot, 16, 16, BOT_W - 32, 104, C_PAPER, C_WOOD_D);
    // The crop cycle at a glance
    static const char *labels[5] = {"DIG", "PLANT", "WATER", "SLEEP", "HARVEST"};
    for (int i = 0; i < 5; i++) {
        int x = 30 + i * 56;
        if (i == 3) {
            static const char *moon[6] = {"..##", ".##.", "##..", "##..", ".##.", "..##"};
            cv_rect(bot, x, 34, 32, 32, RGB(0x141c3a));
            for (int j = 0; j < 6; j++)
                for (int k = 0; k < 4; k++)
                    if (moon[j][k] == '#') cv_rect(bot, x + 20 + k * 2, 37 + j * 2, 2, 2, RGB(0xfff0b0));
            cv_text(bot, x + 4, 52, "ZZZ", C_WHITE, 1);
        } else {
            cv_sprite_scaled(bot, i == 2 ? &S_WATERED : &S_TILLED, x, 34, 2);
            if (i == 1 || i == 2) cv_sprite_scaled(bot, &S_STAGE[i == 1 ? STAGE_SEED : STAGE_SPROUT], x, 30, 2);
            if (i == 4) cv_sprite_scaled(bot, &S_RIPE[0], x, 28, 2);
        }
        cv_text_center(bot, x + 16, 76, labels[i], C_INK, 1);
        if (i < 4) cv_text(bot, x + 40, 46, ">", C_INK_SOFT, 1);
    }
    cv_text_center(bot, BOT_W / 2, 96, "Repeat WATER + SLEEP until ripe!", C_INK_SOFT, 1);
    cv_text_center(bot, BOT_W / 2, 150, g->help_page < HELP_PAGES - 1 ? "A / Tap: Next page" : "A: Done", C_INK, 2);
    cv_text_center(bot, BOT_W / 2, 180, "B: Back", C_INK_SOFT, 1);
}

static void render_summary(Game *g, Canvas *top, Canvas *bot)
{
    char buf[64];
    cv_clear(top, RGB(0x141c3a));
    for (int i = 0; i < 70; i++) {
        unsigned h = hash2(i, 42);
        int tw = ((g->frame / 20 + i) % 7) == 0;
        cv_rect(top, (int)(h % TOP_W), (int)((h >> 9) % 170), tw ? 2 : 1, tw ? 2 : 1, RGB(0xfff6c0));
    }
    for (int y = -13; y <= 13; y++)
        for (int x = -13; x <= 13; x++) {
            int lit = x * x + y * y <= 169, cut = (x - 7) * (x - 7) + (y + 3) * (y + 3) <= 121;
            if (lit && !cut) cv_pixel(top, 330 + x, 44 + y, RGB(0xfff0b0));
        }
    for (int x = 0; x < TOP_W; x++) {
        int h = 190 + (int)(8 * sinf(x * 0.03f));
        cv_rect(top, x, h, 1, SCREEN_H - h, RGB(0x1c3020));
    }
    snprintf(buf, sizeof(buf), "DAY %d COMPLETE", g->sum_day);
    cv_text_shadow(top, (TOP_W - text_width(buf, 3)) / 2, 80, buf, RGB(0xffe070), RGB(0x000000), 3);
    if (g->passed_out) cv_text_center(top, TOP_W / 2, 120, "You collapsed from exhaustion...", RGB(0xffa080), 1);
    snprintf(buf, sizeof(buf), "Next: %s %d, %s", SEASON_NAMES[g->s.season], g->s.day,
             g->s.weather == WEATHER_RAIN ? "rainy" : g->s.weather == WEATHER_SNOW ? "snowy" : "sunny");
    cv_text_center(top, TOP_W / 2, 140, buf, RGB(0xd0d8ff), 1);
    cv_text_center(top, TOP_W / 2, 160, "Game saved.", RGB(0x90a0c0), 1);

    bottom_background(bot);
    cv_panel(bot, 8, 8, BOT_W - 16, SCREEN_H - 16, C_PAPER, C_WOOD_D);
    cv_text_center(bot, BOT_W / 2, 18, "SHIPPING REPORT", C_INK, 2);
    int y = 44, any = 0;
    for (int i = 0; i < NUM_CROPS; i++) {
        if (!g->sum_items[i]) continue;
        any = 1;
        cv_sprite(bot, &S_ICON[IT_CROP0 + i], 24, y - 4, 0);
        snprintf(buf, sizeof(buf), "%s x%d", CROPS[i].name, g->sum_items[i]);
        cv_text(bot, 46, y, buf, C_INK, 1);
        snprintf(buf, sizeof(buf), "%dG", g->sum_items[i] * CROPS[i].sell_price);
        cv_text(bot, BOT_W - 24 - text_width(buf, 1), y, buf, C_INK, 1);
        y += 18;
        if (y > 170) break;
    }
    if (!any) cv_text_center(bot, BOT_W / 2, 60, "Nothing shipped today.", RGB(0x8a6a40), 1);
    cv_rect(bot, 20, 186, BOT_W - 40, 1, C_INK);
    snprintf(buf, sizeof(buf), "TOTAL: %dG", g->sum_earn);
    cv_text_center(bot, BOT_W / 2, 194, buf, C_INK, 2);
    cv_text_center(bot, BOT_W / 2, 216, "Press A to start the day", RGB(0x8a4a20), 1);
}

static void render_fade(Game *g, Canvas *top, Canvas *bot)
{
    if (g->fade <= 0) return;
    int a = g->fade > 255 ? 255 : g->fade;
    cv_rect_alpha(top, 0, 0, TOP_W, SCREEN_H, RGB(0x000000), a);
    cv_rect_alpha(bot, 0, 0, BOT_W, SCREEN_H, RGB(0x000000), a);
}

void game_render(Game *g, Canvas *top, Canvas *bot)
{
    if (g->mode == MODE_TITLE) {
        render_title(g, top, bot);
        return;
    }
    if (g->mode == MODE_HELP) {
        render_help(g, top, bot);
        return;
    }
    if (g->mode == MODE_SUMMARY) {
        render_summary(g, top, bot);
        render_fade(g, top, bot);
        return;
    }

    render_world(g, top);
    render_goal_arrow(g, top);
    render_weather(g, top);
    render_night(g, top);
    render_floaters(g, top);
    render_clock_box(g, top);
    if (g->mode != MODE_SHOP) render_toast(g, top);

    if (g->mode == MODE_SHOP) {
        cv_rect_alpha(top, 0, 0, TOP_W, SCREEN_H, RGB(0x000000), 80);
        cv_panel(top, 100, 90, 200, 50, C_PAPER, C_WOOD_D);
        cv_text_center(top, TOP_W / 2, 100, "SEED STALL", C_INK, 2);
        cv_text_center(top, TOP_W / 2, 122, "Fresh seeds, every season!", RGB(0x8a6a40), 1);
        render_shop_bottom(g, bot);
    } else if (g->mode == MODE_PAUSE) {
        const char *opts[] = {"Resume", "How to Play", g->s.music_on ? "Music: On" : "Music: Off", "Save Game", "Save & Quit"};
        render_dialog(top, "- PAUSED -", opts, 5, g->menu_sel);
        bottom_background(bot);
        cv_text_center(bot, BOT_W / 2, 16, "MENU", C_INK, 2);
        render_menu_buttons(bot, opts, 5, g->menu_sel);
    } else if (g->mode == MODE_ASK_SLEEP) {
        static const char *opts[] = {"Yes, sleep", "Not yet"};
        render_dialog(top, "Go to bed and end the day?", opts, 2, g->menu_sel);
        bottom_background(bot);
        cv_text_center(bot, BOT_W / 2, 16, "SLEEP?", C_INK, 2);
        render_menu_buttons(bot, opts, 2, g->menu_sel);
        cv_text_center(bot, BOT_W / 2, 150, "Watered crops grow overnight.", C_INK, 1);
        cv_text_center(bot, BOT_W / 2, 164, "Shipped crops are paid out.", C_INK, 1);
    } else render_hud(g, bot);
    render_fade(g, top, bot);
}
