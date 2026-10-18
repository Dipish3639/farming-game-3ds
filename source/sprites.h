#pragma once
#include "gfx.h"
#include "data.h"

enum { STAGE_SEED, STAGE_SPROUT, STAGE_YOUNG, STAGE_MATURE, NUM_STAGES };

// [facing: 0 down, 1 up, 2 side(right)] [frame: 0 stand, 1 step A, 2 step B]
extern Sprite S_PLAYER[3][3];
extern Sprite S_ICON[NUM_ITEMS];
extern Sprite S_STAGE[NUM_STAGES];
extern Sprite S_RIPE[NUM_CROPS];
extern Sprite S_DEAD;
extern Sprite S_STONE, S_TWIG, S_SPRINKLER;

// Season dependent (rebuilt by spr_set_season)
extern Sprite S_TREE, S_WEED;
extern Sprite S_GRASS[4];
extern Sprite S_DIRT[2];
extern Sprite S_TILLED, S_WATERED;
extern Sprite S_WATER[4];

void spr_init(void);
void spr_set_season(int season);

// Season palette helpers used by procedural drawing.
u32 season_grass(int season, int shade);
