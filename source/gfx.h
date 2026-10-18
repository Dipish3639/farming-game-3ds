// Tiny software renderer shared by the 3DS build and the PC test harness.
#pragma once
#include <stdint.h>

typedef uint32_t u32;

// Colors are 0xAARRGGBB. Alpha 0 means "transparent" inside sprites.
#define RGB(c) (0xFF000000u | (u32)(c))

typedef struct {
    u32 *px;
    int w, h;
} Canvas;

typedef struct {
    u32 *px;
    int w, h;
    int opaque; // every pixel set: drawn with a fast row copy
} Sprite;

void cv_clear(Canvas *c, u32 col);
void cv_rect(Canvas *c, int x, int y, int w, int h, u32 col);
void cv_rect_alpha(Canvas *c, int x, int y, int w, int h, u32 col, int alpha);
void cv_frame(Canvas *c, int x, int y, int w, int h, u32 col);
void cv_pixel(Canvas *c, int x, int y, u32 col);
void cv_sprite(Canvas *c, const Sprite *s, int x, int y, int flip);
void cv_sprite_scaled(Canvas *c, const Sprite *s, int x, int y, int scale);
// Multiply every pixel channel by r/g/b (0..256).
void cv_tint(Canvas *c, int r, int g, int b);

int text_width(const char *str, int scale);
void cv_text(Canvas *c, int x, int y, const char *str, u32 col, int scale);
void cv_text_shadow(Canvas *c, int x, int y, const char *str, u32 col, u32 shadow, int scale);
void cv_text_center(Canvas *c, int cx, int y, const char *str, u32 col, int scale);

// Rounded "pixel" panel with a border, used for menus and HUD boxes.
void cv_panel(Canvas *c, int x, int y, int w, int h, u32 fill, u32 border);
