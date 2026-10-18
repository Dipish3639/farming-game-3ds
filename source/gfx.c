#include "gfx.h"
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
    for (int j = y0; j < y1; j++) {
        u32 *row = c->px + j * c->w;
        for (int i = x0; i < x1; i++) row[i] = blend(row[i], col, alpha);
    }
}

void cv_frame(Canvas *c, int x, int y, int w, int h, u32 col)
{
    cv_rect(c, x, y, w, 1, col);
    cv_rect(c, x, y + h - 1, w, 1, col);
    cv_rect(c, x, y, 1, h, col);
    cv_rect(c, x + w - 1, y, 1, h, col);
}

void cv_pixel(Canvas *c, int x, int y, u32 col)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = col;
}

void cv_sprite(Canvas *c, const Sprite *s, int x, int y, int flip)
{
    if (!s->px) return;
    for (int j = 0; j < s->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= c->h) continue;
        u32 *row = c->px + dy * c->w;
        const u32 *src = s->px + j * s->w;
        for (int i = 0; i < s->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= c->w) continue;
            u32 p = flip ? src[s->w - 1 - i] : src[i];
            if (p >> 24) row[dx] = p;
        }
    }
}

void cv_sprite_scaled(Canvas *c, const Sprite *s, int x, int y, int scale)
{
    if (!s->px) return;
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            u32 p = s->px[j * s->w + i];
            if (p >> 24) cv_rect(c, x + i * scale, y + j * scale, scale, scale, p);
        }
}

void cv_tint(Canvas *c, int r, int g, int b)
{
    int n = c->w * c->h;
    for (int i = 0; i < n; i++) {
        u32 p = c->px[i];
        u32 pr = (((p >> 16) & 0xFF) * r) >> 8;
        u32 pg = (((p >> 8) & 0xFF) * g) >> 8;
        u32 pb = ((p & 0xFF) * b) >> 8;
        if (pr > 255) pr = 255;
        if (pg > 255) pg = 255;
        if (pb > 255) pb = 255;
        c->px[i] = 0xFF000000u | (pr << 16) | (pg << 8) | pb;
    }
}

void cv_panel(Canvas *c, int x, int y, int w, int h, u32 fill, u32 border)
{
    cv_rect(c, x + 2, y, w - 4, h, border);
    cv_rect(c, x, y + 2, w, h - 4, border);
    cv_rect(c, x + 1, y + 1, w - 2, h - 2, border);
    cv_rect(c, x + 2, y + 2, w - 4, h - 4, fill);
}

// ---------------------------------------------------------------------------
// 5x7 pixel font (uppercase only; lowercase is drawn as uppercase).

typedef struct {
    char ch;
    const char *rows; // 35 chars, row-major, '#' = on
} Glyph;

static const Glyph GLYPHS[] = {
    {'A', ".###.#...##...#######...##...##...#"},
    {'B', "####.#...##...#####.#...##...#####."},
    {'C', ".###.#...##....#....#....#...#.###."},
    {'D', "####.#...##...##...##...##...#####."},
    {'E', "######....#....####.#....#....#####"},
    {'F', "######....#....####.#....#....#...."},
    {'G', ".###.#...##....#.####...##...#.####"},
    {'H', "#...##...##...#######...##...##...#"},
    {'I', ".###...#....#....#....#....#...###."},
    {'J', "..###...#....#....#....##..#..##..."},
    {'K', "#...##..#.#.#..##...#.#..#..#.#...#"},
    {'L', "#....#....#....#....#....#....#####"},
    {'M', "#...###.###.#.##.#.##...##...##...#"},
    {'N', "#...##...###..##.#.##..###...##...#"},
    {'O', ".###.#...##...##...##...##...#.###."},
    {'P', "####.#...##...#####.#....#....#...."},
    {'Q', ".###.#...##...##...##.#.##..#..##.#"},
    {'R', "####.#...##...#####.#.#..#..#.#...#"},
    {'S', ".#####....#.....###.....#....#####."},
    {'T', "#####..#....#....#....#....#....#.."},
    {'U', "#...##...##...##...##...##...#.###."},
    {'V', "#...##...##...##...##...#.#.#...#.."},
    {'W', "#...##...##...##.#.##.#.##.#.#.#.#."},
    {'X', "#...##...#.#.#...#...#.#.#...##...#"},
    {'Y', "#...##...#.#.#...#....#....#....#.."},
    {'Z', "#####....#...#...#...#...#....#####"},
    {'0', ".###.#...##..###.#.###..##...#.###."},
    {'1', "..#...##....#....#....#....#...###."},
    {'2', ".###.#...#....#...#...#...#...#####"},
    {'3', "#####...#...#.....#.....##...#.###."},
    {'4', "...#...##..#.#.#..#.#####...#....#."},
    {'5', "######....####.....#....##...#.###."},
    {'6', "..##..#...#....####.#...##...#.###."},
    {'7', "#####....#...#...#...#....#....#..."},
    {'8', ".###.#...##...#.###.#...##...#.###."},
    {'9', ".###.#...##...#.####....#...#..##.."},
    {'.', "..........................##...##.."},
    {',', ".....................##....#...#..."},
    {':', "......##...##........##...##......."},
    {'!', "..#....#....#....#....#.........#.."},
    {'?', ".###.#...#....#...#...#.........#.."},
    {'-', "...............#####..............."},
    {'+', ".......#....#..#####..#....#......."},
    {'/', ".........#...#...#...#...#........."},
    {'\'', "..#....#...#......................."},
    {'(', "...#...#...#....#....#.....#.....#."},
    {')', ".#.....#.....#....#....#...#...#..."},
    {'%', "##...##..#...#...#...#...#..##...##"},
    {'<', "...#...#...#...#.....#.....#.....#."},
    {'>', ".#.....#.....#.....#...#...#...#..."},
    {'=', "..........#####.....#####.........."},
    {'*', ".......#..#.#.#.###.#.#.#..#......."},
    {'#', ".#.#.#####.#.#..#.#.#####.#.#......"},
};

static unsigned char font_bits[128][7];
static int font_ready = 0;

static void font_init(void)
{
    memset(font_bits, 0, sizeof(font_bits));
    for (unsigned g = 0; g < sizeof(GLYPHS) / sizeof(GLYPHS[0]); g++) {
        const char *r = GLYPHS[g].rows;
        int len = (int)strlen(r);
        for (int i = 0; i < 35 && i < len; i++)
            if (r[i] == '#') font_bits[(int)GLYPHS[g].ch][i / 5] |= (unsigned char)(1 << (i % 5));
    }
    font_ready = 1;
}

int text_width(const char *str, int scale)
{
    int n = (int)strlen(str);
    return n ? (n * 6 - 1) * scale : 0;
}

void cv_text(Canvas *c, int x, int y, const char *str, u32 col, int scale)
{
    if (!font_ready) font_init();
    int cx = x;
    for (; *str; str++) {
        int ch = (unsigned char)*str;
        if (ch == '\n') {
            cx = x;
            y += 9 * scale;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 128)
            for (int r = 0; r < 7; r++) {
                unsigned char bits = font_bits[ch][r];
                if (!bits) continue;
                for (int i = 0; i < 5; i++)
                    if (bits & (1 << i)) {
                        if (scale == 1) cv_pixel(c, cx + i, y + r, col);
                        else cv_rect(c, cx + i * scale, y + r * scale, scale, scale, col);
                    }
            }
        cx += 6 * scale;
    }
}

void cv_text_shadow(Canvas *c, int x, int y, const char *str, u32 col, u32 shadow, int scale)
{
    cv_text(c, x + scale, y + scale, str, shadow, scale);
    cv_text(c, x, y, str, col, scale);
}

void cv_text_center(Canvas *c, int cx, int y, const char *str, u32 col, int scale)
{
    cv_text(c, cx - text_width(str, scale) / 2, y, str, col, scale);
}
