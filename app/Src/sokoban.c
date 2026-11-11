#include <stdbool.h>
#include <stddef.h>
#include "pshell.h"
#include "tui.h"
#include "sokoban.h"

/* Screen position of the top left cell, leaves room for the title */
#define SOKO_TOP            (4U)
#define SOKO_LEFT           (5U)
#define SOKO_CELL_WIDTH     (4U)
#define SOKO_CELL_HEIGHT    (2U)

/* Symbols, SOKO_CELL_HEIGHT rows, every row is SOKO_CELL_WIDTH columns wide (UTF-8) */
static const char * const SOKO_SymBlock[SOKO_CELL_HEIGHT] =
{
    "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88",  /* ████ */
    "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88"   /* ████ */
};
static const char * const SOKO_SymGoal[SOKO_CELL_HEIGHT] =
{
    " \xE2\x96\x84\xE2\x96\x84 ",                         /*  ▄▄  */
    "    "
};
static const char * const SOKO_SymPlayer[SOKO_CELL_HEIGHT] =
{
    "\xE2\x96\x84\xE2\x96\x88\xE2\x96\x88\xE2\x96\x84",  /* ▄██▄ */
    "\xE2\x96\x84\xE2\x96\x80\xE2\x96\x80\xE2\x96\x84"   /* ▄▀▀▄ */
};
static const char * const SOKO_SymFloor[SOKO_CELL_HEIGHT] =
{
    "    ",
    "    "
};

/* Colors, 256 color palette */
#define SOKO_COLOR_WALL     TUI_COLOR_GRAY
#define SOKO_COLOR_BOX      (172)
#define SOKO_COLOR_BOX_DONE TUI_COLOR_GREEN
#define SOKO_COLOR_GOAL     TUI_COLOR_RED
#define SOKO_COLOR_PLAYER   (220)

/* Cell flags */
#define SOKO_CELL_WALL      (0x01U)
#define SOKO_CELL_GOAL      (0x02U)
#define SOKO_CELL_BOX       (0x04U)

static uint8_t SOKO_Cells[SOKO_MAX_HEIGHT][SOKO_MAX_WIDTH];
static uint8_t SOKO_Width;
static uint8_t SOKO_Height;
static uint8_t SOKO_PlayerX;
static uint8_t SOKO_PlayerY;
static uint8_t SOKO_BoxCount;
static uint8_t SOKO_BoxesOnGoal;
static uint16_t SOKO_Moves;
static uint8_t SOKO_LevelIndex;
static bool SOKO_Valid;

/** Cell flags, outside of the map counts as wall */
static uint8_t SOKO_GetCell(int16_t X, int16_t Y)
{
    uint8_t Cell = SOKO_CELL_WALL;

    if ((X >= 0) && (Y >= 0) && (X < (int16_t)SOKO_Width) && (Y < (int16_t)SOKO_Height))
    {
        Cell = SOKO_Cells[Y][X];
    }

    return Cell;
}

/** Inside the map and not a wall */
static bool SOKO_IsFloor(const SOKO_Pos_t *pPos)
{
    return (SOKO_GetCell(pPos->X, pPos->Y) & SOKO_CELL_WALL) == 0U;
}

static bool SOKO_IsSolved(void)
{
    return SOKO_BoxesOnGoal == SOKO_BoxCount;
}

/** Builds the cells of a level, false if the level definition is wrong */
static bool SOKO_Load(uint8_t Index)
{
    const SOKO_Level_t *pLevel = NULL;
    uint8_t X;
    uint8_t Y;
    uint8_t Box;
    bool Valid = false;

    if (Index < SOKO_LevelCount)
    {
        pLevel = &SOKO_Levels[Index];
        Valid = (pLevel->Width >= SOKO_MIN_SIZE) && (pLevel->Width <= SOKO_MAX_WIDTH) &&
                (pLevel->Height >= SOKO_MIN_SIZE) && (pLevel->Height <= SOKO_MAX_HEIGHT) &&
                (pLevel->BoxCount > 0U) && (pLevel->BoxCount <= SOKO_MAX_BOXES) &&
                (pLevel->pMap != NULL) && (pLevel->pBoxes != NULL) && (pLevel->pGoals != NULL);
    }

    if (Valid)
    {
        SOKO_Width = pLevel->Width;
        SOKO_Height = pLevel->Height;
        SOKO_BoxCount = pLevel->BoxCount;
        SOKO_BoxesOnGoal = 0U;
        SOKO_Moves = 0U;

        for (Y = 0U; Y < SOKO_Height; Y++)
        {
            for (X = 0U; X < SOKO_Width; X++)
            {
                SOKO_Cells[Y][X] = (pLevel->pMap[Y * SOKO_Width + X] != 0U) ? SOKO_CELL_WALL : 0U;
            }
        }

        for (Box = 0U; Box < SOKO_BoxCount; Box++)
        {
            if (SOKO_IsFloor(&pLevel->pGoals[Box]))
            {
                SOKO_Cells[pLevel->pGoals[Box].Y][pLevel->pGoals[Box].X] |= SOKO_CELL_GOAL;
            }
            else
            {
                Valid = false;
            }
        }

        for (Box = 0U; Box < SOKO_BoxCount; Box++)
        {
            X = pLevel->pBoxes[Box].X;
            Y = pLevel->pBoxes[Box].Y;
            if (SOKO_IsFloor(&pLevel->pBoxes[Box]) && ((SOKO_Cells[Y][X] & SOKO_CELL_BOX) == 0U))
            {
                SOKO_Cells[Y][X] |= SOKO_CELL_BOX;
                if ((SOKO_Cells[Y][X] & SOKO_CELL_GOAL) != 0U)
                {
                    SOKO_BoxesOnGoal++;
                }
            }
            else
            {
                Valid = false;
            }
        }

        SOKO_PlayerX = pLevel->Player.X;
        SOKO_PlayerY = pLevel->Player.Y;
        if ((SOKO_GetCell(SOKO_PlayerX, SOKO_PlayerY) & (SOKO_CELL_WALL | SOKO_CELL_BOX)) != 0U)
        {
            Valid = false;
        }
    }

    return Valid;
}

static void SOKO_DrawCell(uint8_t X, uint8_t Y)
{
    uint8_t Cell = SOKO_Cells[Y][X];
    const char * const *ppSymbol = SOKO_SymFloor;
    int16_t Color = TUI_COLOR_DEFAULT;
    uint8_t Row;

    if ((X == SOKO_PlayerX) && (Y == SOKO_PlayerY))
    {
        ppSymbol = SOKO_SymPlayer;
        Color = SOKO_COLOR_PLAYER;
    }
    else if ((Cell & SOKO_CELL_BOX) != 0U)
    {
        ppSymbol = SOKO_SymBlock;
        Color = ((Cell & SOKO_CELL_GOAL) != 0U) ? SOKO_COLOR_BOX_DONE : SOKO_COLOR_BOX;
    }
    else if ((Cell & SOKO_CELL_GOAL) != 0U)
    {
        ppSymbol = SOKO_SymGoal;
        Color = SOKO_COLOR_GOAL;
    }
    else if ((Cell & SOKO_CELL_WALL) != 0U)
    {
        ppSymbol = SOKO_SymBlock;
        Color = SOKO_COLOR_WALL;
    }

    TUI_SetColor(Color, TUI_COLOR_DEFAULT);
    for (Row = 0U; Row < SOKO_CELL_HEIGHT; Row++)
    {
        TUI_PrintAt((uint8_t)(SOKO_TOP + Y * SOKO_CELL_HEIGHT + Row), (uint8_t)(SOKO_LEFT + X * SOKO_CELL_WIDTH),
                    ppSymbol[Row]);
    }
    TUI_ResetStyle();
}

/** Fixed width, so it overwrites the previous one */
static void SOKO_DrawStatus(void)
{
    TUI_MoveTo((uint8_t)(SOKO_TOP + SOKO_Height * SOKO_CELL_HEIGHT + 1U), SOKO_LEFT);
    PSH_Printf("Moves: %-5u  Boxes: %2u/%-2u  %-24s",
               (unsigned int)SOKO_Moves, (unsigned int)SOKO_BoxesOnGoal, (unsigned int)SOKO_BoxCount,
               SOKO_IsSolved() ? "Solved! n: next level" : "");
}

static void SOKO_DrawAll(void)
{
    uint8_t X;
    uint8_t Y;

    TUI_Clear();
    TUI_MoveTo(SOKO_TOP - 2U, SOKO_LEFT);
    PSH_Printf("SOKOBAN   Level %u/%u", (unsigned int)(SOKO_LevelIndex + 1U), (unsigned int)SOKO_LevelCount);

    if (SOKO_Valid)
    {
        for (Y = 0U; Y < SOKO_Height; Y++)
        {
            for (X = 0U; X < SOKO_Width; X++)
            {
                SOKO_DrawCell(X, Y);
            }
        }
        SOKO_DrawStatus();
        TUI_PrintAt((uint8_t)(SOKO_TOP + SOKO_Height * SOKO_CELL_HEIGHT + 2U), SOKO_LEFT,
                    "Arrows: move  r: restart  n/p: next/prev level  q: quit");
    }
    else
    {
        TUI_PrintAt(SOKO_TOP, SOKO_LEFT, "Invalid level definition  n/p: next/prev level  q: quit");
    }
}

static void SOKO_Start(uint8_t Index)
{
    SOKO_LevelIndex = Index;
    SOKO_Valid = SOKO_Load(Index);
    SOKO_DrawAll();
}

/** Moves the man, pushes one box if the cell behind it is free, redraws only the changed cells */
static void SOKO_Move(int8_t DX, int8_t DY)
{
    int16_t NextX = (int16_t)SOKO_PlayerX + DX;
    int16_t NextY = (int16_t)SOKO_PlayerY + DY;
    int16_t BeyondX = NextX + DX;
    int16_t BeyondY = NextY + DY;
    uint8_t OldX = SOKO_PlayerX;
    uint8_t OldY = SOKO_PlayerY;
    uint8_t NextCell;
    bool Moved = false;
    bool Pushed = false;

    if (SOKO_Valid && !SOKO_IsSolved())
    {
        NextCell = SOKO_GetCell(NextX, NextY);
        if ((NextCell & SOKO_CELL_WALL) == 0U)
        {
            if ((NextCell & SOKO_CELL_BOX) == 0U)
            {
                Moved = true;
            }
            else if ((SOKO_GetCell(BeyondX, BeyondY) & (SOKO_CELL_WALL | SOKO_CELL_BOX)) == 0U)
            {
                SOKO_Cells[NextY][NextX] &= (uint8_t)~SOKO_CELL_BOX;
                SOKO_Cells[BeyondY][BeyondX] |= SOKO_CELL_BOX;
                if ((NextCell & SOKO_CELL_GOAL) != 0U)
                {
                    SOKO_BoxesOnGoal--;
                }
                if ((SOKO_Cells[BeyondY][BeyondX] & SOKO_CELL_GOAL) != 0U)
                {
                    SOKO_BoxesOnGoal++;
                }
                Moved = true;
                Pushed = true;
            }
        }
    }

    if (Moved)
    {
        SOKO_PlayerX = (uint8_t)NextX;
        SOKO_PlayerY = (uint8_t)NextY;
        SOKO_Moves++;

        SOKO_DrawCell(OldX, OldY);
        SOKO_DrawCell(SOKO_PlayerX, SOKO_PlayerY);
        if (Pushed)
        {
            SOKO_DrawCell((uint8_t)BeyondX, (uint8_t)BeyondY);
        }
        SOKO_DrawStatus();
    }
}

void SOKO_Run(void)
{
    int Key;
    bool Running = true;

    TUI_Begin();
    SOKO_Start(0U);

    while (Running)
    {
        Key = TUI_ReadKey();
        switch (Key)
        {
        case TUI_KEY_STOP:
        case 'q':
        case 'Q':
            Running = false;
            break;
        case TUI_KEY_UP:
            SOKO_Move(0, -1);
            break;
        case TUI_KEY_DOWN:
            SOKO_Move(0, 1);
            break;
        case TUI_KEY_LEFT:
            SOKO_Move(-1, 0);
            break;
        case TUI_KEY_RIGHT:
            SOKO_Move(1, 0);
            break;
        case 'r':
        case 'R':
            SOKO_Start(SOKO_LevelIndex);
            break;
        case 'n':
        case 'N':
            if (SOKO_LevelCount > 0U)
            {
                SOKO_Start((uint8_t)((SOKO_LevelIndex + 1U) % SOKO_LevelCount));
            }
            break;
        case 'p':
        case 'P':
            if (SOKO_LevelCount > 0U)
            {
                SOKO_Start((uint8_t)((SOKO_LevelIndex + SOKO_LevelCount - 1U) % SOKO_LevelCount));
            }
            break;
        default:
            break;
        }
    }

    TUI_End();
}
