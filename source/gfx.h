// Tiny software renderer shared by the 3DS build and the PC test harness.
#pragma once
#include <stdint.h>

typedef uint32_t u32;

// Colors are 0xAARRGGBB.
#define RGB(c) (0xFF000000u | (u32)(c))

typedef struct {
    u32 *px;
    int w, h;
} Canvas;

void cv_clear(Canvas *c, u32 col);
void cv_rect(Canvas *c, int x, int y, int w, int h, u32 col);
void cv_rect_alpha(Canvas *c, int x, int y, int w, int h, u32 col, int alpha);
void cv_pixel(Canvas *c, int x, int y, u32 col);
void cv_pixel_alpha(Canvas *c, int x, int y, u32 col, int alpha);
void cv_frame(Canvas *c, int x, int y, int w, int h, u32 col);
void cv_disc(Canvas *c, int cx, int cy, int r, u32 col);
void cv_line(Canvas *c, int x0, int y0, int x1, int y1, u32 col);
void cv_quad(Canvas *c, const float *xs, const float *ys, u32 col);
// Additive light with a soft falloff; strength 0..256.
void cv_glow(Canvas *c, int cx, int cy, int radius, u32 col, int strength);

// Art from art.c, drawn through a palette (so it can be tinted per frame).
void cv_art(Canvas *c, int img, int x, int y, int flip, const u32 *pal);
void cv_art_scaled(Canvas *c, int img, int x, int y, int scale, const u32 *pal);
// Plants: rows shift sideways more toward the top, like swaying in the wind.
void cv_art_sway(Canvas *c, int img, int x, int y, float sway, const u32 *pal);
// Draws every opaque pixel in one colour (silhouettes, shadows).
void cv_art_mask(Canvas *c, int img, int x, int y, u32 col, int alpha);
int art_w(int img);
int art_h(int img);

// Pixel font (proportional, 9 rows incl. descenders).
int text_width(const char *str, int scale);
void cv_text(Canvas *c, int x, int y, const char *str, u32 col, int scale);
void cv_text_shadow(Canvas *c, int x, int y, const char *str, u32 col, u32 shadow, int scale);
void cv_text_outline(Canvas *c, int x, int y, const char *str, u32 col, u32 outline, int scale);
void cv_text_center(Canvas *c, int cx, int y, const char *str, u32 col, int scale);
