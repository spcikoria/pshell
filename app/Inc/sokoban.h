#pragma once

#include <stdint.h>

/** Map limits, in cells */
#define SOKO_MIN_SIZE   (5U)
#define SOKO_MAX_WIDTH  (40U)
#define SOKO_MAX_HEIGHT (20U)
#define SOKO_MAX_BOXES  (16U)

/** Number of elements of an array, e.g. for the box count of a level */
#define SOKO_ARRAY_COUNT(Array) ((uint8_t)(sizeof(Array) / sizeof((Array)[0])))

/** A cell of the map, X: column, Y: row, from 0 */
typedef struct
{
    uint8_t X;
    uint8_t Y;
} SOKO_Pos_t;

/** A level: wall matrix, start position of the man and of the boxes, and the goals */
typedef struct
{
    uint8_t Width;
    uint8_t Height;
    /* Width * Height, row by row, 1: wall, 0: floor */
    const uint8_t *pMap;
    SOKO_Pos_t Player;
    uint8_t BoxCount;
    /* BoxCount start positions */
    const SOKO_Pos_t *pBoxes;
    /* BoxCount goals */
    const SOKO_Pos_t *pGoals;
} SOKO_Level_t;

/** Levels, sokoban_levels.c */
extern const SOKO_Level_t SOKO_Levels[];
extern const uint8_t SOKO_LevelCount;

/** Runs the game loop, returns on q or Ctrl+C. Uses the shell for all I/O, call it from a shell app only. */
void SOKO_Run(void);
