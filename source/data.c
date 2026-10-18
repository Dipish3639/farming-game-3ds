#include "data.h"
#include <stdio.h>

#define SP SB(SEASON_SPRING)
#define SU SB(SEASON_SUMMER)
#define FA SB(SEASON_FALL)
#define WI SB(SEASON_WINTER)

const CropDef CROPS[NUM_CROPS] = {
    // name           seasons  seed sell days regrow yield  main      shade     highlight  shape     ripe look
    {"Parsnip",       SP,       20,  35,  4, 0, 1, RGB(0xf0deaa), RGB(0xc4a868), RGB(0xfff6dc), TM_ROOT,  RIPE_ON_GROUND},
    {"Potato",        SP,       50,  80,  6, 0, 1, RGB(0xc8985a), RGB(0x8a6236), RGB(0xe8c48c), TM_ROUND, RIPE_ON_GROUND},
    {"Cauliflower",   SP,       80, 175, 12, 0, 1, RGB(0xf4f0e0), RGB(0xc4bca0), RGB(0xffffff), TM_LEAFY, RIPE_ON_GROUND},
    {"Strawberry",    SP,      100, 120,  8, 4, 1, RGB(0xe03040), RGB(0x961828), RGB(0xff8a8a), TM_BERRY, RIPE_BERRIES},
    {"Tomato",        SU,       50,  60, 11, 4, 1, RGB(0xe84a3a), RGB(0xa42a20), RGB(0xff9e84), TM_ROUND, RIPE_BERRIES},
    {"Melon",         SU,       80, 250, 12, 0, 1, RGB(0x7ac850), RGB(0x468a2e), RGB(0xb4ec94), TM_ROUND, RIPE_ON_GROUND},
    {"Blueberry",     SU,       80,  50, 13, 4, 3, RGB(0x4a5ae0), RGB(0x283090), RGB(0x9eacff), TM_BERRY, RIPE_BERRIES},
    {"Corn",          SU | FA, 150,  50, 14, 4, 1, RGB(0xf2d34b), RGB(0xc49a2c), RGB(0xfff2a0), TM_CORN,  RIPE_TALL},
    {"Pumpkin",       FA,      100, 320, 13, 0, 1, RGB(0xe8862a), RGB(0xa45818), RGB(0xffbc64), TM_ROUND, RIPE_ON_GROUND},
    {"Eggplant",      FA,       20,  60,  5, 5, 1, RGB(0x7a3aa0), RGB(0x482068), RGB(0xb474dc), TM_EGG,   RIPE_BERRIES},
    {"Cranberry",     FA,      240,  75,  7, 5, 2, RGB(0xb02030), RGB(0x6e1020), RGB(0xe46474), TM_BERRY, RIPE_BERRIES},
    {"Winter Kale",   WI,       30,  55,  6, 0, 1, RGB(0x5aa88a), RGB(0x387060), RGB(0x98dcbc), TM_LEAFY, RIPE_ON_GROUND},
};

const char *SEASON_NAMES[4] = {"Spring", "Summer", "Fall", "Winter"};
const char *WEEKDAY_NAMES[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

void item_name(int item, char *buf, int len)
{
    static const char *tools[] = {"Hoe", "Watering Can", "Scythe", "Pickaxe", "Sprinkler"};
    if (item <= IT_SPRINKLER) snprintf(buf, len, "%s", tools[item]);
    else if (IS_SEED(item)) snprintf(buf, len, "%s Seeds", CROPS[item - IT_SEED0].name);
    else if (IS_CROP(item)) snprintf(buf, len, "%s", CROPS[item - IT_CROP0].name);
    else snprintf(buf, len, "?");
}
