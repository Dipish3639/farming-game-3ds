#include "game.h"
#include "sprites.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_MAGIC 0x4D524146u // "FARM"
#define SAVE_VERSION 1

#define FRAMES_PER_MIN 36 // 10 in-game minutes every 6 seconds
#define START_MIN (6 * 60)
#define END_MIN (26 * 60) // 2:00 AM
#define MAX_ENERGY 100
#define DAYS_PER_SEASON 28

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
#define C_PAPER RGB(0xf6e7c1)
#define C_PAPER2 RGB(0xe9cf98)
#define C_WOOD RGB(0x8a5430)
#define C_WOOD_D RGB(0x5a3418)
#define C_HILITE RGB(0xffd84a)
#define C_WHITE RGB(0xffffff)
#define C_SHADOW RGB(0x2b1d14)

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
    static const int dx[4] = {0, 0, -1, 1}, dy[4] = {1, -1, 0, 0};
    int x = (int)floorf(g->s.px / TILE), y = (int)floorf((g->s.py - 4) / TILE);
    *tx = x + dx[g->s.dir];
    *ty = y + dy[g->s.dir];
}

// Items the player currently has, in backpack order.
static int owned_items(Game *g, int *out)
{
    int n = 0;
    for (int i = 0; i < NUM_ITEMS; i++)
        if (g->s.inv[i] > 0) out[n++] = i;
    return n;
}

static void fix_selection(Game *g)
{
    if (g->s.inv[g->s.sel] > 0) return;
    int items[NUM_ITEMS], n = owned_items(g, items);
    int best = items[0];
    for (int i = 0; i < n; i++)
        if (items[i] < g->s.sel) best = items[i];
    g->s.sel = best;
}

static void cycle_selection(Game *g, int dir)
{
    int items[NUM_ITEMS], n = owned_items(g, items), cur = 0;
    for (int i = 0; i < n; i++)
        if (items[i] == g->s.sel) cur = i;
    g->s.sel = items[(cur + dir + n) % n];
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

    // Yard paths
    for (int x = 2; x <= 20; x++) s->map[HOUSE_Y + HOUSE_H][x].ground = G_DIRT;
    for (int x = 3; x <= 18; x++) s->map[HOUSE_Y + HOUSE_H + 1][x].ground = G_DIRT;
    for (int y = HOUSE_Y + HOUSE_H; y <= 10; y++) s->map[y][DOOR_X].ground = G_DIRT;

    for (int j = 0; j < HOUSE_H; j++)
        for (int i = 0; i < HOUSE_W; i++) s->map[HOUSE_Y + j][HOUSE_X + i].obj = O_HOUSE;
    s->map[BIN_Y][BIN_X].obj = O_BIN;
    s->map[BIN_Y][BIN_X + 1].obj = O_BIN;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 3; i++) s->map[SHOP_Y + j][SHOP_X + i].obj = O_SHOP;

    // Fence separating the yard from the fields (gap near the path)
    for (int x = 9; x <= 22; x++) s->map[9][x].obj = O_FENCE;
    for (int y = 2; y <= 9; y++) s->map[y][22].obj = O_FENCE;
    s->map[6][22].obj = O_NONE;
    s->map[7][22].obj = O_NONE;

    // Pond
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            float fx = (x - 39.5f) / 5.5f, fy = (y - 22.5f) / 3.6f;
            if (fx * fx + fy * fy <= 1.0f) {
                s->map[y][x].ground = G_WATER;
                s->map[y][x].obj = O_NONE;
            }
        }

    // A grove of trees in the north east
    for (int y = 2; y < 14; y++)
        for (int x = 26; x < MAP_W - 1; x++)
            if (rng_range(s, 100) < 18 + (x - 26) * 2) s->map[y][x].obj = O_TREE;
    for (int x = 25; x < 30; x++) s->map[7][x].obj = O_NONE;

    // Scatter debris over the overgrown farmland
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
    // A clear starter patch next to the path
    for (int y = 11; y <= 15; y++)
        for (int x = 3; x <= 11; x++) s->map[y][x].obj = O_NONE;
}

static void new_game(Game *g)
{
    SaveData *s = &g->s;
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
    s->sel = IT_HOE;
    s->px = DOOR_X * TILE + 8;
    s->py = (DOOR_Y + 1) * TILE + 14;
    s->dir = DIR_DOWN;
    spr_set_season(s->season);
    g->loaded_season = s->season;
    show_msg(g, "Welcome to your farm!");
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

    // Crops on watered soil grow overnight.
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
                if (t->crop && !(CROPS[t->crop - 1].seasons & SB(s->season))) t->dead = 1;
            }
    }

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            Tile *t = &s->map[y][x];
            // Bare, dry soil slowly turns back into grass.
            if (t->ground == G_TILLED && !t->crop && !t->watered && rng_range(s, 100) < 8) t->ground = G_GRASS;
            t->watered = 0;
        }

    // Weeds creep back onto empty grass.
    if (s->season != SEASON_WINTER)
        for (int i = 0; i < 20; i++) {
            int x = 1 + rng_range(s, MAP_W - 2), y = 10 + rng_range(s, MAP_H - 11);
            Tile *t = &s->map[y][x];
            if (t->ground == G_GRASS && t->obj == O_NONE) t->obj = O_WEED;
        }

    int r = rng_range(s, 100);
    if (s->day == 1) s->weather = WEATHER_SUN;
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

    if (g->loaded_season != s->season) {
        spr_set_season(s->season);
        g->loaded_season = s->season;
    }
    save_game(g);
    g->mode = MODE_SUMMARY;
}

// ---------------------------------------------------------------------------
// Farming actions

static int spend_energy(Game *g, int cost)
{
    if (g->s.energy < cost) {
        show_msg(g, "You're too tired. Go to bed!");
        return 0;
    }
    g->s.energy -= cost;
    return 1;
}

static void start_swing(Game *g)
{
    g->swing = 16;
    g->swing_item = g->s.sel;
}

static int tillable(const Tile *t)
{
    return t && (t->ground == G_GRASS || t->ground == G_DIRT) && t->obj == O_NONE;
}

// Works out what pressing A does on the facing tile. Returns a short label
// for the HUD, and performs the action when apply is set.
static const char *interact(Game *g, int apply)
{
    static char label[48];
    SaveData *s = &g->s;
    int tx, ty;
    facing_tile(g, &tx, &ty);
    Tile *t = tile_at(g, tx, ty);
    int sel = s->sel;

    if (!t) return NULL;

    if (tx == DOOR_X && ty == DOOR_Y) {
        if (apply) {
            g->mode = MODE_ASK_SLEEP;
            g->menu_sel = 0;
        }
        return "Go to bed";
    }
    if (t->obj == O_BIN) {
        if (IS_CROP(sel)) {
            if (apply) {
                int c = sel - IT_CROP0, n = s->inv[sel];
                s->shipped[c] += n;
                s->inv[sel] = 0;
                snprintf(label, sizeof(label), "Shipped %d %s. +%dG tonight", n, CROPS[c].name, n * CROPS[c].sell_price);
                show_msg(g, label);
                fix_selection(g);
            }
            return "Ship crops";
        }
        if (apply) show_msg(g, "Select crops to ship them.");
        return "Shipping bin";
    }
    if (t->obj == O_SHOP) {
        if (apply) {
            g->mode = MODE_SHOP;
            g->shop_sel = 0;
            g->shop_scroll = 0;
        }
        return "Shop";
    }
    if (t->obj == O_HOUSE) return NULL;

    if (crop_ripe(t)) {
        if (apply) {
            int c = t->crop - 1, n = CROPS[c].yield + (rng_range(s, 10) == 0 ? 1 : 0);
            s->inv[IT_CROP0 + c] += n;
            if (CROPS[c].regrow) t->growth = (uint8_t)(CROPS[c].days - CROPS[c].regrow);
            else {
                t->crop = 0;
                t->growth = 0;
            }
            snprintf(label, sizeof(label), "+%d %s", n, CROPS[c].name);
            show_msg(g, label);
        }
        return "Harvest";
    }

    switch (sel) {
    case IT_HOE:
        if (tillable(t)) {
            if (apply && spend_energy(g, s->hoe_lvl ? 3 : 2)) {
                static const int dx[4] = {0, 0, -1, 1}, dy[4] = {1, -1, 0, 0};
                int n = s->hoe_lvl ? 3 : 1;
                for (int i = 0; i < n; i++) {
                    Tile *u = tile_at(g, tx + dx[s->dir] * i, ty + dy[s->dir] * i);
                    if (!tillable(u)) break;
                    u->ground = G_TILLED;
                    u->watered = s->weather == WEATHER_RAIN;
                }
                start_swing(g);
            }
            return "Till soil";
        }
        break;
    case IT_CAN:
        if (t->ground == G_WATER) {
            if (apply) {
                s->water = can_capacity(s);
                show_msg(g, "Watering can refilled!");
                start_swing(g);
            }
            return "Refill can";
        }
        if (t->ground == G_TILLED) {
            if (apply) {
                if (s->water <= 0) {
                    show_msg(g, "The can is empty. Refill at the pond.");
                } else if (spend_energy(g, 1)) {
                    static const int dx[4] = {0, 0, -1, 1}, dy[4] = {1, -1, 0, 0};
                    int n = s->can_lvl ? 3 : 1;
                    for (int i = 0; i < n && s->water > 0; i++) {
                        Tile *u = tile_at(g, tx + dx[s->dir] * i, ty + dy[s->dir] * i);
                        if (!u || u->ground != G_TILLED) break;
                        u->watered = 1;
                        s->water--;
                    }
                    start_swing(g);
                }
            }
            return "Water";
        }
        break;
    case IT_SCYTHE:
        if (t->obj == O_WEED || t->obj == O_TWIG) {
            const char *what = t->obj == O_WEED ? "Cut weeds" : "Clear twig";
            if (apply) {
                t->obj = O_NONE;
                start_swing(g);
            }
            return what;
        }
        if (t->crop && t->dead) {
            if (apply) {
                t->crop = 0;
                t->dead = 0;
                t->growth = 0;
                start_swing(g);
            }
            return "Clear dead crop";
        }
        break;
    case IT_PICK:
        if (t->obj == O_STONE) {
            if (apply && spend_energy(g, 2)) {
                t->obj = O_NONE;
                start_swing(g);
            }
            return "Break stone";
        }
        if (t->obj == O_SPRINKLER) {
            if (apply) {
                t->obj = O_NONE;
                s->inv[IT_SPRINKLER]++;
                start_swing(g);
            }
            return "Pick up sprinkler";
        }
        if (t->crop && t->dead) {
            if (apply) {
                t->crop = 0;
                t->dead = 0;
                start_swing(g);
            }
            return "Clear dead crop";
        }
        if (t->ground == G_TILLED && !t->crop && t->obj == O_NONE) {
            if (apply && spend_energy(g, 1)) {
                t->ground = G_DIRT;
                t->watered = 0;
                start_swing(g);
            }
            return "Flatten soil";
        }
        break;
    case IT_SPRINKLER:
        if (t->obj == O_NONE && !t->crop && t->ground != G_WATER) {
            if (apply) {
                t->obj = O_SPRINKLER;
                if (t->ground == G_TILLED) t->ground = G_DIRT;
                t->watered = 0;
                s->inv[IT_SPRINKLER]--;
                show_msg(g, "Sprinkler placed. It waters every morning.");
                fix_selection(g);
            }
            return "Place sprinkler";
        }
        break;
    default:
        if (IS_SEED(sel) && t->ground == G_TILLED && !t->crop && t->obj == O_NONE) {
            int c = sel - IT_SEED0;
            if (apply) {
                if (!(CROPS[c].seasons & SB(s->season))) {
                    snprintf(label, sizeof(label), "%s won't grow in %s.", CROPS[c].name, SEASON_NAMES[s->season]);
                    show_msg(g, label);
                } else {
                    t->crop = (uint8_t)(c + 1);
                    t->growth = 0;
                    t->dead = 0;
                    s->inv[sel]--;
                    fix_selection(g);
                }
            }
            return "Plant";
        }
        break;
    }
    if (apply && IS_TOOL(sel) && sel != IT_CAN) start_swing(g);
    return NULL;
}

static void eat_selected(Game *g)
{
    SaveData *s = &g->s;
    if (!IS_CROP(s->sel) || s->inv[s->sel] <= 0) return;
    if (s->energy >= MAX_ENERGY) {
        show_msg(g, "You're not hungry right now.");
        return;
    }
    int c = s->sel - IT_CROP0, gain = CROPS[c].sell_price / 4;
    if (gain < 10) gain = 10;
    if (gain > 50) gain = 50;
    s->energy += gain;
    if (s->energy > MAX_ENERGY) s->energy = MAX_ENERGY;
    s->inv[s->sel]--;
    char buf[64];
    snprintf(buf, sizeof(buf), "Ate %s. +%d energy", CROPS[c].name, gain);
    show_msg(g, buf);
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
        if (CROPS[c].seasons & SB(s->season)) out[n++] = (ShopEntry){SHOP_ITEM, IT_SEED0 + c, CROPS[c].seed_price};
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
        show_msg(g, "Big can! Holds more and waters 3 tiles.");
    } else if (it->kind == SHOP_HOE) {
        s->hoe_lvl = 1;
        show_msg(g, "Steel hoe! Tills 3 tiles at once.");
    } else {
        s->inv[it->item] += bought;
        snprintf(buf, sizeof(buf), "Bought %d %s", bought, name);
        show_msg(g, buf);
    }
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
    if (in->down & BTN_B) {
        g->mode = MODE_PLAY;
        return;
    }
    if (in->down & BTN_UP) g->shop_sel = (g->shop_sel + n - 1) % n;
    if (in->down & BTN_DOWN) g->shop_sel = (g->shop_sel + 1) % n;
    if (in->down & BTN_A) shop_buy(g, 1);
    if (in->down & BTN_X) shop_buy(g, 5);
    if (in->touch_down) {
        if (in->tx >= 262 && in->ty < 26) {
            g->mode = MODE_PLAY;
            return;
        }
        int row = (in->ty - SHOP_ROW_Y) / SHOP_ROW_H;
        if (in->ty >= SHOP_ROW_Y && row < SHOP_ROWS) {
            int idx = g->shop_scroll + row;
            if (idx < n) {
                if (idx == g->shop_sel) shop_buy(g, 1);
                else g->shop_sel = idx;
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
#define INV_ROWS 3
#define INV_X 10
#define INV_Y 72
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
    if (idx < n) g->s.sel = items[idx];
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
        return;
    }
    if (in->touch_down) inventory_touch(g, in->tx, in->ty);
    if (in->down & BTN_L) cycle_selection(g, -1);
    if (in->down & BTN_R) cycle_selection(g, 1);

    if (++g->minute_acc >= FRAMES_PER_MIN) {
        g->minute_acc = 0;
        s->minutes++;
        if (s->minutes >= END_MIN) {
            end_day(g, 1);
            return;
        }
    }
    if (g->msg_timer > 0) g->msg_timer--;

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
        float sp = (in->held & BTN_B) ? 1.8f : 1.1f;
        if (dx && dy) sp *= 0.7071f;
        float nx = s->px + dx * sp, ny = s->py + dy * sp;
        if (box_free(g, nx, s->py)) s->px = nx;
        if (box_free(g, s->px, ny)) s->py = ny;
        g->moving = 1;
        g->anim++;
    } else {
        g->moving = 0;
        g->anim = 0;
    }

    if (in->down & BTN_A) interact(g, 1);
    if (in->down & BTN_Y) eat_selected(g);
}

static int menu_nav(Game *g, const Input *in, int count)
{
    if (in->down & BTN_UP) g->menu_sel = (g->menu_sel + count - 1) % count;
    if (in->down & BTN_DOWN) g->menu_sel = (g->menu_sel + 1) % count;
    return (in->down & BTN_A) != 0;
}

// Bottom-screen menu buttons, shared by title/pause/sleep menus.
#define MENU_BTN_X 60
#define MENU_BTN_W 200
#define MENU_BTN_Y 70
#define MENU_BTN_H 34
#define MENU_BTN_GAP 44

static int menu_touch(const Input *in, int count)
{
    if (!in->touch_down || in->tx < MENU_BTN_X || in->tx >= MENU_BTN_X + MENU_BTN_W) return -1;
    for (int i = 0; i < count; i++) {
        int y = MENU_BTN_Y + i * MENU_BTN_GAP;
        if (in->ty >= y && in->ty < y + MENU_BTN_H) return i;
    }
    return -1;
}

void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    spr_init();
    SaveData tmp;
    g->has_save = load_game(&tmp);
    g->mode = MODE_TITLE;
    g->loaded_season = SEASON_SPRING;
}

void game_update(Game *g, const Input *in)
{
    g->frame++;
    int pick;
    switch (g->mode) {
    case MODE_TITLE: {
        int count = g->has_save ? 2 : 1;
        pick = menu_nav(g, in, count) ? g->menu_sel : menu_touch(in, count);
        if (pick < 0) break;
        if (g->has_save && pick == 0) {
            SaveData tmp;
            if (load_game(&tmp)) {
                g->s = tmp;
                spr_set_season(g->s.season);
                g->loaded_season = g->s.season;
                fix_selection(g);
                show_msg(g, "Welcome back!");
            } else new_game(g);
        } else new_game(g);
        g->mode = MODE_PLAY;
        break;
    }
    case MODE_PLAY:
        update_play(g, in);
        break;
    case MODE_SHOP:
        update_shop(g, in);
        break;
    case MODE_PAUSE:
        if (in->down & (BTN_B | BTN_START)) {
            g->mode = MODE_PLAY;
            break;
        }
        pick = menu_nav(g, in, 3) ? g->menu_sel : menu_touch(in, 3);
        if (pick == 0) g->mode = MODE_PLAY;
        else if (pick == 1) {
            show_msg(g, save_game(g) ? "Game saved." : "Could not save!");
            g->mode = MODE_PLAY;
        } else if (pick == 2) {
            save_game(g);
            g->quit = 1;
        }
        break;
    case MODE_ASK_SLEEP:
        if (in->down & BTN_B) {
            g->mode = MODE_PLAY;
            break;
        }
        pick = menu_nav(g, in, 2) ? g->menu_sel : menu_touch(in, 2);
        if (pick == 0) end_day(g, 0);
        else if (pick == 1) g->mode = MODE_PLAY;
        break;
    case MODE_SUMMARY:
        if ((in->down & BTN_A) || in->touch_down) {
            g->mode = MODE_PLAY;
            if (g->passed_out) show_msg(g, "You passed out... Get to bed before 2AM!");
            else if (g->s.day == 1) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s has arrived!", SEASON_NAMES[g->s.season]);
                show_msg(g, buf);
            } else if (g->s.weather == WEATHER_RAIN) show_msg(g, "It's raining. The crops are watered!");
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering: world

static unsigned hash2(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

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

static void draw_house(Canvas *c, int sx, int sy)
{
    int w = HOUSE_W * TILE, h = HOUSE_H * TILE;
    u32 ink = RGB(0x2b1d14), wall = RGB(0xd8a868), wall_d = RGB(0xb07e44);
    u32 roof = RGB(0xb83a3a), roof_d = RGB(0x7a2424), roof_l = RGB(0xe05a4a);
    int roof_h = 30;
    // chimney
    cv_rect(c, sx + 58, sy - 4, 10, 14, ink);
    cv_rect(c, sx + 59, sy - 3, 8, 13, RGB(0x8a8a8a));
    cv_rect(c, sx + 59, sy - 3, 8, 2, RGB(0xaaaaaa));
    // walls
    cv_rect(c, sx + 3, sy + roof_h - 2, w - 6, h - roof_h + 2, ink);
    cv_rect(c, sx + 4, sy + roof_h - 2, w - 8, h - roof_h + 1, wall);
    for (int y = sy + roof_h + 3; y < sy + h - 1; y += 5) cv_rect(c, sx + 4, y, w - 8, 1, wall_d);
    // roof
    for (int i = 0; i < roof_h; i++) {
        int inset = (roof_h - i) / 3;
        cv_rect(c, sx + inset - 1, sy + i, w - 2 * inset + 2, 1, ink);
        cv_rect(c, sx + inset, sy + i, w - 2 * inset, 1, (i % 6 == 5) ? roof_d : roof);
    }
    cv_rect(c, sx + 10, sy + 1, w - 20, 2, roof_l);
    cv_rect(c, sx - 1, sy + roof_h - 2, w + 2, 3, ink);
    cv_rect(c, sx, sy + roof_h - 2, w, 1, roof_d);
    // door (centre tile)
    int dx = sx + 2 * TILE + 2, dy = sy + h - 24;
    cv_rect(c, dx - 1, dy - 1, 14, 25, ink);
    cv_rect(c, dx, dy, 12, 24, RGB(0x7a4a24));
    cv_rect(c, dx + 2, dy + 2, 8, 9, RGB(0x8e5a2e));
    cv_rect(c, dx + 2, dy + 13, 8, 9, RGB(0x8e5a2e));
    cv_rect(c, dx + 9, dy + 12, 2, 2, RGB(0xf2d34b));
    // windows
    for (int i = 0; i < 2; i++) {
        int wx = sx + (i ? w - 26 : 12), wy = sy + roof_h + 6;
        cv_rect(c, wx - 1, wy - 1, 16, 14, ink);
        cv_rect(c, wx, wy, 14, 12, RGB(0x9fd3ff));
        cv_rect(c, wx + 1, wy + 1, 4, 3, RGB(0xe8f6ff));
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
    // lid
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
    // posts
    cv_rect(c, sx + 2, sy + 4, 4, 26, ink);
    cv_rect(c, sx + 42, sy + 4, 4, 26, ink);
    cv_rect(c, sx + 3, sy + 4, 2, 26, RGB(0x9a6332));
    cv_rect(c, sx + 43, sy + 4, 2, 26, RGB(0x9a6332));
    // counter
    cv_rect(c, sx, sy + 16, 48, 16, ink);
    cv_rect(c, sx + 1, sy + 17, 46, 14, RGB(0xb07a44));
    cv_rect(c, sx + 1, sy + 17, 46, 3, RGB(0xd8a868));
    // crates of produce on the counter
    static const u32 goods[3] = {RGB(0xe03040), RGB(0xf2d34b), RGB(0x7ac850)};
    for (int i = 0; i < 3; i++) {
        int gx = sx + 6 + i * 13;
        cv_rect(c, gx, sy + 11, 10, 6, ink);
        cv_rect(c, gx + 1, sy + 12, 8, 4, goods[i]);
        cv_rect(c, gx + 2, sy + 12, 2, 1, C_WHITE);
    }
    // striped awning
    cv_rect(c, sx - 2, sy - 6, 52, 12, ink);
    for (int i = 0; i < 50; i++) {
        u32 col = ((i / 6) % 2) ? RGB(0xf4f0e0) : RGB(0x3c9a5a);
        cv_rect(c, sx - 1 + i, sy - 5, 1, 10, col);
    }
    for (int i = 0; i < 50; i += 6) cv_rect(c, sx - 1 + i, sy + 5, 6, 2, ((i / 6) % 2) ? RGB(0xf4f0e0) : RGB(0x3c9a5a));
    // sign
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
    int frame = g->moving ? cycle[(g->anim / 8) % 4] : 0;

    // shadow
    cv_rect_alpha(c, px - 5, py - 2, 10, 3, RGB(0x000000), 70);

    int tool_x = 0, tool_y = 0, tool_flip = flip, show_tool = g->swing > 0;
    if (show_tool) {
        int up = g->swing > 8;
        switch (s->dir) {
        case DIR_DOWN: tool_x = px - 4; tool_y = up ? py - 26 : py - 10; break;
        case DIR_UP: tool_x = px - 10; tool_y = up ? py - 26 : py - 22; break;
        case DIR_LEFT: tool_x = up ? px - 12 : px - 20; tool_y = up ? py - 26 : py - 14; break;
        default: tool_x = up ? px - 4 : px + 4; tool_y = up ? py - 26 : py - 14; break;
        }
        if (s->dir == DIR_UP) cv_sprite(c, &S_ICON[g->swing_item], tool_x, tool_y, tool_flip);
    }
    cv_sprite(c, &S_PLAYER[di][frame], px - 8, py - 16, flip);
    if (show_tool && s->dir != DIR_UP) cv_sprite(c, &S_ICON[g->swing_item], tool_x, tool_y, tool_flip);

    // water droplets while watering
    if (show_tool && g->swing_item == IT_CAN && g->swing <= 8) {
        int tx, ty;
        facing_tile(g, &tx, &ty);
        int wx = tx * TILE - camx, wy = ty * TILE - camy;
        for (int i = 0; i < 6; i++)
            cv_rect(c, wx + 2 + (int)(hash2(i, g->swing) % 12), wy + 2 + ((i * 5 + (8 - g->swing) * 2) % 12), 1, 2, RGB(0x9fd3ff));
    }
}

static void render_world(Game *g, Canvas *c)
{
    SaveData *s = &g->s;
    int camx = (int)s->px - TOP_W / 2, camy = (int)s->py - 8 - SCREEN_H / 2;
    if (camx < 0) camx = 0;
    if (camy < 0) camy = 0;
    if (camx > MAP_W * TILE - TOP_W) camx = MAP_W * TILE - TOP_W;
    if (camy > MAP_H * TILE - SCREEN_H) camy = MAP_H * TILE - SCREEN_H;

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
            default: cv_sprite(c, &S_GRASS[(h % 13 == 0) ? 3 : (int)(h % 3)], sx, sy, (h >> 8) & 1); break;
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
                else if (crop_ripe(t)) cv_sprite(c, &S_RIPE[t->crop - 1], sx, sy - 3, 0);
                else cv_sprite(c, &S_STAGE[crop_stage(t)], sx, sy - 2, 0);
            }
        }

    // Target cursor
    if (g->mode == MODE_PLAY) {
        int tx, ty;
        facing_tile(g, &tx, &ty);
        int sx = tx * TILE - camx, sy = ty * TILE - camy;
        u32 col = interact(g, 0) ? C_HILITE : RGB(0xffffff);
        int a = interact(g, 0) ? 230 : 90;
        cv_rect_alpha(c, sx, sy, 4, 1, col, a);
        cv_rect_alpha(c, sx, sy, 1, 4, col, a);
        cv_rect_alpha(c, sx + 12, sy, 4, 1, col, a);
        cv_rect_alpha(c, sx + 15, sy, 1, 4, col, a);
        cv_rect_alpha(c, sx, sy + 15, 4, 1, col, a);
        cv_rect_alpha(c, sx, sy + 12, 1, 4, col, a);
        cv_rect_alpha(c, sx + 12, sy + 15, 4, 1, col, a);
        cv_rect_alpha(c, sx + 15, sy + 12, 1, 4, col, a);
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

    int ftx, fty;
    facing_tile(g, &ftx, &fty);
    int near_bin = fty == BIN_Y && (ftx == BIN_X || ftx == BIN_X + 1) && IS_CROP(s->sel);
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
        case D_HOUSE: draw_house(c, sx, sy); break;
        case D_BIN: draw_bin(c, sx, sy, near_bin); break;
        case D_SHOP: draw_shop(c, sx, sy); break;
        case D_PLAYER: draw_player(c, g, camx, camy); break;
        }
    }
}

static void render_weather(Game *g, Canvas *c)
{
    if (g->s.weather == WEATHER_RAIN) {
        cv_rect_alpha(c, 0, 0, TOP_W, SCREEN_H, RGB(0x203050), 50);
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

static void render_night(Game *g, Canvas *c)
{
    int m = g->s.minutes;
    if (m <= 17 * 60) return;
    float t = (m - 17 * 60) / 240.0f;
    if (t > 1) t = 1;
    cv_tint(c, 256 - (int)(t * 130), 256 - (int)(t * 110), 256 - (int)(t * 40));
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
    cv_text(c, TOP_W - 72, 21, t, C_INK, 1);
}

static void render_toast(Game *g, Canvas *c)
{
    if (g->msg_timer <= 0 || !g->msg[0]) return;
    int w = text_width(g->msg, 1) + 20;
    int x = (TOP_W - w) / 2, y = SCREEN_H - 30;
    cv_panel(c, x, y, w, 22, C_PAPER, C_WOOD_D);
    cv_text(c, x + 10, y + 8, g->msg, C_INK, 1);
}

static void render_dialog(Canvas *c, const char *title, const char *const *opts, int count, int sel)
{
    int w = 220, h = 34 + count * 18;
    int x = (TOP_W - w) / 2, y = (SCREEN_H - h) / 2;
    cv_rect_alpha(c, 0, 0, TOP_W, SCREEN_H, RGB(0x000000), 90);
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

static void render_hud(Game *g, Canvas *c)
{
    SaveData *s = &g->s;
    char buf[64], t[16];
    cv_clear(c, RGB(0xc89a60));
    for (int y = 0; y < SCREEN_H; y += 8) cv_rect(c, 0, y, BOT_W, 1, RGB(0xb88a50));

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
    draw_bar(c, 12, 49, 140, s->energy, MAX_ENERGY, s->energy > 25 ? RGB(0x5ac83a) : RGB(0xe0503a), "ENERGY", buf);
    snprintf(buf, sizeof(buf), "%d/%d", s->water, can_capacity(s));
    draw_bar(c, 164, 49, 144, s->water, can_capacity(s), RGB(0x3f8fe6), "WATER", buf);

    // Backpack
    cv_panel(c, 4, INV_Y - 4, BOT_W - 8, INV_ROWS * INV_STEP + 6, C_PAPER2, C_WOOD_D);
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
            if (!IS_TOOL(it)) {
                snprintf(buf, sizeof(buf), "%d", s->inv[it]);
                cv_text_shadow(c, x + 35 - text_width(buf, 1), y + 27, buf, C_WHITE, C_SHADOW, 1);
            }
            if ((it == IT_HOE && s->hoe_lvl) || (it == IT_CAN && s->can_lvl)) cv_rect(c, x + 29, y + 3, 4, 4, RGB(0x4a90e8));
        }
    if (scroll > 0) cv_text(c, BOT_W - 16, INV_Y - 2, "^", C_INK, 1);

    // Info / hints
    int iy = INV_Y + INV_ROWS * INV_STEP + 4;
    cv_panel(c, 4, iy, BOT_W - 8, SCREEN_H - iy - 4, C_PAPER, C_WOOD_D);
    char nm[32];
    item_name(s->sel, nm, sizeof(nm));
    if (IS_CROP(s->sel)) snprintf(buf, sizeof(buf), "%s  (%dG each)", nm, CROPS[s->sel - IT_CROP0].sell_price);
    else if (IS_SEED(s->sel)) {
        const CropDef *cd = &CROPS[s->sel - IT_SEED0];
        snprintf(buf, sizeof(buf), "%s  %d days%s", nm, cd->days, cd->regrow ? ", regrows" : "");
    } else snprintf(buf, sizeof(buf), "%s", nm);
    cv_text(c, 12, iy + 9, buf, C_INK, 1);
    const char *act = interact(g, 0);
    if (act) snprintf(buf, sizeof(buf), "A: %s", act);
    else if (IS_CROP(s->sel)) snprintf(buf, sizeof(buf), "Y: Eat   Ship it at the bin");
    else snprintf(buf, sizeof(buf), "L/R: Item  B: Run  START: Menu");
    cv_text(c, 12, iy + 25, buf, RGB(0x8a4a20), 1);
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
        cv_sprite(c, &S_ICON[it->item], 12, y + 3, 0);
        shop_entry_name(it, name, sizeof(name));
        cv_text(c, 34, y + 4, name, C_INK, 1);
        if (it->kind == SHOP_ITEM && IS_SEED(it->item)) {
            const CropDef *cd = &CROPS[it->item - IT_SEED0];
            snprintf(buf, sizeof(buf), "%dd%s, sells %dG", cd->days, cd->regrow ? " regrows" : "", cd->sell_price * cd->yield);
        } else if (it->item == IT_SPRINKLER) snprintf(buf, sizeof(buf), "Waters 8 tiles around it");
        else if (it->kind == SHOP_CAN) snprintf(buf, sizeof(buf), "80 water, waters 3 tiles");
        else snprintf(buf, sizeof(buf), "Tills 3 tiles in a row");
        cv_text(c, 34, y + 13, buf, RGB(0x8a6a40), 1);
        snprintf(buf, sizeof(buf), "%dG", it->price);
        cv_text(c, BOT_W - 14 - text_width(buf, 1), y + 8, buf, s->gold >= it->price ? C_INK : RGB(0xc03020), 1);
        if (it->kind == SHOP_ITEM) {
            snprintf(buf, sizeof(buf), "x%d", s->inv[it->item]);
            cv_text(c, 200, y + 8, buf, RGB(0x8a6a40), 1);
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
        cv_text_center(c, BOT_W / 2, y + 10, opts[i], C_INK, 2);
    }
}

static void render_title(Game *g, Canvas *top, Canvas *bot)
{
    // Sky
    for (int y = 0; y < SCREEN_H; y++) {
        int t = y * 256 / SCREEN_H;
        u32 r = 0x70 + (0xc8 - 0x70) * t / 256, gg = 0xb8 + (0xe8 - 0xb8) * t / 256, b = 0xf0;
        cv_rect(top, 0, y, TOP_W, 1, RGB((r << 16) | (gg << 8) | b));
    }
    // Clouds
    for (int i = 0; i < 4; i++) {
        int x = (int)((i * 130 + g->frame / 3) % 520) - 60, y = 20 + i * 17 % 50;
        cv_rect(top, x, y + 4, 50, 10, C_WHITE);
        cv_rect(top, x + 10, y, 26, 18, C_WHITE);
    }
    // Hills & field
    for (int x = 0; x < TOP_W; x++) {
        int h1 = 150 + (int)(10 * sinf(x * 0.02f)), h2 = 170 + (int)(8 * sinf(x * 0.035f + 1));
        cv_rect(top, x, h1, 1, SCREEN_H - h1, RGB(0x55a428));
        cv_rect(top, x, h2, 1, SCREEN_H - h2, RGB(0x6abe30));
    }
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 13; i++) {
            int x = 8 + i * 30 + row * 15, y = 192 + row * 22;
            cv_sprite(top, &S_TILLED, x, y, 0);
            cv_sprite(top, &S_RIPE[(i + row * 5) % NUM_CROPS], x, y - 3, 0);
        }
    cv_sprite(top, &S_TREE, 20, 150, 0);
    cv_sprite(top, &S_TREE, 360, 152, 1);
    const char *t1 = "PIXEL FARM";
    cv_text_shadow(top, (TOP_W - text_width(t1, 5)) / 2, 50, t1, RGB(0xffe070), RGB(0x5a3418), 5);
    cv_text_center(top, TOP_W / 2, 100, "A COZY LITTLE FARMING GAME", RGB(0x3a2414), 1);

    cv_clear(bot, RGB(0xc89a60));
    for (int y = 0; y < SCREEN_H; y += 8) cv_rect(bot, 0, y, BOT_W, 1, RGB(0xb88a50));
    cv_text_center(bot, BOT_W / 2, 30, "WELCOME, FARMER!", C_INK, 2);
    const char *opts_save[] = {"Continue", "New Farm"};
    const char *opts_new[] = {"New Farm"};
    if (g->has_save) render_menu_buttons(bot, opts_save, 2, g->menu_sel);
    else render_menu_buttons(bot, opts_new, 1, 0);
    cv_panel(bot, 8, 160, BOT_W - 16, 72, C_PAPER, C_WOOD_D);
    cv_text(bot, 16, 167, "Move: Circle Pad / D-Pad   Run: hold B", C_INK, 1);
    cv_text(bot, 16, 179, "A: Use tool / Plant / Harvest / Talk", C_INK, 1);
    cv_text(bot, 16, 191, "L/R or Tap: Pick item     Y: Eat crop", C_INK, 1);
    cv_text(bot, 16, 203, "Ship crops in the bin, sleep in bed.", C_INK, 1);
    cv_text(bot, 16, 215, "START: Menu / Save", C_INK, 1);
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

    cv_clear(bot, RGB(0xc89a60));
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

void game_render(Game *g, Canvas *top, Canvas *bot)
{
    if (g->mode == MODE_TITLE) {
        render_title(g, top, bot);
        return;
    }
    if (g->mode == MODE_SUMMARY) {
        render_summary(g, top, bot);
        return;
    }

    render_world(g, top);
    render_weather(g, top);
    render_night(g, top);
    render_clock_box(g, top);
    if (g->mode != MODE_SHOP) render_toast(g, top);

    if (g->mode == MODE_SHOP) {
        cv_rect_alpha(top, 0, 0, TOP_W, SCREEN_H, RGB(0x000000), 80);
        cv_panel(top, 100, 90, 200, 50, C_PAPER, C_WOOD_D);
        cv_text_center(top, TOP_W / 2, 100, "SEED STALL", C_INK, 2);
        cv_text_center(top, TOP_W / 2, 122, "Fresh seeds, every season!", RGB(0x8a6a40), 1);
        render_shop_bottom(g, bot);
        return;
    }

    if (g->mode == MODE_PAUSE) {
        static const char *opts[] = {"Resume", "Save Game", "Save & Quit"};
        render_dialog(top, "- PAUSED -", opts, 3, g->menu_sel);
        cv_clear(bot, RGB(0xc89a60));
        cv_text_center(bot, BOT_W / 2, 30, "MENU", C_INK, 2);
        render_menu_buttons(bot, opts, 3, g->menu_sel);
        return;
    }
    if (g->mode == MODE_ASK_SLEEP) {
        static const char *opts[] = {"Yes, sleep", "Not yet"};
        render_dialog(top, "Go to bed for the night?", opts, 2, g->menu_sel);
        cv_clear(bot, RGB(0xc89a60));
        cv_text_center(bot, BOT_W / 2, 30, "SLEEP?", C_INK, 2);
        render_menu_buttons(bot, opts, 2, g->menu_sel);
        return;
    }
    render_hud(g, bot);
}
