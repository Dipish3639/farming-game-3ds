// Headless PC harness: plays Tiny Harvest with button presses only, checks
// the core loop works, and writes screenshots (top + bottom screen stacked,
// PPM) plus a WAV of everything the game played.
//
//   cc -O2 -Isource source/game.c source/gfx.c source/art.c source/audio.c tools/pc_sim.c -lm -o pc_sim
//   ./pc_sim out_dir
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "game.h"

static char save_path[512];
const char *plat_save_path(void) { return save_path; }

static u32 top_px[TOP_W * SCREEN_H], bot_px[BOT_W * SCREEN_H];
static Canvas top = {top_px, TOP_W, SCREEN_H}, bot = {bot_px, BOT_W, SCREEN_H};
static Game game;
static const char *out_dir = ".";
static int shot_no = 0, failures = 0;

#define MAX_AUDIO (AUDIO_RATE * 240)
static int16_t *audio_buf;
static int audio_len;
static double audio_acc;

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
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

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
    hold(0, 6);
}

static void tap(int x, int y)
{
    Input in = {0};
    in.touching = in.touch_down = 1;
    in.tx = x;
    in.ty = y;
    game_update(&game, &in);
    hold(0, 6);
}

// Walk to a bed with the d-pad, like a player would.
static void go_to_bed(int bed)
{
    for (int guard = 0; guard < 20 && game.target != bed; guard++) {
        int col = game.target % BED_COLS, row = game.target / BED_COLS;
        int tc = bed % BED_COLS, tr = bed / BED_COLS;
        press(tr < row ? BTN_UP : tr > row ? BTN_DOWN : tc < col ? BTN_LEFT : BTN_RIGHT);
    }
    for (int guard = 0; guard < 400 && game.path_len > 0; guard++) step(0, 0);
}

// Press A at every bed: plants, waters or harvests, whatever it needs.
static void tend_all(void)
{
    for (int i = 0; i < NUM_BEDS; i++) {
        go_to_bed(i);
        press(BTN_A);
        hold(0, 24);
        press(BTN_A);
        hold(0, 24);
    }
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

int main(int argc, char **argv)
{
    if (argc > 1) out_dir = argv[1];
    snprintf(save_path, sizeof(save_path), "%s/save.dat", out_dir);
    remove(save_path);
    audio_buf = (int16_t *)malloc(MAX_AUDIO * sizeof(int16_t));

    audio_init();
    game_init(&game);
    hold(0, 50);
    shot("title");
    press(BTN_A);
    check(game.mode == MODE_PLAY, "A starts the game");
    hold(0, 30);
    shot("start");

    // First round: plant + water turnips in as many beds as we can afford.
    int coins0 = game.s.coins;
    go_to_bed(0);
    press(BTN_A);
    hold(0, 8);
    shot("planting");
    check(game.s.beds[0].kind == 1 && game.s.coins == coins0 - 2, "A on an empty bed plants a turnip");
    hold(0, 20);
    shot("needs_water");
    press(BTN_A);
    hold(0, 6);
    shot("watering");
    check(game.s.beds[0].watered, "A again waters it");
    hold(0, 30);
    for (int i = 1; i < NUM_BEDS; i++) {
        go_to_bed(i);
        press(BTN_A);
        hold(0, 24);
        press(BTN_A);
        hold(0, 24);
    }
    hold(0, 60 * 8);
    shot("growing");
    hold(0, 60 * 14);
    check(game.s.beds[0].stage == 4, "watered turnips ripen in about 20 seconds");
    go_to_bed(0);
    hold(0, 10);
    shot("ripe");
    press(BTN_A);
    hold(0, 10);
    shot("harvest");
    check(game.s.beds[0].kind == 0 && game.s.harvested == 1, "A harvests a ripe crop");

    // Keep farming turnips until we can unlock carrots.
    for (int round = 0; round < 12 && game.s.coins < 40; round++) {
        tend_all();
        hold(0, 60 * 21);
    }
    tend_all();
    check(game.s.coins >= 40, "earned enough to unlock carrots");
    tap(8 + 104 + 40, 42 + 30); // the carrot card
    check(game.s.unlocked == 2 && game.s.sel == 1, "tapping the next card unlocks it");
    hold(0, 20);
    shot("unlocked");
    press(BTN_L);
    check(game.s.sel == 0, "L picks the previous seed");
    press(BTN_R);

    // Showcase: every crop ripe, at different times of day and in the rain.
    for (int i = 0; i < NUM_BEDS; i++) {
        Bed *b = &game.s.beds[i];
        b->kind = (uint8_t)(i % NUM_CROP_KINDS + 1);
        b->stage = (uint8_t)(i < 6 ? 4 : 2 + i % 2);
        b->watered = 1;
        b->grow = 0;
    }
    game.s.beds[6].kind = 6;
    game.s.beds[7].kind = 5;
    game.s.unlocked = NUM_CROP_KINDS;
    game.s.coins = 4321;
    go_to_bed(5);
    hold(0, 6);
    game.s.clock = 0.30f;
    hold(0, 2);
    shot("day");
    game.s.clock = 0.645f;
    hold(0, 2);
    shot("sunset");
    game.s.clock = 0.85f;
    hold(0, 90);
    shot("night");
    game.s.clock = 0.40f;
    game.rain_timer = -5;
    hold(0, 60);
    shot("rain");
    game.rain_timer = 100;

    press(BTN_START);
    shot("pause");
    check(game.mode == MODE_PAUSE, "START pauses");
    press(BTN_DOWN);
    press(BTN_A);
    check(game.s.music_on == 0, "music can be switched off");
    press(BTN_A);
    press(BTN_DOWN);
    press(BTN_A);
    check(game.quit == 1, "save & quit");

    Game copy = game;
    game_init(&game);
    check(game.has_save && game.s.coins == copy.s.coins && game.s.unlocked == copy.s.unlocked, "save/load round trip");

    char wav_path[600];
    snprintf(wav_path, sizeof(wav_path), "%s/session.wav", out_dir);
    wav(wav_path);
    int peak = 0;
    for (int i = 0; i < audio_len; i++) {
        int v = abs(audio_buf[i]);
        if (v > peak) peak = v;
    }
    check(peak > 1000 && peak < 32767, "audio is audible and not clipping");
    printf(failures ? "%d CHECK(S) FAILED\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
