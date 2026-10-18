// Headless PC harness: plays a scripted session through the real game code
// and writes screenshots (top + bottom screen stacked) as PPM files.
//
//   cc -O2 -Isource source/game.c source/gfx.c source/sprites.c source/data.c
//      tools/pc_sim.c -lm -o pc_sim && ./pc_sim out_dir
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

static char save_path[512];
const char *plat_save_path(void) { return save_path; }

static u32 top_px[TOP_W * SCREEN_H], bot_px[BOT_W * SCREEN_H];
static Canvas top = {top_px, TOP_W, SCREEN_H}, bot = {bot_px, BOT_W, SCREEN_H};
static Game game;
static const char *out_dir = ".";
static int shot_no = 0;

static void shot(const char *name)
{
    game_render(&game, &top, &bot);
    char path[600];
    snprintf(path, sizeof(path), "%s/%02d_%s.ppm", out_dir, shot_no++, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TOP_W, SCREEN_H * 2);
    for (int y = 0; y < SCREEN_H * 2; y++)
        for (int x = 0; x < TOP_W; x++) {
            u32 c = 0xff202020;
            if (y < SCREEN_H) c = top_px[y * TOP_W + x];
            else if (x >= 40 && x < 40 + BOT_W) c = bot_px[(y - SCREEN_H) * BOT_W + x - 40];
            unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("wrote %s\n", path);
}

static void step(uint32_t held, uint32_t down)
{
    Input in = {0};
    in.held = held | down;
    in.down = down;
    game_update(&game, &in);
}

static void hold(uint32_t keys, int frames)
{
    for (int i = 0; i < frames; i++) step(keys, 0);
}

static void press(uint32_t keys)
{
    step(keys, keys);
    hold(0, 20);
}

static void tap(int x, int y)
{
    Input in = {0};
    in.touching = in.touch_down = 1;
    in.tx = x;
    in.ty = y;
    game_update(&game, &in);
    hold(0, 5);
}

static void select_item(int item)
{
    for (int i = 0; i < NUM_ITEMS && game.s.sel != item; i++) press(BTN_R);
}

static void face(uint32_t dir)
{
    step(dir, dir);
    hold(0, 2);
}

#include "sprites.h"

// Writes the 48x48 app icon (green tile with a ripe strawberry) as PPM.
static int write_icon(const char *path)
{
    static u32 px[48 * 48];
    Canvas c = {px, 48, 48};
    spr_init();
    cv_clear(&c, RGB(0x2b1d14));
    cv_panel(&c, 0, 0, 48, 48, RGB(0x6abe30), RGB(0x2b1d14));
    cv_rect(&c, 4, 30, 40, 14, RGB(0x8e5c34));
    cv_rect(&c, 4, 34, 40, 1, RGB(0x6a4024));
    cv_rect(&c, 4, 39, 40, 1, RGB(0x6a4024));
    cv_sprite_scaled(&c, &S_ICON[IT_CROP0 + 3], 0, -1, 3);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n48 48\n255\n");
    for (int i = 0; i < 48 * 48; i++) {
        unsigned char rgb[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--icon")) return write_icon(argv[2]);
    if (argc > 1) out_dir = argv[1];
    snprintf(save_path, sizeof(save_path), "%s/save.dat", out_dir);
    remove(save_path);

    game_init(&game);
    shot("title");
    press(BTN_A); // new game
    shot("farm_start");

    // Walk to the starter patch below the house
    hold(BTN_DOWN, 70);
    shot("walk");

    // Till, plant and water a row of 5 tiles facing right.
    for (int i = 0; i < 5; i++) {
        face(BTN_RIGHT);
        select_item(IT_HOE);
        press(BTN_A);
        select_item(IT_SEED0);
        press(BTN_A);
        select_item(IT_CAN);
        press(BTN_A);
        hold(BTN_LEFT, 15);
    }
    shot("planted");

    // Go to bed: walk back up to the door
    game.s.px = 6 * TILE + 8;
    game.s.py = 6 * TILE + 14;
    face(BTN_UP);
    press(BTN_A);
    shot("ask_sleep");
    press(BTN_A);
    shot("summary");
    press(BTN_A);

    // Fast-forward days, keeping crops watered by hand via the save struct.
    for (int d = 0; d < 4; d++) {
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (game.s.map[y][x].ground == G_TILLED) game.s.map[y][x].watered = 1;
        game.mode = MODE_ASK_SLEEP;
        game.menu_sel = 0;
        press(BTN_A);
        press(BTN_A);
    }
    // Teleport next to the crops and look at them
    game.s.px = 2 * TILE + 8;
    game.s.py = 11 * TILE + 14;
    game.s.dir = DIR_RIGHT;
    hold(0, 2);
    shot("grown");

    // Harvest everything in the row
    for (int i = 0; i < 6; i++) {
        press(BTN_A);
        hold(BTN_RIGHT, 16);
    }
    shot("harvested");

    // Ship at the bin
    game.s.px = 10 * TILE + 8;
    game.s.py = 6 * TILE + 14;
    game.s.dir = DIR_UP;
    select_item(IT_CROP0);
    press(BTN_A);
    shot("shipped");

    // Shop
    game.s.px = 15 * TILE + 8;
    game.s.py = 6 * TILE + 14;
    game.s.dir = DIR_UP;
    press(BTN_A);
    press(BTN_DOWN);
    press(BTN_A);
    shot("shop");
    press(BTN_B);

    // Evening + rain view with lots of crop variety
    for (int i = 0; i < NUM_CROPS; i++) {
        Tile *t = &game.s.map[17][3 + i];
        t->obj = 0;
        t->ground = G_TILLED;
        t->watered = i & 1;
        t->crop = (uint8_t)(i + 1);
        t->growth = (uint8_t)CROPS[i].days;
        Tile *u = &game.s.map[18][3 + i];
        u->obj = 0;
        u->ground = G_TILLED;
        u->crop = (uint8_t)(i + 1);
        u->growth = (uint8_t)(i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : i % 4 == 2 ? CROPS[i].days / 2 : CROPS[i].days - 1);
    }
    game.s.map[16][16].obj = 6; // sprinkler
    game.s.px = 9 * TILE;
    game.s.py = 16 * TILE + 10;
    game.s.dir = DIR_DOWN;
    game.s.minutes = 19 * 60;
    hold(0, 2);
    shot("evening");
    game.s.weather = WEATHER_RAIN;
    game.s.minutes = 10 * 60;
    hold(0, 2);
    shot("rain");

    // Pond + tool swing
    game.s.px = 34 * TILE + 4;
    game.s.py = 22 * TILE + 14;
    game.s.dir = DIR_RIGHT;
    game.s.weather = WEATHER_SUN;
    select_item(IT_CAN);
    step(BTN_A, BTN_A);
    hold(0, 3);
    shot("pond_swing");
    hold(0, 30);

    // Seasons
    for (int s = 1; s < 4; s++) {
        game.s.day = 28;
        game.s.season = s - 1;
        game.mode = MODE_ASK_SLEEP;
        game.menu_sel = 0;
        press(BTN_A);
        press(BTN_A);
        game.s.px = 20 * TILE;
        game.s.py = 14 * TILE;
        hold(0, 2);
        char name[32];
        snprintf(name, sizeof(name), "season_%d", s);
        shot(name);
    }

    // Pause menu
    press(BTN_START);
    shot("pause");
    press(BTN_DOWN);
    press(BTN_A); // save

    // Reload from title
    Game copy = game;
    game_init(&game);
    press(BTN_A); // continue
    printf("reload ok: day %d season %d gold %d (expected %d)\n", game.s.day, game.s.season, game.s.gold, copy.s.gold);
    shot("reloaded");
    tap(200, 120); // touch the backpack
    return game.s.gold == copy.s.gold ? 0 : 1;
}
