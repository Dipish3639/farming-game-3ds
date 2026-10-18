// Nintendo 3DS entry point: input from HID, game frames blitted straight to
// the LCD framebuffers.
#include <3ds.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "game.h"

static u32 top_px[TOP_W * SCREEN_H];
static u32 bot_px[BOT_W * SCREEN_H];

const char *plat_save_path(void)
{
    return "sdmc:/3ds/pixelfarm/save.dat";
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
    mkdir("sdmc:/3ds/pixelfarm", 0777);

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
        if (game->quit) break;
        game_render(game, &top, &bot);

        blit(GFX_TOP, &top);
        blit(GFX_BOTTOM, &bot);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    free(game);
    gfxExit();
    return 0;
}
