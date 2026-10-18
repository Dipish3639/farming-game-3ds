#include "sprites.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Sprite S_PLAYER[3][3];
Sprite S_ICON[NUM_ITEMS];
Sprite S_STAGE[NUM_STAGES];
Sprite S_RIPE[NUM_CROPS];
Sprite S_DEAD;
Sprite S_STONE, S_TWIG, S_SPRINKLER;
Sprite S_TREE, S_WEED;
Sprite S_GRASS[4];
Sprite S_DIRT[2];
Sprite S_TILLED, S_WATERED;
Sprite S_WATER[4];

#define ROWS(a) (int)(sizeof(a) / sizeof(a[0]))

// ---------------------------------------------------------------------------
// Art. Each char is a palette entry; '.' is transparent.

static const char *ART_PLAYER_DOWN[] = {
    "................",
    "....kkkkkkkk....",
    "...kaaaaaaaak...",
    "...kaaaaaaaak...",
    "...kaffffffak...",
    "...kfkffffkfk...",
    "...kffffffffk...",
    "....kFffffFk....",
    "...keeeeeeeek...",
    "..keeeeeeeeeek..",
    "..kfeeeeeeeefk..",
    "..kkEEEEEEEEkk..",
    "...kjjjjjjjjk...",
    "...kjjjkkjjjk...",
    "...kjjk..kjjk...",
    "....kk....kk....",
};

static const char *ART_PLAYER_UP[] = {
    "................",
    "....kkkkkkkk....",
    "...kaaaaaaaak...",
    "...kaaaaaaaak...",
    "...kaaaaaaaak...",
    "...kaaaaaaaak...",
    "...kaaaaaaaak...",
    "....kaaaaaak....",
    "...keeeeeeeek...",
    "..keeeeeeeeeek..",
    "..kfeeeeeeeefk..",
    "..kkEEEEEEEEkk..",
    "...kjjjjjjjjk...",
    "...kjjjkkjjjk...",
    "...kjjk..kjjk...",
    "....kk....kk....",
};

static const char *ART_PLAYER_SIDE[] = {
    "................",
    ".....kkkkkk.....",
    "....kaaaaaak....",
    "...kaaaaaaaak...",
    "...kaaaaffffk...",
    "...kaaaffkffk...",
    "...kaaafffffk...",
    "....kaafffFk....",
    "....keeeeeek....",
    "...keeeeeeeek...",
    "...keeeefeeek...",
    "...kEEEEEEEEk...",
    "....kjjjjjjk....",
    "....kjjjjjjk....",
    "....kjjkkjjk....",
    ".....kk..kk.....",
};

// Leg rows (13..15) for walking frames.
static const char *LEGS_FRONT_A[] = {"...kjjjkkjjjk...", "...kjjk..kkk....", "....kk.........."};
static const char *LEGS_FRONT_B[] = {"...kjjjkkjjjk...", "....kkk..kjjk...", "..........kk...."};
static const char *LEGS_SIDE_A[] = {"...kjjk..kjjk...", "..kjjk....kjjk..", "..kk........kk.."};

static const char *ART_HOE[] = {
    "................",
    "................",
    "..........kkkk..",
    ".........kssssk.",
    "........knkkssk.",
    ".......knk..ksk.",
    "......knk....k..",
    ".....knk........",
    "....knk.........",
    "...knk..........",
    "..knk...........",
    ".knk............",
    ".kk.............",
    "................",
    "................",
    "................",
};

static const char *ART_CAN[] = {
    "................",
    "................",
    "......kkkk......",
    ".....kS..Sk.....",
    ".k...kS..Sk.....",
    "kbk..kkkkkk.....",
    ".kbkksssssskk...",
    "..kbksllsssssk..",
    "...kksllsssssk..",
    "....ksssssssSk..",
    "....ksssssssSk..",
    "....kSSSSSSSSk..",
    ".....kkkkkkkk...",
    "................",
    "................",
    "................",
};

static const char *ART_SCYTHE[] = {
    "................",
    "....kkkkkk......",
    "...kllllllkk....",
    "..kllkkkkkllk...",
    "..klk.....kk....",
    "..kk......knk...",
    "...........knk..",
    "...........knk..",
    "..........knk...",
    "..........knk...",
    ".........knk....",
    ".........knk....",
    "........knk.....",
    "........kk......",
    "................",
    "................",
};

static const char *ART_PICK[] = {
    "................",
    "...kkkkkkkk.....",
    "..kssssssssk....",
    ".kskkkknkkksk...",
    ".kk....knk..kk..",
    ".......knk......",
    ".......knk......",
    ".......knk......",
    ".......knk......",
    ".......knk......",
    ".......knk......",
    ".......knk......",
    ".......kkk......",
    "................",
    "................",
    "................",
};

static const char *ART_SEEDBAG[] = {
    "................",
    "................",
    ".....kkkkkk.....",
    "......knnk......",
    ".....kmmmmk.....",
    "....kmmmmmmk....",
    "...kmmmmmmmmk...",
    "...kmkkkkkkmk...",
    "...kmkvppqkmk...",
    "...kmkppppkmk...",
    "...kmkpqqqkmk...",
    "...kmkkkkkkmk...",
    "...knmmmmmmnk...",
    "....knnnnnnk....",
    ".....kkkkkk.....",
    "................",
};

static const char *ART_ROOT[] = {
    "................",
    "......z..z......",
    ".......zz.......",
    "......kzzk......",
    ".....kvppqk.....",
    ".....kvppqk.....",
    ".....kpppqk.....",
    "......kppqk.....",
    "......kppqk.....",
    "......kpqk......",
    ".......kqk......",
    ".......kqk......",
    "........k.......",
    "................",
    "................",
    "................",
};

static const char *ART_ROUND[] = {
    "................",
    "................",
    ".......zz.......",
    "....kkkzzkkk....",
    "...kvvpzzppqk...",
    "..kvvppppppqqk..",
    "..kvpppppppqqk..",
    ".kvpppppppppqqk.",
    ".kppppppppppqqk.",
    ".kppppppppppqqk.",
    ".kpppppppppqqqk.",
    "..kppppppppqqk..",
    "..kqpppppqqqqk..",
    "...kqqqqqqqqk...",
    "....kkkkkkkk....",
    "................",
};

static const char *ART_BERRY[] = {
    "................",
    "................",
    ".......zz.......",
    "......zzzz......",
    "...kkk.zz.kkk...",
    "..kvppk..kvppk..",
    "..kppqk..kppqk..",
    "..kpqqkkkkpqqk..",
    "...kkkvppkkkk...",
    ".....kvpppk.....",
    ".....kpppqk.....",
    ".....kppqqk.....",
    "......kqqk......",
    ".......kk.......",
    "................",
    "................",
};

static const char *ART_CORN[] = {
    "................",
    "........z.......",
    ".......kvk......",
    "......kvpqk.....",
    "......kvpqk.....",
    ".....zkpvqkz....",
    ".....zkvpqkz....",
    "....zzkpvqkzz...",
    "....zZkvpqkZz...",
    "....zZkpvqkZz...",
    ".....zZkqkZz....",
    ".....zZZkZZz....",
    "......zZZZz.....",
    ".......zzz......",
    "................",
    "................",
};

static const char *ART_EGG[] = {
    "................",
    "......zzz.......",
    ".....zzzzz......",
    "......kzk.......",
    ".....kvppk......",
    "....kvpppqk.....",
    "....kvpppqk.....",
    "...kvppppqqk....",
    "...kpppppqqk....",
    "...kpppppqqk....",
    "...kppppqqqk....",
    "....kpppqqk.....",
    ".....kqqqk......",
    "......kkk.......",
    "................",
    "................",
};

static const char *ART_LEAFY[] = {
    "................",
    "................",
    "................",
    ".....kkkkkk.....",
    "...kkvvppvvkk...",
    "..kvvppvvppqqk..",
    "..kvppppppppqk..",
    ".zkppvvppppqqkz.",
    "zZkqppppppqqqkZz",
    "zZZkqqqqqqqqkZZz",
    ".zZZkkkkkkkkZZz.",
    "..zZZZZZZZZZZz..",
    "....zzzzzzzz....",
    "................",
    "................",
    "................",
};

static const char *ART_SEED[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....mm....mm....",
    "....NN....NN....",
    "................",
    ".......mm.......",
    ".......NN.......",
    "..mm........mm..",
    "..NN........NN..",
    "................",
    "................",
};

static const char *ART_SPROUT[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".......z.z......",
    "......zZzZz.....",
    ".......zZ.......",
    "........Z.......",
    "........Z.......",
    "................",
    "................",
    "................",
};

static const char *ART_YOUNG[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "......z...z.....",
    ".....zzz.zzz....",
    "....zzZzzzZzz...",
    ".....zzZ.Zzz....",
    "...z...ZZZ...z..",
    "..zzz..ZZ..zzz..",
    "...zZZ.Z..ZZz...",
    ".....ZZZZZZ.....",
    ".......ZZ.......",
    "................",
    "................",
};

static const char *ART_MATURE[] = {
    "................",
    "................",
    "......z..z......",
    ".....zzz.zzz....",
    "....zzZzzzZzz...",
    "..z..zzZzZzz..z.",
    ".zzz..zZZZz..zzz",
    "zzZzz..ZZZ..zzZz",
    ".zZZzz.ZZ.zzZZz.",
    "..zZZZzZZzZZZz..",
    "...zZZZZZZZZz...",
    ".....ZZZZZZ.....",
    ".......ZZ.......",
    "................",
    "................",
    "................",
};

static const char *ART_BLOB[] = {
    ".kk.",
    "kvpk",
    "kpqk",
    ".kk.",
};

static const char *ART_WEED[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....z.....z.....",
    "...zz.z..zz.....",
    "..zZz.zz.zZ.z...",
    "..zZzzZz.zZzz...",
    "...zZZzZzZZz....",
    "....zZZZZZz.....",
    ".....ZZZZZ......",
    "................",
    "................",
};

static const char *ART_STONE[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....kkkkk......",
    "...kkllssskk....",
    "..klllsssssSk...",
    ".klsssssssSSSk..",
    ".kssssssSSSSSk..",
    ".kSsssSSSSSSSk..",
    "..kkSSSSSSSSk...",
    "....kkkkkkkk....",
};

static const char *ART_TWIG[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....kk....knk...",
    "...knnkkkknmnk..",
    "....kkmnnnnkkk..",
    "......kkkkk.....",
    "................",
};

static const char *ART_SPRINKLER[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".......kk.......",
    "......kcck......",
    ".......kk.......",
    "....kkkllkkk....",
    "...klsslssslk...",
    "...kllkllkllk...",
    "...kSSSSSSSSk...",
    "....kkkkkkkk....",
    "................",
    "................",
};

static const char *ART_TREE[] = {
    "......kkkk......",
    "....kkzzzzkk....",
    "...kzzhzzzzzk...",
    "..kzhhzzzzzzZk..",
    ".kzhhzzzzzzzZZk.",
    ".kzhzzzzzzzzZZk.",
    "kzzzzzzzzzzZZZZk",
    "kzzzzzzzzzZZZZZk",
    "kzzzzzzzZzZZZZZk",
    "kzZzzzzzZZZZZZZk",
    "kZzzzzZZZZZZZZZk",
    ".kZZZZZZZZZZZZk.",
    "..kkZZZZZZZZkk..",
    "....kkknnkkk....",
    "......knNk......",
    "......knNk......",
    "......knNk......",
    ".....knnNNk.....",
    "....kk.kk.kk....",
};

// ---------------------------------------------------------------------------

typedef struct {
    u32 p, q, v;     // produce colors
    u32 z, Z, h;     // greens
} Pal;

static const Pal CROP_GREENS = {0, 0, 0, RGB(0x4fae3a), RGB(0x2e7a2e), RGB(0x8fd14f)};

static u32 pal_color(char ch, const Pal *pal)
{
    switch (ch) {
    case 'k': return RGB(0x2b1d14);
    case 'w': return RGB(0xf4f4e8);
    case 'n': return RGB(0x9a6332);
    case 'N': return RGB(0x6b4220);
    case 'm': return RGB(0xc48a4a);
    case 's': return RGB(0x9c9a98);
    case 'S': return RGB(0x6a6866);
    case 'l': return RGB(0xcac8c4);
    case 'f': return RGB(0xf2c39a);
    case 'F': return RGB(0xd49a72);
    case 'a': return RGB(0x7a4220);
    case 'e': return RGB(0x3f6fc8);
    case 'E': return RGB(0x2a4a8a);
    case 'j': return RGB(0x5a4a3a);
    case 'b': return RGB(0x3f7fd6);
    case 'c': return RGB(0x9fd3ff);
    case 'p': return pal->p;
    case 'q': return pal->q;
    case 'v': return pal->v;
    case 'z': return pal->z;
    case 'Z': return pal->Z;
    case 'h': return pal->h;
    default: return 0;
    }
}

static Sprite blank(int w, int h)
{
    Sprite s;
    s.w = w;
    s.h = h;
    s.px = (u32 *)calloc((size_t)(w * h), sizeof(u32));
    return s;
}

static void spr_free(Sprite *s)
{
    free(s->px);
    s->px = NULL;
}

static Sprite build(const char *const *rows, int h, const Pal *pal)
{
    int w = (int)strlen(rows[0]);
    Sprite s = blank(w, h);
    for (int y = 0; y < h; y++) {
        int len = (int)strlen(rows[y]);
#ifndef __3DS__
        if (len != w) fprintf(stderr, "sprite row %d has width %d, expected %d: %s\n", y, len, w, rows[y]);
#endif
        for (int x = 0; x < w && x < len; x++) s.px[y * w + x] = pal_color(rows[y][x], pal);
    }
    return s;
}

static void overlay(Sprite *dst, const Sprite *src, int ox, int oy)
{
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++) {
            int dx = x + ox, dy = y + oy;
            u32 p = src->px[y * src->w + x];
            if ((p >> 24) && dx >= 0 && dy >= 0 && dx < dst->w && dy < dst->h) dst->px[dy * dst->w + dx] = p;
        }
}

static Sprite build_player(const char *const *base, const char *const *legs)
{
    const char *rows[16];
    for (int i = 0; i < 16; i++) rows[i] = base[i];
    if (legs)
        for (int i = 0; i < 3; i++) rows[13 + i] = legs[i];
    return build(rows, 16, &CROP_GREENS);
}

static const char *const *produce_art(int tmpl)
{
    switch (tmpl) {
    case TM_ROOT: return ART_ROOT;
    case TM_ROUND: return ART_ROUND;
    case TM_BERRY: return ART_BERRY;
    case TM_CORN: return ART_CORN;
    case TM_EGG: return ART_EGG;
    default: return ART_LEAFY;
    }
}

void spr_init(void)
{
    S_PLAYER[0][0] = build_player(ART_PLAYER_DOWN, NULL);
    S_PLAYER[0][1] = build_player(ART_PLAYER_DOWN, LEGS_FRONT_A);
    S_PLAYER[0][2] = build_player(ART_PLAYER_DOWN, LEGS_FRONT_B);
    S_PLAYER[1][0] = build_player(ART_PLAYER_UP, NULL);
    S_PLAYER[1][1] = build_player(ART_PLAYER_UP, LEGS_FRONT_A);
    S_PLAYER[1][2] = build_player(ART_PLAYER_UP, LEGS_FRONT_B);
    S_PLAYER[2][0] = build_player(ART_PLAYER_SIDE, NULL);
    S_PLAYER[2][1] = build_player(ART_PLAYER_SIDE, LEGS_SIDE_A);
    S_PLAYER[2][2] = build_player(ART_PLAYER_SIDE, NULL);

    S_ICON[IT_HOE] = build(ART_HOE, ROWS(ART_HOE), &CROP_GREENS);
    S_ICON[IT_CAN] = build(ART_CAN, ROWS(ART_CAN), &CROP_GREENS);
    S_ICON[IT_SCYTHE] = build(ART_SCYTHE, ROWS(ART_SCYTHE), &CROP_GREENS);
    S_ICON[IT_PICK] = build(ART_PICK, ROWS(ART_PICK), &CROP_GREENS);
    S_SPRINKLER = build(ART_SPRINKLER, ROWS(ART_SPRINKLER), &CROP_GREENS);
    S_ICON[IT_SPRINKLER] = build(ART_SPRINKLER, ROWS(ART_SPRINKLER), &CROP_GREENS);
    S_STONE = build(ART_STONE, ROWS(ART_STONE), &CROP_GREENS);
    S_TWIG = build(ART_TWIG, ROWS(ART_TWIG), &CROP_GREENS);

    S_STAGE[STAGE_SEED] = build(ART_SEED, 16, &CROP_GREENS);
    S_STAGE[STAGE_SPROUT] = build(ART_SPROUT, 16, &CROP_GREENS);
    S_STAGE[STAGE_YOUNG] = build(ART_YOUNG, 16, &CROP_GREENS);
    S_STAGE[STAGE_MATURE] = build(ART_MATURE, 16, &CROP_GREENS);

    Pal dead = {0, 0, 0, RGB(0xa07a48), RGB(0x6e5030), RGB(0xc09a60)};
    S_DEAD = build(ART_YOUNG, 16, &dead);

    for (int i = 0; i < NUM_CROPS; i++) {
        const CropDef *c = &CROPS[i];
        Pal pal = CROP_GREENS;
        pal.p = c->p;
        pal.q = c->q;
        pal.v = c->v;
        Sprite icon = build(produce_art(c->tmpl), 16, &pal);
        S_ICON[IT_SEED0 + i] = build(ART_SEEDBAG, 16, &pal);
        S_ICON[IT_CROP0 + i] = icon;

        Sprite ripe = blank(16, 16);
        if (c->ripe == RIPE_ON_GROUND) {
            overlay(&ripe, &S_STAGE[STAGE_YOUNG], 0, 2);
            overlay(&ripe, &icon, 0, 1);
        } else if (c->ripe == RIPE_TALL) {
            overlay(&ripe, &S_STAGE[STAGE_MATURE], 0, 2);
            overlay(&ripe, &icon, 0, -2);
        } else {
            static const int spots[5][2] = {{2, 6}, {10, 4}, {6, 8}, {11, 9}, {5, 3}};
            Sprite blob = build(ART_BLOB, 4, &pal);
            overlay(&ripe, &S_STAGE[STAGE_MATURE], 0, 1);
            for (int b = 0; b < 5; b++) overlay(&ripe, &blob, spots[b][0], spots[b][1]);
            spr_free(&blob);
        }
        S_RIPE[i] = ripe;
    }
    spr_set_season(SEASON_SPRING);
}

// ---------------------------------------------------------------------------
// Procedural ground tiles

static unsigned tile_rng;
static int rnd(int n)
{
    tile_rng = tile_rng * 1103515245u + 12345u;
    return (int)((tile_rng >> 16) % (unsigned)n);
}

static const u32 GRASS[4][3] = {
    {RGB(0x6abe30), RGB(0x55a428), RGB(0x8fd14f)},
    {RGB(0x4fa83a), RGB(0x3e902e), RGB(0x78c050)},
    {RGB(0xb4a046), RGB(0x968034), RGB(0xd0bc62)},
    {RGB(0xe6eef6), RGB(0xc6d4e2), RGB(0xffffff)},
};

u32 season_grass(int season, int shade)
{
    return GRASS[season & 3][shade];
}

static Sprite noise_tile(u32 base, u32 dark, u32 light, int ndark, int nlight, unsigned seed)
{
    Sprite s = blank(16, 16);
    for (int i = 0; i < 256; i++) s.px[i] = base;
    tile_rng = seed;
    for (int i = 0; i < ndark; i++) s.px[rnd(256)] = dark;
    for (int i = 0; i < nlight; i++) s.px[rnd(256)] = light;
    return s;
}

static void make_grass(int season)
{
    const u32 *g = GRASS[season];
    static const u32 flowers[4][2] = {
        {RGB(0xffffff), RGB(0xf2d34b)},
        {RGB(0xf2d34b), RGB(0xe84a8a)},
        {RGB(0xe8862a), RGB(0xb04a1a)},
        {RGB(0xc6d4e2), RGB(0xc6d4e2)},
    };
    for (int v = 0; v < 4; v++) {
        spr_free(&S_GRASS[v]);
        S_GRASS[v] = noise_tile(g[0], g[1], g[2], 14, 6, 77u + (unsigned)v * 31u);
        // small grass tufts
        int tufts = v == 0 ? 3 : 1 + v % 2;
        for (int t = 0; t < tufts; t++) {
            int x = 1 + rnd(14), y = 2 + rnd(13);
            S_GRASS[v].px[y * 16 + x] = g[1];
            S_GRASS[v].px[(y - 1) * 16 + x - 1] = g[1];
            S_GRASS[v].px[(y - 1) * 16 + x + 1] = g[1];
            S_GRASS[v].px[(y - 2) * 16 + x] = g[2];
        }
        if (v == 3) {
            int x = 3 + rnd(10), y = 3 + rnd(10);
            S_GRASS[v].px[y * 16 + x] = flowers[season][1];
            S_GRASS[v].px[(y - 1) * 16 + x] = flowers[season][0];
            S_GRASS[v].px[(y + 1) * 16 + x] = flowers[season][0];
            S_GRASS[v].px[y * 16 + x - 1] = flowers[season][0];
            S_GRASS[v].px[y * 16 + x + 1] = flowers[season][0];
        }
    }
}

static Sprite make_soil(u32 base, u32 furrow, u32 light)
{
    Sprite s = noise_tile(base, furrow, light, 10, 8, 999u);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int r = y % 5;
            if (r == 3) s.px[y * 16 + x] = furrow;
            else if (r == 4 && (x + y) % 3) s.px[y * 16 + x] = light;
        }
    // dark border so tilled patches read as separate plots
    for (int i = 0; i < 16; i++) {
        s.px[i] = furrow;
        s.px[15 * 16 + i] = furrow;
        s.px[i * 16] = furrow;
        s.px[i * 16 + 15] = furrow;
    }
    return s;
}

static void make_static_ground(void)
{
    for (int v = 0; v < 2; v++) {
        spr_free(&S_DIRT[v]);
        S_DIRT[v] = noise_tile(RGB(0xcf9c5e), RGB(0xb07e44), RGB(0xe4b87c), 18, 10, 5u + (unsigned)v * 17u);
    }
    spr_free(&S_TILLED);
    spr_free(&S_WATERED);
    S_TILLED = make_soil(RGB(0x8e5c34), RGB(0x6a4024), RGB(0xa8744a));
    S_WATERED = make_soil(RGB(0x5c3a22), RGB(0x3e2614), RGB(0x6e4a2e));

    for (int f = 0; f < 4; f++) {
        spr_free(&S_WATER[f]);
        Sprite s = blank(16, 16);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                u32 c = RGB(0x3f7fd6);
                if ((x + y) % 16 < 3 && y % 8 == 2) c = RGB(0x5a9ae8);
                int wx = (x + f * 4) % 16;
                if (y % 8 == 5 && wx >= 2 && wx < 6) c = RGB(0x9fd3ff);
                if ((y + 4) % 8 == 5 && (wx + 8) % 16 >= 2 && (wx + 8) % 16 < 5) c = RGB(0x7ab8f4);
                s.px[y * 16 + x] = c;
            }
        S_WATER[f] = s;
    }
}

void spr_set_season(int season)
{
    static const Pal TREE_PAL[4] = {
        {0, 0, 0, RGB(0x4fae3a), RGB(0x2e7a2e), RGB(0x8fd14f)},
        {0, 0, 0, RGB(0x3a9a34), RGB(0x22662a), RGB(0x6ac050)},
        {0, 0, 0, RGB(0xe0782a), RGB(0xa84a1a), RGB(0xf0b040)},
        {0, 0, 0, RGB(0xf0f6fc), RGB(0x9aaabb), RGB(0xffffff)},
    };
    static const Pal WEED_PAL[4] = {
        {0, 0, 0, RGB(0x3c9a2c), RGB(0x24661e), RGB(0x6ac050)},
        {0, 0, 0, RGB(0x2e8a26), RGB(0x1c561a), RGB(0x5ab040)},
        {0, 0, 0, RGB(0xb07a2a), RGB(0x7a4a1a), RGB(0xd09a40)},
        {0, 0, 0, RGB(0x8a7050), RGB(0x5a4630), RGB(0xa89070)},
    };
    season &= 3;
    spr_free(&S_TREE);
    spr_free(&S_WEED);
    S_TREE = build(ART_TREE, ROWS(ART_TREE), &TREE_PAL[season]);
    S_WEED = build(ART_WEED, ROWS(ART_WEED), &WEED_PAL[season]);
    make_grass(season);
    make_static_ground();
}
