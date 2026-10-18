#pragma once
#include <stdint.h>
#include "art.h"
#include "gfx.h"

#define TOP_W 400
#define BOT_W 320
#define SCREEN_H 240

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

typedef struct {
    uint8_t kind;    // 0 = empty, otherwise crop kind + 1
    uint8_t stage;   // 0 seed .. 4 ripe
    uint8_t watered; // once watered, a crop grows all the way
    uint8_t pad;
    float grow;      // seconds of growth so far
} Bed;

typedef struct {
    uint32_t magic, version;
    Bed beds[NUM_BEDS];
    int coins, unlocked, sel, music_on, harvested;
    float clock; // time of day, 0..1
} SaveData;

enum { MODE_TITLE, MODE_PLAY, MODE_PAUSE };

typedef struct {
    float x, y, vx, vy, grav;
    int life;
    u32 col;
    uint8_t size, kind; // kind: 0 square bit, 1 soft puff (smoke)
} Particle;

typedef struct {
    float x, y;
    int life;
    u32 col;
    char text[24];
} Floater;

#define MAX_PARTICLES 220
#define MAX_FLOATERS 8

typedef struct {
    SaveData s;
    int mode, frame, menu_sel;
    int target;          // bed the farmer is heading to / standing at
    float fx, fy;        // farmer's feet
    float path_x[3], path_y[3];
    int path_len, facing, walk_anim, idle;
    int pending;         // A was pressed while walking: act on arrival
    int act_timer, act_tool, repeat;
    float rain, rain_timer, wind;
    char msg[64];
    int msg_timer;
    int dirty, save_timer, has_save, quit;
    int card_bounce;
    Particle parts[MAX_PARTICLES];
    Floater floats[MAX_FLOATERS];
    u32 pal[256];        // art palette tinted for the time of day
    float dark;          // 0 day .. 1 deep night
} Game;

void game_init(Game *g);
void game_update(Game *g, const Input *in);
void game_render(Game *g, Canvas *top, Canvas *bottom);
void game_shutdown(Game *g); // saves

// Provided by the platform layer.
const char *plat_save_path(void);
