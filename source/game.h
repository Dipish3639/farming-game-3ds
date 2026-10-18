#pragma once
#include <stdint.h>
#include "gfx.h"
#include "data.h"

#define TOP_W 400
#define BOT_W 320
#define SCREEN_H 240

#define MAP_W 48
#define MAP_H 32
#define TILE 16

enum {
    BTN_A = 1 << 0,
    BTN_B = 1 << 1,
    BTN_X = 1 << 2,
    BTN_Y = 1 << 3,
    BTN_L = 1 << 4,
    BTN_R = 1 << 5,
    BTN_START = 1 << 6,
    BTN_SELECT = 1 << 7,
    BTN_UP = 1 << 8,
    BTN_DOWN = 1 << 9,
    BTN_LEFT = 1 << 10,
    BTN_RIGHT = 1 << 11,
};

typedef struct {
    uint32_t held, down;
    int touching, touch_down; // touch_down: first frame of a tap
    int tx, ty;               // bottom-screen touch position
} Input;

enum { G_GRASS, G_DIRT, G_WATER, G_TILLED };
enum { O_NONE, O_TREE, O_WEED, O_STONE, O_TWIG, O_FENCE, O_SPRINKLER, O_HOUSE, O_BIN, O_SHOP };

typedef struct {
    uint8_t ground, obj, watered, crop; // crop: 0 = none, otherwise crop index + 1
    uint8_t growth, dead;
} Tile;

enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum { WEATHER_SUN, WEATHER_RAIN, WEATHER_SNOW };

typedef struct {
    uint32_t magic, version;
    Tile map[MAP_H][MAP_W];
    float px, py;
    int dir;
    int day, season, year, minutes;
    int gold, energy, water;
    int inv[NUM_ITEMS];
    int sel; // selected backpack item, -1 = none
    int hoe_lvl, can_lvl;
    int weather;
    uint32_t rng;
    int shipped[NUM_CROPS];
    int total_earned, harvested, bought;
    int quest, quest_prog;
    int music_on;
} SaveData;

enum { MODE_TITLE, MODE_PLAY, MODE_SHOP, MODE_PAUSE, MODE_ASK_SLEEP, MODE_SUMMARY, MODE_HELP };

typedef struct {
    float x, y, vx, vy, grav;
    int life;
    u32 col;
    uint8_t size;
} Particle;

typedef struct {
    float x, y;
    int life;
    u32 col;
    char text[28];
} Floater;

typedef struct {
    float x, y, vy;
    int item, life;
} ItemPop;

typedef struct {
    float x, y, vx, vy;
    int t;
    u32 col;
} Critter;

#define MAX_PARTICLES 160
#define MAX_FLOATERS 10
#define MAX_POPS 12
#define MAX_CRITTERS 6

typedef struct {
    SaveData s;
    int mode, menu_sel, help_page, help_return;
    int shop_sel, shop_scroll;
    int frame, minute_acc;
    int anim, moving, swing, swing_tool, a_hold, step_timer;
    float camx, camy;
    char msg[64];
    int msg_timer;
    int quit, has_save, loaded_season;
    int sum_earn, sum_items[NUM_CROPS], passed_out, sum_day;
    int fade, fade_dir, fade_pending; // fade_pending: 1 = sleep, 2 = pass out
    Particle parts[MAX_PARTICLES];
    Floater floats[MAX_FLOATERS];
    ItemPop pops[MAX_POPS];
    Critter critters[MAX_CRITTERS];
} Game;

void game_init(Game *g);
void game_update(Game *g, const Input *in);
void game_render(Game *g, Canvas *top, Canvas *bottom);

// Provided by the platform layer.
const char *plat_save_path(void);
