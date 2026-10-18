// Static game data: crops and items.
#pragma once
#include "gfx.h"

enum { SEASON_SPRING, SEASON_SUMMER, SEASON_FALL, SEASON_WINTER };
#define SB(s) (1 << (s))

// Produce icon shapes.
enum { TM_ROOT, TM_ROUND, TM_BERRY, TM_CORN, TM_EGG, TM_LEAFY };
// How a ripe crop looks on the field.
enum {
    RIPE_ON_GROUND, // produce sits on the soil among small leaves
    RIPE_BERRIES,   // bush dotted with fruit
    RIPE_TALL       // bush with the produce standing on top
};

typedef struct {
    const char *name;
    int seasons;    // bitmask of SB(season)
    int seed_price;
    int sell_price;
    int days;       // days of watered growth until harvest
    int regrow;     // days to regrow after harvest, 0 = single harvest
    int yield;      // produce per harvest
    u32 p, q, v;    // produce colors: main, shade, highlight
    int tmpl;
    int ripe;
} CropDef;

#define NUM_CROPS 12
extern const CropDef CROPS[NUM_CROPS];

enum {
    IT_HOE,
    IT_CAN,
    IT_SCYTHE,
    IT_PICK,
    IT_SPRINKLER,
    IT_SEED0,
    IT_CROP0 = IT_SEED0 + NUM_CROPS,
    NUM_ITEMS = IT_CROP0 + NUM_CROPS
};

#define IS_TOOL(i) ((i) <= IT_PICK)
#define IS_SEED(i) ((i) >= IT_SEED0 && (i) < IT_CROP0)
#define IS_CROP(i) ((i) >= IT_CROP0 && (i) < NUM_ITEMS)

#define SPRINKLER_PRICE 400
#define HOE_UPGRADE_PRICE 1500
#define CAN_UPGRADE_PRICE 1000

extern const char *SEASON_NAMES[4];
extern const char *WEEKDAY_NAMES[7];

// Writes a display name for an item into buf.
void item_name(int item, char *buf, int len);
