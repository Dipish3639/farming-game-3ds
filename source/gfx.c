#include "gfx.h"
#include "art.h"
#include <math.h>
#include <string.h>

void cv_clear(Canvas *c, u32 col)
{
    int n = c->w * c->h;
    for (int i = 0; i < n; i++) c->px[i] = col;
}

void cv_rect(Canvas *c, int x, int y, int w, int h, u32 col)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *row = c->px + j * c->w;
        for (int i = x0; i < x1; i++) row[i] = col;
    }
}

static inline u32 blend(u32 a, u32 b, int t)
{
    u32 rb = ((((a & 0xFF00FFu) * (u32)(256 - t)) + ((b & 0xFF00FFu) * (u32)t)) >> 8) & 0xFF00FFu;
    u32 g = ((((a & 0x00FF00u) * (u32)(256 - t)) + ((b & 0x00FF00u) * (u32)t)) >> 8) & 0x00FF00u;
    return 0xFF000000u | rb | g;
}

void cv_rect_alpha(Canvas *c, int x, int y, int w, int h, u32 col, int alpha)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
    if (alpha <= 0) return;
    if (alpha > 256) alpha = 256;
    for (int j = y0; j < y1; j++) {
        u32 *row = c->px + j * c->w;
        for (int i = x0; i < x1; i++) row[i] = blend(row[i], col, alpha);
    }
}

void cv_pixel(Canvas *c, int x, int y, u32 col)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = col;
}

void cv_pixel_alpha(Canvas *c, int x, int y, u32 col, int alpha)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h && alpha > 0)
        c->px[y * c->w + x] = blend(c->px[y * c->w + x], col, alpha > 256 ? 256 : alpha);
}

void cv_frame(Canvas *c, int x, int y, int w, int h, u32 col)
{
    cv_rect(c, x, y, w, 1, col);
    cv_rect(c, x, y + h - 1, w, 1, col);
    cv_rect(c, x, y, 1, h, col);
    cv_rect(c, x + w - 1, y, 1, h, col);
}

void cv_disc(Canvas *c, int cx, int cy, int r, u32 col)
{
    for (int y = -r; y <= r; y++) {
        int half = (int)sqrtf((float)(r * r - y * y) + 0.5f);
        cv_rect(c, cx - half, cy + y, half * 2 + 1, 1, col);
    }
}

void cv_line(Canvas *c, int x0, int y0, int x1, int y1, u32 col)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        cv_pixel(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void cv_quad(Canvas *c, const float *xs, const float *ys, u32 col)
{
    float ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < 4; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= c->h) y1 = c->h - 1;
    for (int y = y0; y <= y1; y++) {
        float yc = y + 0.5f, lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4; i++) {
            float xa = xs[i], ya = ys[i], xb = xs[(i + 1) & 3], yb = ys[(i + 1) & 3];
            if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc)) {
                float x = xa + (yc - ya) * (xb - xa) / (yb - ya);
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (lo > hi) continue;
        int xa = (int)ceilf(lo - 0.5f), xb = (int)floorf(hi - 0.5f);
        cv_rect(c, xa, y, xb - xa + 1, 1, col);
    }
}

void cv_glow(Canvas *c, int cx, int cy, int radius, u32 col, int strength)
{
    if (strength <= 0) return;
    int r2 = radius * radius;
    int cr = (col >> 16) & 0xFF, cg = (col >> 8) & 0xFF, cb = col & 0xFF;
    int y0 = cy - radius < 0 ? 0 : cy - radius, y1 = cy + radius >= c->h ? c->h - 1 : cy + radius;
    int x0 = cx - radius < 0 ? 0 : cx - radius, x1 = cx + radius >= c->w ? c->w - 1 : cx + radius;
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        u32 *row = c->px + y * c->w;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int f = 256 - (d2 << 8) / r2; // 256 at centre -> 0 at edge
            f = (f * f >> 8) * strength >> 8;
            u32 p = row[x];
            int r = (int)((p >> 16) & 0xFF) + (cr * f >> 8);
            int g = (int)((p >> 8) & 0xFF) + (cg * f >> 8);
            int b = (int)(p & 0xFF) + (cb * f >> 8);
            row[x] = 0xFF000000u | (u32)(r > 255 ? 255 : r) << 16 | (u32)(g > 255 ? 255 : g) << 8 | (u32)(b > 255 ? 255 : b);
        }
    }
}

// ---------------------------------------------------------------------------
// Art

int art_w(int img)
{
    return art_images[img].w;
}

int art_h(int img)
{
    return art_images[img].h;
}

static void art_row(Canvas *c, const uint8_t *src, int w, int x, int y, int flip, const u32 *pal)
{
    if (y < 0 || y >= c->h) return;
    int i0 = x < 0 ? -x : 0, i1 = x + w > c->w ? c->w - x : w;
    u32 *row = c->px + y * c->w + x;
    if (flip) {
        for (int i = i0; i < i1; i++) {
            uint8_t p = src[w - 1 - i];
            if (p) row[i] = pal[p];
        }
    } else {
        for (int i = i0; i < i1; i++) {
            uint8_t p = src[i];
            if (p) row[i] = pal[p];
        }
    }
}

void cv_art(Canvas *c, int img, int x, int y, int flip, const u32 *pal)
{
    const ArtImage *a = &art_images[img];
    const uint8_t *px = art_pixels + a->offset;
    for (int j = 0; j < a->h; j++) art_row(c, px + j * a->w, a->w, x, y + j, flip, pal);
}

void cv_art_sway(Canvas *c, int img, int x, int y, float sway, const u32 *pal)
{
    const ArtImage *a = &art_images[img];
    const uint8_t *px = art_pixels + a->offset;
    for (int j = 0; j < a->h; j++) {
        float up = 1.0f - (float)j / a->h; // 0 at the roots, 1 at the top
        int off = (int)floorf(sway * up * up + 0.5f);
        art_row(c, px + j * a->w, a->w, x + off, y + j, 0, pal);
    }
}

void cv_art_scaled(Canvas *c, int img, int x, int y, int scale, const u32 *pal)
{
    const ArtImage *a = &art_images[img];
    const uint8_t *px = art_pixels + a->offset;
    for (int j = 0; j < a->h; j++)
        for (int i = 0; i < a->w; i++) {
            uint8_t p = px[j * a->w + i];
            if (p) cv_rect(c, x + i * scale, y + j * scale, scale, scale, pal[p]);
        }
}

void cv_art_mask(Canvas *c, int img, int x, int y, u32 col, int alpha)
{
    const ArtImage *a = &art_images[img];
    const uint8_t *px = art_pixels + a->offset;
    for (int j = 0; j < a->h; j++)
        for (int i = 0; i < a->w; i++)
            if (px[j * a->w + i]) {
                if (alpha >= 256) cv_pixel(c, x + i, y + j, col);
                else cv_pixel_alpha(c, x + i, y + j, col, alpha);
            }
}

// ---------------------------------------------------------------------------
// Text

int text_width(const char *str, int scale)
{
    int w = 0, n = 0;
    for (; *str; str++, n++) w += font_width[(unsigned char)*str & 127] + 1;
    return n ? (w - 1) * scale : 0;
}

void cv_text(Canvas *c, int x, int y, const char *str, u32 col, int scale)
{
    for (; *str; str++) {
        int ch = (unsigned char)*str & 127, w = font_width[ch];
        for (int r = 0; r < 9; r++) {
            unsigned bits = font_rows[ch][r];
            for (int i = 0; bits; i++, bits >>= 1)
                if (bits & 1) {
                    if (scale == 1) cv_pixel(c, x + i, y + r, col);
                    else cv_rect(c, x + i * scale, y + r * scale, scale, scale, col);
                }
        }
        x += (w + 1) * scale;
    }
}

void cv_text_shadow(Canvas *c, int x, int y, const char *str, u32 col, u32 shadow, int scale)
{
    cv_text(c, x, y + scale, str, shadow, scale);
    cv_text(c, x, y, str, col, scale);
}

void cv_text_outline(Canvas *c, int x, int y, const char *str, u32 col, u32 outline, int scale)
{
    static const int d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int i = 0; i < 8; i++) cv_text(c, x + d[i][0], y + d[i][1], str, outline, scale);
    cv_text(c, x, y + 1 + scale, str, outline, scale);
    cv_text(c, x, y, str, col, scale);
}

void cv_text_center(Canvas *c, int cx, int y, const char *str, u32 col, int scale)
{
    cv_text(c, cx - text_width(str, scale) / 2, y, str, col, scale);
}
