// Headless PC harness: plays through the tutorial with button presses only
// (like a real player following the on-screen goals), checks the goals
// complete, and writes screenshots (top + bottom screen stacked, PPM) plus a
// WAV of everything the game played.
//
//   cc -O2 -Isource source/{game,gfx,sprites,data,audio}.c tools/pc_sim.c -lm -o pc_sim
//   ./pc_sim out_dir
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "game.h"
#include "sprites.h"

static char save_path[512];
const char *plat_save_path(void) { return save_path; }

static u32 top_px[TOP_W * SCREEN_H], bot_px[BOT_W * SCREEN_H];
static Canvas top = {top_px, TOP_W, SCREEN_H}, bot = {bot_px, BOT_W, SCREEN_H};
static Game game;
static const char *out_dir = ".";
static int shot_no = 0, failures = 0;

#define MAX_AUDIO (AUDIO_RATE * 300)
static int16_t *audio_buf;
static int audio_len;
static double audio_acc;

static int write_ppm(const char *path, const Canvas *c)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        unsigned char rgb[3] = {(unsigned char)(c->px[i] >> 16), (unsigned char)(c->px[i] >> 8), (unsigned char)c->px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static void shot(const char *name)
{
    static u32 both_px[TOP_W * SCREEN_H * 2];
    Canvas both = {both_px, TOP_W, SCREEN_H * 2};
    game_render(&game, &top, &bot);
    for (int y = 0; y < SCREEN_H * 2; y++)
        for (int x = 0; x < TOP_W; x++) {
            u32 c = 0xff202020;
            if (y < SCREEN_H) c = top_px[y * TOP_W + x];
            else if (x >= 40 && x < 40 + BOT_W) c = bot_px[(y - SCREEN_H) * BOT_W + x - 40];
            both_px[y * TOP_W + x] = c;
        }
    char path[600];
    snprintf(path, sizeof(path), "%s/%02d_%s.ppm", out_dir, shot_no++, name);
    write_ppm(path, &both);
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// One 60 fps frame, including the audio the 3DS would play during it.
static void step(uint32_t held, uint32_t down)
{
    Input in = {0};
    in.held = held | down;
    in.down = down;
    game_update(&game, &in);
    audio_acc += AUDIO_RATE / 60.0;
    int n = (int)audio_acc;
    audio_acc -= n;
    if (audio_len + n <= MAX_AUDIO) {
        audio_render(audio_buf + audio_len, n);
        audio_len += n;
    } else {
        int16_t tmp[1024];
        audio_render(tmp, n);
    }
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

static int walkable(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
    const Tile *t = &game.s.map[y][x];
    return t->ground != G_WATER && t->obj == O_NONE;
}

// Walk one tile at a time (with the d-pad, like a player) until centred in (tx, ty).
static void step_to_tile(int tx, int ty)
{
    for (int guard = 0; guard < 120; guard++) {
        float fx = game.s.px - (tx * TILE + 8), fy = game.s.py - (ty * TILE + 12);
        uint32_t k = 0;
        if (fx > 1.5f || fx < -1.5f) k |= fx > 0 ? BTN_LEFT : BTN_RIGHT;
        else if (fy > 1.5f || fy < -1.5f) k |= fy > 0 ? BTN_UP : BTN_DOWN;
        if (!k) return;
        step(k, 0);
    }
}

// Breadth-first search over walkable tiles, then follow the path.
static void walk_to(int tx, int ty)
{
    static int prev[MAP_H * MAP_W];
    int sx = (int)(game.s.px / TILE), sy = (int)((game.s.py - 4) / TILE);
    int queue[MAP_H * MAP_W], qh = 0, qt = 0;
    for (int i = 0; i < MAP_H * MAP_W; i++) prev[i] = -2;
    prev[sy * MAP_W + sx] = -1;
    queue[qt++] = sy * MAP_W + sx;
    while (qh < qt) {
        int cur = queue[qh++], cx = cur % MAP_W, cy = cur / MAP_W;
        if (cx == tx && cy == ty) break;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (!walkable(nx, ny) || prev[ny * MAP_W + nx] != -2) continue;
            prev[ny * MAP_W + nx] = cur;
            queue[qt++] = ny * MAP_W + nx;
        }
    }
    if (prev[ty * MAP_W + tx] == -2) {
        printf("  (no path to %d,%d)\n", tx, ty);
        return;
    }
    int path[MAP_H * MAP_W], n = 0;
    for (int at = ty * MAP_W + tx; at != -1; at = prev[at]) path[n++] = at;
    for (int i = n - 1; i >= 0; i--) step_to_tile(path[i] % MAP_W, path[i] / MAP_W);
    hold(0, 2);
}

static void face(uint32_t dir)
{
    step(dir, dir);
    hold(0, 3);
}

static void wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int rate = AUDIO_RATE, bytes = audio_len * 2, x;
    short s;
    fwrite("RIFF", 1, 4, f);
    x = 36 + bytes;
    fwrite(&x, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    x = 16;
    fwrite(&x, 4, 1, f);
    s = 1;
    fwrite(&s, 2, 1, f);
    fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    x = rate * 2;
    fwrite(&x, 4, 1, f);
    s = 2;
    fwrite(&s, 2, 1, f);
    s = 16;
    fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(audio_buf, 2, audio_len, f);
    fclose(f);
}

static void sleep_through_night(int screenshot)
{
    walk_to(6, 6);
    face(BTN_UP);
    press(BTN_A); // "Sleep" -> ask
    press(BTN_A); // yes
    hold(0, 40);  // fade out
    if (screenshot) shot("summary");
    press(BTN_A); // start the day
    hold(0, 25);
}

static void water_row(void)
{
    for (int x = 4; x <= 7; x++) {
        walk_to(x, 12);
        face(BTN_DOWN);
        press(BTN_A);
    }
}

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
    return write_ppm(path, &c);
}

// Writes the 256x128 HOME Menu banner (for the .cia) as PPM.
static int write_banner(const char *path)
{
    static u32 px[256 * 128];
    Canvas c = {px, 256, 128};
    spr_init();
    for (int y = 0; y < 128; y++) {
        u32 r = 0x70 + 0x50 * y / 128, g = 0xb8 + 0x30 * y / 128;
        cv_rect(&c, 0, y, 256, 1, RGB((r << 16) | (g << 8) | 0xf0));
    }
    cv_rect(&c, 20, 14, 40, 8, RGB(0xffffff));
    cv_rect(&c, 28, 10, 22, 14, RGB(0xffffff));
    cv_rect(&c, 190, 22, 36, 7, RGB(0xffffff));
    cv_rect(&c, 198, 18, 18, 12, RGB(0xffffff));
    for (int x = 0; x < 256; x++) {
        int h1 = 78 + (x * 7 % 23 < 12 ? 0 : 1) + (x < 128 ? (128 - x) / 16 : (x - 128) / 20);
        cv_rect(&c, x, h1, 1, 128 - h1, RGB(0x55a428));
        cv_rect(&c, x, 92, 1, 36, RGB(0x6abe30));
    }
    cv_sprite(&c, &S_TREE, 6, 66, 0);
    cv_sprite(&c, &S_TREE, 234, 68, 1);
    cv_sprite(&c, &S_PLAYER[0][0], 120, 82, 0);
    for (int i = 0; i < 8; i++) {
        cv_sprite(&c, &S_TILLED, 8 + i * 31, 104, 0);
        cv_sprite(&c, &S_RIPE[(i * 5) % NUM_CROPS], 8 + i * 31, 101, 0);
    }
    const char *t = "PIXEL FARM";
    cv_text_shadow(&c, (256 - text_width(t, 3)) / 2, 34, t, RGB(0xffe070), RGB(0x5a3418), 3);
    return write_ppm(path, &c);
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--icon")) return write_icon(argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--banner")) return write_banner(argv[2]);
    if (argc > 1) out_dir = argv[1];
    snprintf(save_path, sizeof(save_path), "%s/save.dat", out_dir);
    remove(save_path);
    audio_buf = (int16_t *)malloc(MAX_AUDIO * sizeof(int16_t));

    audio_init();
    game_init(&game);
    hold(0, 60);
    shot("title");
    press(BTN_A); // New Farm
    hold(0, 30);
    shot("start");
    check(game.mode == MODE_PLAY && game.s.quest == 0, "new game starts on the first goal");

    // Goal 1: clear the 3 weeds next to the starter field
    walk_to(4, 10);
    face(BTN_DOWN);
    shot("weed_prompt");
    press(BTN_A);
    walk_to(8, 10);
    face(BTN_DOWN);
    press(BTN_A);
    walk_to(10, 11);
    face(BTN_DOWN);
    press(BTN_A);
    check(game.s.quest == 1, "goal: clear 3 weeds");

    // Goals 2-4: dig, plant and water 4 tiles, one tile at a time with A, A, A
    for (int x = 4; x <= 7; x++) {
        walk_to(x, 12);
        face(BTN_DOWN);
        press(BTN_A);
        if (x == 4) shot("dug");
        press(BTN_A);
        if (x == 4) shot("planted");
        press(BTN_A);
    }
    shot("watered");
    check(game.s.quest == 4, "goals: dig, plant and water 4 tiles (in any order)");

    // Goal 5: sleep. The arrow points at the door.
    walk_to(8, 10);
    hold(0, 5);
    shot("arrow_to_door");
    sleep_through_night(1);
    check(game.s.quest == 5 && game.s.day == 2, "goal: go to bed");

    // Keep watering every day until the parsnips are ripe (4 days)
    for (int d = 0; d < 3; d++) {
        water_row();
        if (d == 0) shot("next_day_watering");
        sleep_through_night(0);
    }
    walk_to(4, 12);
    face(BTN_DOWN);
    hold(0, 30);
    shot("ripe");
    // Goal 6: harvest
    for (int x = 4; x <= 7; x++) {
        walk_to(x, 12);
        face(BTN_DOWN);
        press(BTN_A);
        if (x == 5) shot("harvest_pop");
    }
    check(game.s.inv[IT_CROP0] >= 4, "harvested 4 parsnips");
    check(game.s.quest == 6, "goal: harvest 4 crops");

    // Goal 7: ship them
    walk_to(10, 6);
    face(BTN_UP);
    shot("ship_prompt");
    press(BTN_A);
    hold(0, 10);
    shot("shipped");
    check(game.s.quest == 7, "goal: ship crops");

    // Goal 8: buy seeds
    walk_to(15, 6);
    face(BTN_UP);
    press(BTN_A);
    check(game.mode == MODE_SHOP, "shop opens");
    press(BTN_DOWN);
    press(BTN_A);
    shot("shop");
    press(BTN_B);
    check(game.s.quest == 8 && game.s.inv[IT_SEED0 + 1] == 1, "goal: buy seeds");

    // Hold A on one spot: dig, plant and water in one go
    walk_to(9, 13);
    face(BTN_RIGHT);
    hold(BTN_A, 60);
    shot("hold_a");
    check(game.s.map[13][10].crop != 0 && game.s.map[13][10].watered, "holding A digs, plants and waters");

    // Running is faster than walking
    walk_to(3, 7);
    float x0 = game.s.px;
    hold(BTN_RIGHT, 25);
    float walked = game.s.px - x0;
    x0 = game.s.px;
    hold(BTN_RIGHT | BTN_B, 25);
    printf("  walk %.0f px/s, run %.0f px/s\n", walked * 60 / 25, (game.s.px - x0) * 60 / 25);
    check(game.s.px - x0 > walked * 1.3f, "B makes you run");

    // Summer night with fireflies
    game.s.season = SEASON_SUMMER;
    spr_set_season(SEASON_SUMMER);
    game.s.minutes = 20 * 60;
    hold(0, 60);
    shot("summer_night");
    game.s.season = SEASON_SPRING;
    spr_set_season(SEASON_SPRING);
    game.s.minutes = 10 * 60;

    // Pond refill
    game.s.water = 3;
    for (int i = 0, done = 0; i < MAP_W * MAP_H && !done; i++) {
        int x = i % MAP_W, y = i / MAP_W;
        if (x + 1 < MAP_W && walkable(x, y) && game.s.map[y][x + 1].ground == G_WATER) {
            walk_to(x, y);
            done = 1;
        }
    }
    face(BTN_RIGHT);
    press(BTN_A);
    check(game.s.water == 40, "refill at the pond");

    // Pause menu, help, save
    press(BTN_START);
    shot("pause");
    press(BTN_DOWN);
    press(BTN_A);
    shot("help");
    press(BTN_B);
    press(BTN_DOWN);
    press(BTN_DOWN);
    press(BTN_A);
    check(game.mode == MODE_PLAY, "save from the menu");

    // Reload
    Game copy = game;
    game_init(&game);
    hold(0, 5);
    shot("title_continue");
    press(BTN_A);
    check(game.s.gold == copy.s.gold && game.s.quest == copy.s.quest, "save/load round trip");
    tap(30, 140);

    char wav_path[600];
    snprintf(wav_path, sizeof(wav_path), "%s/session.wav", out_dir);
    wav(wav_path);
    int peak = 0;
    for (int i = 0; i < audio_len; i++) {
        int v = abs(audio_buf[i]);
        if (v > peak) peak = v;
    }
    printf("  audio: %.1f s recorded, peak %d\n", audio_len / (double)AUDIO_RATE, peak);
    check(peak > 1000 && peak < 32767, "audio is audible and not clipping");
    printf(failures ? "%d CHECK(S) FAILED\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
