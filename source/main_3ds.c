// Nintendo 3DS entry point: input from HID, game frames blitted straight to
// the LCD framebuffers.
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "audio.h"
#include "game.h"

static u32 top_px[TOP_W * SCREEN_H];
static u32 bot_px[BOT_W * SCREEN_H];

const char *plat_save_path(void)
{
    return "sdmc:/3ds/tinyharvest/save.dat";
}

// The 3DS LCDs are portrait panels rotated 90 degrees: the framebuffer is
// column-major, 240 pixels per column from bottom to top, BGR8.
static void blit(gfxScreen_t screen, const Canvas *cv)
{
    u16 fw, fh;
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fw, &fh);
    for (int x = 0; x < cv->w; x++) {
        u8 *col = fb + x * SCREEN_H * 3;
        const u32 *src = cv->px + (SCREEN_H - 1) * cv->w + x;
        for (int y = 0; y < SCREEN_H; y++, src -= cv->w) {
            u32 c = *src;
            col[0] = (u8)c;
            col[1] = (u8)(c >> 8);
            col[2] = (u8)(c >> 16);
            col += 3;
        }
    }
}

// Audio: the synth fills a ring of small DSP wave buffers from the main loop.
// ndsp needs the DSP firmware dump (sdmc:/3ds/dspfirm.cdc); without it the
// game simply runs silently.
#define AUDIO_BUFS 6
#define AUDIO_BUF_SAMPLES 512

static ndspWaveBuf wave_bufs[AUDIO_BUFS];
static int16_t *audio_mem;
static int audio_ok;

static void audio_start(void)
{
    audio_init();
    if (R_FAILED(ndspInit())) return;
    audio_mem = (int16_t *)linearAlloc(AUDIO_BUFS * AUDIO_BUF_SAMPLES * sizeof(int16_t));
    if (!audio_mem) {
        ndspExit();
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, AUDIO_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    memset(wave_bufs, 0, sizeof(wave_bufs));
    for (int i = 0; i < AUDIO_BUFS; i++) {
        wave_bufs[i].data_vaddr = audio_mem + i * AUDIO_BUF_SAMPLES;
        wave_bufs[i].nsamples = AUDIO_BUF_SAMPLES;
        wave_bufs[i].status = NDSP_WBUF_DONE;
    }
    audio_ok = 1;
}

static void audio_pump(void)
{
    if (!audio_ok) return;
    for (int i = 0; i < AUDIO_BUFS; i++) {
        ndspWaveBuf *wb = &wave_bufs[i];
        if (wb->status != NDSP_WBUF_DONE) continue;
        int16_t *data = audio_mem + i * AUDIO_BUF_SAMPLES;
        audio_render(data, AUDIO_BUF_SAMPLES);
        DSP_FlushDataCache(data, AUDIO_BUF_SAMPLES * sizeof(int16_t));
        ndspChnWaveBufAdd(0, wb);
    }
}

static void audio_stop(void)
{
    if (!audio_ok) return;
    ndspChnWaveBufClear(0);
    ndspExit();
    linearFree(audio_mem);
}

static uint32_t map_keys(u32 k)
{
    uint32_t b = 0;
    if (k & KEY_A) b |= BTN_A;
    if (k & KEY_B) b |= BTN_B;
    if (k & KEY_X) b |= BTN_X;
    if (k & KEY_Y) b |= BTN_Y;
    if (k & KEY_L) b |= BTN_L;
    if (k & KEY_R) b |= BTN_R;
    if (k & KEY_START) b |= BTN_START;
    if (k & KEY_SELECT) b |= BTN_SELECT;
    if (k & KEY_UP) b |= BTN_UP;
    if (k & KEY_DOWN) b |= BTN_DOWN;
    if (k & KEY_LEFT) b |= BTN_LEFT;
    if (k & KEY_RIGHT) b |= BTN_RIGHT;
    return b;
}

int main(void)
{
    gfxInitDefault();
    osSetSpeedupEnable(true);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/tinyharvest", 0777);

    audio_start();
    Game *game = (Game *)malloc(sizeof(Game));
    game_init(game);
    Canvas top = {top_px, TOP_W, SCREEN_H};
    Canvas bot = {bot_px, BOT_W, SCREEN_H};

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        Input in = {0};
        in.down = map_keys(down);
        in.held = map_keys(held);
        if (held & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            in.touching = 1;
            in.touch_down = (down & KEY_TOUCH) != 0;
            in.tx = tp.px;
            in.ty = tp.py;
        }

        game_update(game, &in);
        audio_pump();
        if (game->quit) break;
        game_render(game, &top, &bot);

        blit(GFX_TOP, &top);
        blit(GFX_BOTTOM, &bot);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    game_shutdown(game);
    free(game);
    audio_stop();
    gfxExit();
    return 0;
}
