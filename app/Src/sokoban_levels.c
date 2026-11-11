#include "sokoban.h"

/* Level 1 */
static const uint8_t SOKO_Level1Map[] = {
    1, 1, 1, 1, 0, 0,
    1, 0, 0, 1, 0, 0,
    1, 0, 0, 1, 1, 1,
    1, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 0, 0,
};
static const SOKO_Pos_t SOKO_Level1Boxes[] = { { 1, 3 }, { 3, 4 } };
static const SOKO_Pos_t SOKO_Level1Goals[] = { { 2, 1 }, { 1, 3 } };

/* Level 2 */
static const uint8_t SOKO_Level2Map[] = {
    0, 0, 1, 1, 1, 1, 0, 0, 0,
    1, 1, 1, 0, 0, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 1, 0, 0, 1, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static const SOKO_Pos_t SOKO_Level2Boxes[] = { { 6, 2 }, { 6, 3 } };
static const SOKO_Pos_t SOKO_Level2Goals[] = { { 2, 4 }, { 4, 4 } };

/* Level 3 */
static const uint8_t SOKO_Level3Map[] = {
    1, 1, 1, 1, 1, 0,
    1, 0, 0, 0, 1, 1,
    1, 0, 0, 0, 0, 1,
    1, 1, 0, 0, 0, 1,
    0, 1, 1, 0, 0, 1,
    0, 0, 1, 1, 0, 1,
    0, 0, 0, 1, 1, 1,
};
static const SOKO_Pos_t SOKO_Level3Boxes[] = { { 2, 2 }, { 3, 2 } };
static const SOKO_Pos_t SOKO_Level3Goals[] = { { 1, 1 }, { 4, 5 } };

const SOKO_Level_t SOKO_Levels[] = {
    { 6, 7, SOKO_Level1Map, { 2, 3 }, SOKO_ARRAY_COUNT(SOKO_Level1Boxes), SOKO_Level1Boxes, SOKO_Level1Goals },
    { 9, 6, SOKO_Level2Map, { 6, 4 }, SOKO_ARRAY_COUNT(SOKO_Level2Boxes), SOKO_Level2Boxes, SOKO_Level2Goals },
    { 6, 7, SOKO_Level3Map, { 1, 2 }, SOKO_ARRAY_COUNT(SOKO_Level3Boxes), SOKO_Level3Boxes, SOKO_Level3Goals },
};

const uint8_t SOKO_LevelCount = SOKO_ARRAY_COUNT(SOKO_Levels);
