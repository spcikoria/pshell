#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pshell.h"
#include "task.h"
#include "tui.h"
#include "trex.h"

/* Timing and speed, the speed unit is 1/TREX_SPEED_SUB column per tick */
#define TREX_TICK_MS        (50U)
#define TREX_SPEED_SUB      (16)
#define TREX_SPEED_START    ((3 * TREX_SPEED_SUB) / 2)   /* 1.5 columns/tick */
#define TREX_SPEED_MAX      (4 * TREX_SPEED_SUB)         /* 4 columns/tick */
#define TREX_SPEED_STEP     (20)                         /* score points per speed step */
#define TREX_SCORE_COLS     (3)                          /* scrolled columns per score point */
#define TREX_SCORE_DIGITS   (5)

/* Dino */
#define TREX_DINO_W         (16)
#define TREX_DINO_H         (8)
#define TREX_DINO_BODY_H    (TREX_DINO_H - 1)
#define TREX_JUMP_V         (4)                          /* take-off speed in rows/tick */
#define TREX_JUMP_H         ((TREX_JUMP_V * (TREX_JUMP_V + 1)) / 2)
#define TREX_STEP_TICKS     (3)                          /* ticks per leg frame */
#define TREX_HANG_TICKS     (2)                          /* ticks at the top of the jump */
#define TREX_LEGS_STAND     (2)

/* Screen layout, rows and columns from 0 */
#define TREX_W              (80)
#define TREX_SCORE_ROW      (0)
#define TREX_SCORE_COL      (TREX_W - 15)
#define TREX_TOP            (2)
#define TREX_DINO_X         (4)
#define TREX_DINO_Y         (TREX_TOP + TREX_JUMP_H)     /* top row of the standing dino */
#define TREX_GROUND_Y       (TREX_DINO_Y + TREX_DINO_H - 1)
#define TREX_ROWS           (TREX_GROUND_Y + 3 - TREX_TOP)
#define TREX_HELP_ROW       (TREX_GROUND_Y + 4)
#define TREX_NO_CURSOR      (0xFFU)

/* Cacti */
#define TREX_CACTUS_MAX     (4U)
#define TREX_CACTUS_TYPES   (4U)
#define TREX_CACTUS_W_MAX   (16)
#define TREX_CACTUS_FREE    (-1)
#define TREX_CACTUS_GAP     (20)
#define TREX_SPAWN_CHANCE   (15U)
#define TREX_GAP_JUMP       (18)                         /* ticks of free run after a cactus */
#define TREX_GAP_RANDOM     (30U)

/* Ground */
#define TREX_BUMP_COUNT     (3U)
#define TREX_BUMP_CHANCE    (40U)
#define TREX_PEBBLE_COUNT   (5U)
#define TREX_PEBBLE_CHANCE  (8U)

/* Output buffer, sent with one PSH_Write() */
#define TREX_OUT_SIZE       (128U)

/* Cell codes: below 0x80 ASCII, from 0x80 an index into TREX_Glyph. Only code page 437 blocks, in every console font */
#define TREX_G_FULL         (0x80U)  /* █ */
#define TREX_G_UPPER        (0x81U)  /* ▀ */
#define TREX_G_LOWER        (0x82U)  /* ▄ */
#define TREX_G_FIRST        TREX_G_FULL
#define TREX_G_COUNT        (3U)
#define TREX_G_GROUND       TREX_G_UPPER

/* Sprite keys: '#' █, '^' ▀, '_' ▄, ' ' and '\0' are transparent */
#define TREX_IS_SOLID(Ch)   ((uint8_t)(Ch) > (uint8_t)' ')

typedef struct
{
    const char (*pLines)[TREX_CACTUS_W_MAX + 1];
    uint8_t H;
    uint8_t W;
    int16_t MinSpeed;    /* spawns only from this speed on */
} TREX_CactusType_t;

static const char *const TREX_Glyph[TREX_G_COUNT] = { "\xE2\x96\x88", "\xE2\x96\x80", "\xE2\x96\x84" };

static const char TREX_DinoBody[TREX_DINO_BODY_H][TREX_DINO_W + 1] = {
    "        _#^####_",
    "        ########",
    "        #####_",
    "#     _#####__",
    "##___####### ^",
    " ^#########^",
    "   ^##^^##",
};

static const char TREX_DinoLegs[3][TREX_DINO_W + 1] = {
    "    ^^  #_",
    "    #_  ^^",
    "    #_  #_",
};

static const char TREX_CactusLarge[8][TREX_CACTUS_W_MAX + 1] = {
    "    _##_",
    "    #### _#_",
    "_#_ #### ###",
    "### ####_##^",
    "### ####^^",
    "^#######",
    "    ####",
    "    ####",
};

static const char TREX_CactusSmall[6][TREX_CACTUS_W_MAX + 1] = {
    "  _##_",
    "_ #### #",
    "# ####_#",
    "#_####^",
    " ^####",
    "  ####",
};

static const char TREX_CactusTiny[4][TREX_CACTUS_W_MAX + 1] = {
    "  _#_",
    "_ ### #",
    "#_###^^",
    "  ###",
};

static const char TREX_CactusGroup[6][TREX_CACTUS_W_MAX + 1] = {
    "  _##_    _##_",
    "_ #### #_ #### #",
    "# ####_## ####_#",
    "#_####^ #_####^",
    " ^####   ^####",
    "  ####    ####",
};

/* Sorted by MinSpeed, wide and tall cacti need a faster scroll to be cleared by a jump */
static const TREX_CactusType_t TREX_CactusTypes[TREX_CACTUS_TYPES] = {
    { TREX_CactusTiny,  4U, 7U,  0 },
    { TREX_CactusSmall, 6U, 8U,  2 * TREX_SPEED_SUB },
    { TREX_CactusLarge, 8U, 12U, (5 * TREX_SPEED_SUB) / 2 },
    { TREX_CactusGroup, 6U, 16U, (5 * TREX_SPEED_SUB) / 2 },
};

static const uint8_t TREX_BumpA[] = { '_', TREX_G_LOWER, '_', 0U };
static const uint8_t TREX_BumpB[] = { '_', '_', TREX_G_LOWER, '_', '_', 0U };
static const uint8_t TREX_BumpC[] = { '_', '_', 0U };
static const uint8_t *const TREX_Bumps[TREX_BUMP_COUNT] = { TREX_BumpA, TREX_BumpB, TREX_BumpC };
static const uint8_t TREX_Pebbles[TREX_PEBBLE_COUNT] = { '.', ',', '\'', '`', '.' };

static const char TREX_GameOverText[] = "GAME OVER";
static const char TREX_HelpText[] = "space: jump  r: restart  q: quit";

/* Game */
static uint32_t TREX_RandState;
static uint32_t TREX_Score;
static uint32_t TREX_Hi;
static uint32_t TREX_Dist;
static int16_t TREX_Speed;
static int16_t TREX_Sub;             /* fraction of a column carried to the next tick */
static bool TREX_GameOver;

/* Dino */
static uint8_t TREX_DinoFrame;
static uint8_t TREX_DinoTick;
static int16_t TREX_DinoOffset;      /* rows between the feet and the ground */
static int16_t TREX_DinoVel;         /* rows/tick: >0 rising, <0 falling, 0 on ground */
static uint8_t TREX_DinoHang;

/* Cacti */
static int16_t TREX_CactusX[TREX_CACTUS_MAX];
static int8_t TREX_CactusKind[TREX_CACTUS_MAX];
static int16_t TREX_CactusGap;       /* columns to scroll before the next cactus may spawn */

/* Ground, ring buffers of glyph codes */
static uint8_t TREX_GroundTop[TREX_W];
static uint8_t TREX_GroundBot[TREX_W];
static uint8_t TREX_GroundHead;
static const uint8_t *TREX_pBump;

/* Screen: cells on the terminal, only changed cells are sent */
static uint8_t TREX_Front[TREX_ROWS][TREX_W];
static uint32_t TREX_ShownScore;
static uint32_t TREX_ShownHi;
static char TREX_Out[TREX_OUT_SIZE];
static uint16_t TREX_OutLen;

/** xorshift32 PRNG */
static uint32_t TREX_Rand(void)
{
    TREX_RandState ^= TREX_RandState << 13;
    TREX_RandState ^= TREX_RandState >> 17;
    TREX_RandState ^= TREX_RandState << 5;
    return TREX_RandState;
}

/*****************************************************************************/
/* Output */

static void TREX_Flush(void)
{
    if (TREX_OutLen > 0U)
    {
        PSH_Write(TREX_Out, TREX_OutLen);
        TREX_OutLen = 0U;
    }
}

static void TREX_PutByte(char Byte)
{
    if (TREX_OutLen >= TREX_OUT_SIZE)
    {
        TREX_Flush();
    }
    TREX_Out[TREX_OutLen] = Byte;
    TREX_OutLen++;
}

static void TREX_PutText(const char *pText)
{
    while (*pText != '\0')
    {
        TREX_PutByte(*pText);
        pText++;
    }
}

static void TREX_PutNum(uint8_t Value)
{
    if (Value >= 100U)
    {
        TREX_PutByte((char)('0' + (Value / 100U)));
    }
    if (Value >= 10U)
    {
        TREX_PutByte((char)('0' + ((Value / 10U) % 10U)));
    }
    TREX_PutByte((char)('0' + (Value % 10U)));
}

/** Zero padded TREX_SCORE_DIGITS digit number */
static void TREX_PutScore(uint32_t Value)
{
    char Digits[TREX_SCORE_DIGITS];
    int8_t Index;

    for (Index = TREX_SCORE_DIGITS - 1; Index >= 0; Index--)
    {
        Digits[Index] = (char)('0' + (Value % 10U));
        Value /= 10U;
    }
    for (Index = 0; Index < TREX_SCORE_DIGITS; Index++)
    {
        TREX_PutByte(Digits[Index]);
    }
}

static void TREX_PutMove(int16_t Row, uint8_t Col)
{
    TREX_PutText("\x1b[");
    TREX_PutNum((uint8_t)(Row + 1));
    TREX_PutByte(';');
    TREX_PutNum((uint8_t)(Col + 1U));
    TREX_PutByte('H');
}

static void TREX_PutCell(uint8_t Cell)
{
    if (Cell >= TREX_G_FIRST)
    {
        TREX_PutText(TREX_Glyph[Cell - TREX_G_FIRST]);
    }
    else
    {
        TREX_PutByte((char)Cell);
    }
}

/*****************************************************************************/
/* Ground */

static void TREX_GroundNext(void)
{
    uint8_t Col = TREX_GroundHead;

    TREX_GroundHead = (uint8_t)((TREX_GroundHead + 1U) % TREX_W);

    if ((TREX_pBump == NULL) && ((TREX_Rand() % TREX_BUMP_CHANCE) == 0U))
    {
        TREX_pBump = TREX_Bumps[TREX_Rand() % TREX_BUMP_COUNT];
    }

    TREX_GroundTop[Col] = ' ';
    if (TREX_pBump != NULL)
    {
        TREX_GroundTop[Col] = *TREX_pBump;
        TREX_pBump++;
        if (*TREX_pBump == 0U)
        {
            TREX_pBump = NULL;
        }
    }

    TREX_GroundBot[Col] = ' ';
    if ((TREX_Rand() % TREX_PEBBLE_CHANCE) == 0U)
    {
        TREX_GroundBot[Col] = TREX_Pebbles[TREX_Rand() % TREX_PEBBLE_COUNT];
    }
}

static void TREX_GroundInit(void)
{
    uint8_t Col;

    TREX_GroundHead = 0U;
    TREX_pBump = NULL;
    for (Col = 0U; Col < TREX_W; Col++)
    {
        TREX_GroundNext();
    }
}

/*****************************************************************************/
/* Dino */

static void TREX_DinoInit(void)
{
    TREX_DinoOffset = 0;
    TREX_DinoVel = 0;
    TREX_DinoHang = 0U;
}

static void TREX_DinoJump(void)
{
    if (TREX_DinoVel == 0)
    {
        TREX_DinoVel = TREX_JUMP_V;
    }
}

static void TREX_DinoStep(void)
{
    if (TREX_DinoVel > 0)
    {
        /* Fast take-off: 4, 3, 2, 1 rows/tick */
        TREX_DinoOffset += TREX_DinoVel;
        TREX_DinoVel--;
        if (TREX_DinoVel == 0)
        {
            TREX_DinoVel = -1;
            TREX_DinoHang = TREX_HANG_TICKS;
        }
    }
    else if (TREX_DinoVel < 0)
    {
        if (TREX_DinoHang > 0U)
        {
            TREX_DinoHang--;
        }
        else
        {
            /* Linear fall: 1 row/tick */
            TREX_DinoOffset += TREX_DinoVel;
            if (TREX_DinoOffset <= 0)
            {
                TREX_DinoOffset = 0;
                TREX_DinoVel = 0;
            }
        }
    }
    else
    {
        /* On the ground */
    }

    TREX_DinoTick++;
    if (TREX_DinoTick >= TREX_STEP_TICKS)
    {
        TREX_DinoTick = 0U;
        TREX_DinoFrame ^= 1U;
    }
}

static int16_t TREX_DinoTop(void)
{
    return (int16_t)(TREX_DINO_Y - TREX_DinoOffset);
}

/** Sprite row, the legs run on the ground and stand in the air */
static const char *TREX_DinoLine(int16_t Row)
{
    const char *pLine;

    if (Row < TREX_DINO_BODY_H)
    {
        pLine = TREX_DinoBody[Row];
    }
    else if (TREX_DinoVel != 0)
    {
        pLine = TREX_DinoLegs[TREX_LEGS_STAND];
    }
    else
    {
        pLine = TREX_DinoLegs[TREX_DinoFrame];
    }
    return pLine;
}

/*****************************************************************************/
/* Cacti */

static void TREX_CactusInit(void)
{
    uint8_t Index;

    for (Index = 0U; Index < TREX_CACTUS_MAX; Index++)
    {
        TREX_CactusKind[Index] = TREX_CACTUS_FREE;
    }
    TREX_CactusGap = TREX_CACTUS_GAP;
}

/** Scrolls the cacti by Dx columns and spawns a new one at the right edge */
static void TREX_CactusNext(int16_t Dx, int16_t Speed)
{
    const TREX_CactusType_t *pType;
    int8_t Free = TREX_CACTUS_FREE;
    uint8_t Kinds = 1U;
    uint8_t Index;

    for (Index = 0U; Index < TREX_CACTUS_MAX; Index++)
    {
        if (TREX_CactusKind[Index] != TREX_CACTUS_FREE)
        {
            TREX_CactusX[Index] -= Dx;
            if ((TREX_CactusX[Index] + TREX_CactusTypes[TREX_CactusKind[Index]].W) <= 0)
            {
                TREX_CactusKind[Index] = TREX_CACTUS_FREE;
            }
        }
        if (TREX_CactusKind[Index] == TREX_CACTUS_FREE)
        {
            Free = (int8_t)Index;
        }
    }

    if (TREX_CactusGap > 0)
    {
        TREX_CactusGap -= Dx;
    }
    else if ((Free != TREX_CACTUS_FREE) && ((TREX_Rand() % TREX_SPAWN_CHANCE) == 0U))
    {
        while ((Kinds < TREX_CACTUS_TYPES) && (TREX_CactusTypes[Kinds].MinSpeed <= Speed))
        {
            Kinds++;
        }
        TREX_CactusKind[Free] = (int8_t)(TREX_Rand() % Kinds);
        TREX_CactusX[Free] = TREX_W;
        pType = &TREX_CactusTypes[TREX_CactusKind[Free]];

        /* Enough free columns to land and jump again */
        TREX_CactusGap = (int16_t)(pType->W + ((TREX_GAP_JUMP * Speed) / TREX_SPEED_SUB) +
                                   (int16_t)(TREX_Rand() % TREX_GAP_RANDOM));
    }
    else
    {
        /* Wait for a free slot or luck */
    }
}

/** Cell exact collision of the dino with any cactus */
static bool TREX_CactusHit(void)
{
    const TREX_CactusType_t *pType;
    const char *pDino;
    int16_t DinoTop = TREX_DinoTop();
    int16_t Top;
    int16_t Row;
    int16_t Col;
    int16_t CactusCol;
    uint8_t Index;
    bool Hit = false;

    for (Index = 0U; (Index < TREX_CACTUS_MAX) && !Hit; Index++)
    {
        if (TREX_CactusKind[Index] != TREX_CACTUS_FREE)
        {
            pType = &TREX_CactusTypes[TREX_CactusKind[Index]];
            Top = (int16_t)(TREX_GROUND_Y - pType->H + 1);
            for (Row = 0; (Row < TREX_DINO_H) && !Hit; Row++)
            {
                if (((DinoTop + Row) >= Top) && ((DinoTop + Row) <= TREX_GROUND_Y))
                {
                    pDino = TREX_DinoLine(Row);
                    for (Col = 0; (Col < TREX_DINO_W) && !Hit; Col++)
                    {
                        CactusCol = (int16_t)(TREX_DINO_X + Col - TREX_CactusX[Index]);
                        if (TREX_IS_SOLID(pDino[Col]) && (CactusCol >= 0) && (CactusCol < pType->W))
                        {
                            Hit = TREX_IS_SOLID(pType->pLines[DinoTop + Row - Top][CactusCol]);
                        }
                    }
                }
            }
        }
    }
    return Hit;
}

/*****************************************************************************/
/* Drawing */

static uint8_t TREX_KeyGlyph(char Key)
{
    uint8_t Glyph;

    switch (Key)
    {
    case '#':
        Glyph = TREX_G_FULL;
        break;
    case '^':
        Glyph = TREX_G_UPPER;
        break;
    case '_':
        Glyph = TREX_G_LOWER;
        break;
    default:
        Glyph = (uint8_t)Key;
        break;
    }
    return Glyph;
}

/** Paints the solid cells of a sprite row, clipped to the screen */
static void TREX_PaintLine(uint8_t *pCells, int16_t X, const char *pLine, uint8_t Width)
{
    uint8_t Col;
    int16_t Pos;

    for (Col = 0U; Col < Width; Col++)
    {
        Pos = (int16_t)(X + Col);
        if (TREX_IS_SOLID(pLine[Col]) && (Pos >= 0) && (Pos < TREX_W))
        {
            pCells[Pos] = TREX_KeyGlyph(pLine[Col]);
        }
    }
}

/** Builds one screen row: ground, cacti, dino on top, game over text */
static void TREX_ComposeRow(int16_t Row, uint8_t *pCells)
{
    const TREX_CactusType_t *pType;
    int16_t Top;
    uint8_t Ring;
    uint8_t Col;
    uint8_t Index;

    for (Col = 0U; Col < TREX_W; Col++)
    {
        Ring = (uint8_t)((TREX_GroundHead + Col) % TREX_W);
        if (Row == TREX_GROUND_Y)
        {
            pCells[Col] = TREX_GroundTop[Ring];
        }
        else if (Row == (TREX_GROUND_Y + 1))
        {
            pCells[Col] = TREX_G_GROUND;
        }
        else if (Row == (TREX_GROUND_Y + 2))
        {
            pCells[Col] = TREX_GroundBot[Ring];
        }
        else
        {
            pCells[Col] = ' ';
        }
    }

    for (Index = 0U; Index < TREX_CACTUS_MAX; Index++)
    {
        if (TREX_CactusKind[Index] != TREX_CACTUS_FREE)
        {
            pType = &TREX_CactusTypes[TREX_CactusKind[Index]];
            Top = (int16_t)(TREX_GROUND_Y - pType->H + 1);
            if ((Row >= Top) && (Row <= TREX_GROUND_Y))
            {
                TREX_PaintLine(pCells, TREX_CactusX[Index], pType->pLines[Row - Top], pType->W);
            }
        }
    }

    Top = TREX_DinoTop();
    if ((Row >= Top) && (Row < (Top + TREX_DINO_H)))
    {
        TREX_PaintLine(pCells, TREX_DINO_X, TREX_DinoLine(Row - Top), TREX_DINO_W);
    }

    if (TREX_GameOver && (Row == TREX_TOP))
    {
        TREX_PaintLine(pCells, (TREX_W - (int16_t)(sizeof(TREX_GameOverText) - 1U)) / 2,
                       TREX_GameOverText, (uint8_t)(sizeof(TREX_GameOverText) - 1U));
    }
}

/** Sends the changed cells and the score if it changed */
static void TREX_Draw(void)
{
    uint8_t Cells[TREX_W];
    uint8_t *pFront;
    int16_t Row;
    uint8_t Col;
    uint8_t Cursor;

    for (Row = TREX_TOP; Row < (TREX_TOP + TREX_ROWS); Row++)
    {
        TREX_ComposeRow(Row, Cells);
        pFront = TREX_Front[Row - TREX_TOP];
        Cursor = TREX_NO_CURSOR;
        for (Col = 0U; Col < TREX_W; Col++)
        {
            if (Cells[Col] != pFront[Col])
            {
                if (Cursor != Col)
                {
                    TREX_PutMove(Row, Col);
                }
                TREX_PutCell(Cells[Col]);
                pFront[Col] = Cells[Col];
                Cursor = (uint8_t)(Col + 1U);
            }
        }
    }

    if ((TREX_Score != TREX_ShownScore) || (TREX_Hi != TREX_ShownHi))
    {
        TREX_PutMove(TREX_SCORE_ROW, TREX_SCORE_COL);
        TREX_PutText("HI ");
        TREX_PutScore(TREX_Hi);
        TREX_PutByte(' ');
        TREX_PutScore(TREX_Score);
        TREX_ShownScore = TREX_Score;
        TREX_ShownHi = TREX_Hi;
    }

    TREX_Flush();
}

/** Clears the screen, the next draw sends everything */
static void TREX_Redraw(void)
{
    int16_t Row;
    uint8_t Col;

    TUI_Begin();
    for (Row = 0; Row < TREX_ROWS; Row++)
    {
        for (Col = 0U; Col < TREX_W; Col++)
        {
            TREX_Front[Row][Col] = ' ';
        }
    }
    TREX_ShownScore = UINT32_MAX;
    TREX_ShownHi = UINT32_MAX;
    TUI_PrintAt(TREX_HELP_ROW + 1, 1U, TREX_HelpText);
}

/*****************************************************************************/
/* Game */

static void TREX_Reset(void)
{
    TREX_CactusInit();
    TREX_DinoInit();
    TREX_Score = 0U;
    TREX_Dist = 0U;
    TREX_Sub = 0;
    TREX_Speed = TREX_SPEED_START;
    TREX_GameOver = false;
}

/** Scrolls the world one tick, updates the score and the speed */
static void TREX_Advance(void)
{
    int16_t Cols;
    int16_t Index;

    TREX_Sub += TREX_Speed;
    Cols = TREX_Sub / TREX_SPEED_SUB;
    TREX_Sub %= TREX_SPEED_SUB;

    for (Index = 0; Index < Cols; Index++)
    {
        TREX_GroundNext();
    }
    TREX_CactusNext(Cols, TREX_Speed);
    TREX_DinoStep();

    TREX_Dist += (uint32_t)Cols;
    TREX_Score = TREX_Dist / TREX_SCORE_COLS;
    TREX_Speed = (int16_t)(TREX_SPEED_START + (int16_t)(TREX_Score / TREX_SPEED_STEP));
    if (TREX_Speed > TREX_SPEED_MAX)
    {
        TREX_Speed = TREX_SPEED_MAX;
    }
}

/** @return false to quit */
static bool TREX_HandleKey(int Key)
{
    bool Running = true;

    switch (Key)
    {
    case TUI_KEY_STOP:
    case 'q':
    case 'Q':
        Running = false;
        break;
    case TUI_KEY_SPACE:
    case TUI_KEY_UP:
        if (!TREX_GameOver)
        {
            TREX_DinoJump();
        }
        break;
    case 'r':
    case 'R':
        if (TREX_GameOver)
        {
            TREX_Reset();
        }
        break;
    default:
        break;
    }
    return Running;
}

/*****************************************************************************/
void TREX_Main(uint8_t Argc, char **ppArgv)
{
    int Key;
    bool Running = true;

    (void)Argc;
    (void)ppArgv;

    /* The start time makes every game different, xorshift needs a non-zero seed */
    TREX_RandState = (uint32_t)xTaskGetTickCount() | 1U;
    TREX_GroundInit();
    TREX_Reset();
    TREX_Redraw();

    while (Running)
    {
        /* Apply every queued key, a timeout of 0 also reports Ctrl+C */
        Key = TUI_ReadKeyTimeout(0U);
        while (Running && (Key != TUI_KEY_NONE))
        {
            Running = TREX_HandleKey(Key);
            Key = TUI_ReadKeyTimeout(0U);
        }

        if (Running)
        {
            TREX_Draw();
            if (!TREX_GameOver)
            {
                TREX_Advance();
                TREX_GameOver = TREX_CactusHit();
                if (TREX_GameOver && (TREX_Score > TREX_Hi))
                {
                    TREX_Hi = TREX_Score;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(TREX_TICK_MS));
        }
    }

    TUI_End();
}
