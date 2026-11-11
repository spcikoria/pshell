#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pshell.h"
#include "game2048.h"

/* Escape sequences */
#define TUI_ESC             "\x1b"
#define TUI_HOME            TUI_ESC "[H"
#define TUI_CLEAR           TUI_ESC "[2J"
#define TUI_HIDE_CURSOR     TUI_ESC "[?25l"
#define TUI_SHOW_CURSOR     TUI_ESC "[?25h"
#define TUI_NL              "\r\n"

/* Box elements (UTF-8) */
#define TUI_BOX_H           "\xE2\x94\x80"  /* ─ */
#define TUI_BOX_V           "\xE2\x94\x82"  /* │ */
#define TUI_BOX_TL          "\xE2\x94\x8C"  /* ┌ */
#define TUI_BOX_TR          "\xE2\x94\x90"  /* ┐ */
#define TUI_BOX_BR          "\xE2\x94\x98"  /* ┘ */
#define TUI_BOX_BL          "\xE2\x94\x94"  /* └ */
#define TUI_BOX_L           "\xE2\x94\x9C"  /* ├ */
#define TUI_BOX_R           "\xE2\x94\xA4"  /* ┤ */
#define TUI_BOX_T           "\xE2\x94\xAC"  /* ┬ */
#define TUI_BOX_B           "\xE2\x94\xB4"  /* ┴ */
#define TUI_BOX_X           "\xE2\x94\xBC"  /* ┼ */

/* Input characters */
#define G2048_CHR_ESC       0x1B
#define G2048_CHR_CSI       '['
#define G2048_CHR_SS3       'O'
#define G2048_CHR_UP        'A'
#define G2048_CHR_DOWN      'B'
#define G2048_CHR_RIGHT     'C'
#define G2048_CHR_LEFT      'D'

#define G2048_SIZE          4
#define G2048_CELLS         (G2048_SIZE * G2048_SIZE)
#define G2048_CELL_WIDTH    7
#define G2048_WIN_VALUE     2048
#define G2048_GRID_WIDTH    (G2048_SIZE * (G2048_CELL_WIDTH + 1) + 1)
#define G2048_FRAME_WIDTH   (G2048_GRID_WIDTH + 2)  /* inner width, one space padding on each side */
#define G2048_TITLE         " 2048 "
#define G2048_SCORE_LABEL   "Score: "

typedef enum
{
    G2048_KEY_NONE,
    G2048_KEY_UP,
    G2048_KEY_DOWN,
    G2048_KEY_LEFT,
    G2048_KEY_RIGHT,
    G2048_KEY_QUIT
} G2048_Key_t;

typedef enum
{
    G2048_ESC_IDLE,
    G2048_ESC_START,
    G2048_ESC_SEQ
} G2048_EscState_t;

static uint32_t G2048_Board[G2048_CELLS];
static uint32_t G2048_Score;
static uint32_t G2048_RandState;
static G2048_EscState_t G2048_EscState;

/** xorshift32 PRNG */
static uint32_t G2048_Rand(void)
{
    G2048_RandState ^= G2048_RandState << 13;
    G2048_RandState ^= G2048_RandState >> 17;
    G2048_RandState ^= G2048_RandState << 5;
    return G2048_RandState;
}

static void G2048_SpawnTile(void)
{
    uint8_t Empty[G2048_CELLS];
    uint8_t EmptyCount = 0;
    uint8_t Index;

    for (Index = 0; Index < G2048_CELLS; Index++)
    {
        if (G2048_Board[Index] == 0)
        {
            Empty[EmptyCount] = Index;
            EmptyCount++;
        }
    }

    if (EmptyCount > 0)
    {
        Index = Empty[G2048_Rand() % EmptyCount];
        G2048_Board[Index] = ((G2048_Rand() % 10) == 0) ? 4 : 2;
    }
}

static void G2048_Init(uint32_t Seed)
{
    uint8_t Index;

    for (Index = 0; Index < G2048_CELLS; Index++)
    {
        G2048_Board[Index] = 0;
    }
    G2048_Score = 0;
    G2048_RandState = (Seed != 0) ? Seed : 1;
    G2048_EscState = G2048_ESC_IDLE;

    G2048_SpawnTile();
    G2048_SpawnTile();
}

/** Maps a line/position in the move direction to a board index, position 0 is the target edge */
static uint8_t G2048_CellIndex(G2048_Key_t Dir, uint8_t Line, uint8_t Pos)
{
    uint8_t Index;

    switch (Dir)
    {
    case G2048_KEY_LEFT:
        Index = Line * G2048_SIZE + Pos;
        break;
    case G2048_KEY_RIGHT:
        Index = Line * G2048_SIZE + (G2048_SIZE - 1 - Pos);
        break;
    case G2048_KEY_UP:
        Index = Pos * G2048_SIZE + Line;
        break;
    default:
        Index = (G2048_SIZE - 1 - Pos) * G2048_SIZE + Line;
        break;
    }

    return Index;
}

/** Slides and merges one line towards position 0, each tile merges at most once */
static bool G2048_SlideLine(uint32_t *Line)
{
    uint32_t Out[G2048_SIZE] = { 0 };
    uint8_t In;
    uint8_t OutPos = 0;
    bool CanMerge = false;
    bool Moved = false;

    for (In = 0; In < G2048_SIZE; In++)
    {
        if (Line[In] != 0)
        {
            if (CanMerge && (Out[OutPos - 1] == Line[In]))
            {
                Out[OutPos - 1] *= 2;
                G2048_Score += Out[OutPos - 1];
                CanMerge = false;
            }
            else
            {
                Out[OutPos] = Line[In];
                OutPos++;
                CanMerge = true;
            }
        }
    }

    for (In = 0; In < G2048_SIZE; In++)
    {
        if (Line[In] != Out[In])
        {
            Moved = true;
        }
        Line[In] = Out[In];
    }

    return Moved;
}

static bool G2048_Move(G2048_Key_t Dir)
{
    uint32_t Line[G2048_SIZE];
    uint8_t LineIdx;
    uint8_t Pos;
    bool Moved = false;

    for (LineIdx = 0; LineIdx < G2048_SIZE; LineIdx++)
    {
        for (Pos = 0; Pos < G2048_SIZE; Pos++)
        {
            Line[Pos] = G2048_Board[G2048_CellIndex(Dir, LineIdx, Pos)];
        }

        if (G2048_SlideLine(Line))
        {
            Moved = true;
            for (Pos = 0; Pos < G2048_SIZE; Pos++)
            {
                G2048_Board[G2048_CellIndex(Dir, LineIdx, Pos)] = Line[Pos];
            }
        }
    }

    return Moved;
}

static bool G2048_CanMove(void)
{
    uint8_t Row;
    uint8_t Col;
    uint32_t Value;
    bool CanMove = false;

    for (Row = 0; Row < G2048_SIZE; Row++)
    {
        for (Col = 0; Col < G2048_SIZE; Col++)
        {
            Value = G2048_Board[Row * G2048_SIZE + Col];
            if ((Value == 0) ||
                ((Col < G2048_SIZE - 1) && (Value == G2048_Board[Row * G2048_SIZE + Col + 1])) ||
                ((Row < G2048_SIZE - 1) && (Value == G2048_Board[(Row + 1) * G2048_SIZE + Col])))
            {
                CanMove = true;
            }
        }
    }

    return CanMove;
}

static bool G2048_IsWon(void)
{
    uint8_t Index;
    bool Won = false;

    for (Index = 0; Index < G2048_CELLS; Index++)
    {
        if (G2048_Board[Index] >= G2048_WIN_VALUE)
        {
            Won = true;
        }
    }

    return Won;
}

static void G2048_DrawHLine(uint8_t Count)
{
    uint8_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        PSH_Send(TUI_BOX_H);
    }
}

/** Top edge of the outer frame, interrupted at the top left by the title */
static void G2048_DrawFrameTop(void)
{
    PSH_Send(TUI_BOX_TL);
    G2048_DrawHLine(1);
    PSH_Send(G2048_TITLE);
    G2048_DrawHLine(G2048_FRAME_WIDTH - 1 - (sizeof(G2048_TITLE) - 1));
    PSH_Send(TUI_BOX_TR TUI_NL);
}

static void G2048_DrawFrameBottom(void)
{
    PSH_Send(TUI_BOX_BL);
    G2048_DrawHLine(G2048_FRAME_WIDTH);
    PSH_Send(TUI_BOX_BR TUI_NL);
}

/** One text line inside the outer frame, padded to the frame width */
static void G2048_DrawFrameText(const char *pText)
{
    PSH_Printf(TUI_BOX_V " %-*s " TUI_BOX_V TUI_NL, G2048_GRID_WIDTH, pText);
}

static void G2048_DrawBorder(const char *Left, const char *Mid, const char *Right)
{
    uint8_t Col;

    PSH_Send(TUI_BOX_V " ");
    PSH_Send(Left);
    for (Col = 0; Col < G2048_SIZE; Col++)
    {
        G2048_DrawHLine(G2048_CELL_WIDTH);
        PSH_Send((Col < G2048_SIZE - 1) ? Mid : Right);
    }
    PSH_Send(" " TUI_BOX_V TUI_NL);
}

static void G2048_Draw(void)
{
    uint8_t Row;
    uint8_t Col;
    uint32_t Value;

    PSH_Send(TUI_HOME TUI_CLEAR);
    G2048_DrawFrameTop();
    PSH_Printf(TUI_BOX_V " " G2048_SCORE_LABEL "%-*lu " TUI_BOX_V TUI_NL,
               (int)(G2048_GRID_WIDTH - (sizeof(G2048_SCORE_LABEL) - 1)), (unsigned long)G2048_Score);
    G2048_DrawFrameText("");

    G2048_DrawBorder(TUI_BOX_TL, TUI_BOX_T, TUI_BOX_TR);
    for (Row = 0; Row < G2048_SIZE; Row++)
    {
        PSH_Send(TUI_BOX_V " " TUI_BOX_V);
        for (Col = 0; Col < G2048_SIZE; Col++)
        {
            Value = G2048_Board[Row * G2048_SIZE + Col];
            if (Value != 0)
            {
                PSH_Printf(" %5lu " TUI_BOX_V, (unsigned long)Value);
            }
            else
            {
                PSH_Send("       " TUI_BOX_V);
            }
        }
        PSH_Send(" " TUI_BOX_V TUI_NL);

        if (Row < G2048_SIZE - 1)
        {
            G2048_DrawBorder(TUI_BOX_L, TUI_BOX_X, TUI_BOX_R);
        }
    }
    G2048_DrawBorder(TUI_BOX_BL, TUI_BOX_B, TUI_BOX_BR);

    if (!G2048_CanMove())
    {
        G2048_DrawFrameText("Game over!");
    }
    else if (G2048_IsWon())
    {
        G2048_DrawFrameText("You win!");
    }
    else
    {
        G2048_DrawFrameText("");
    }

    G2048_DrawFrameText("Arrows: move   q: quit");
    G2048_DrawFrameBottom();
}

/** Decodes ESC [ A..D (or ESC O A..D) arrow sequences and 'q' */
static G2048_Key_t G2048_DecodeKey(int Ch)
{
    G2048_Key_t Key = G2048_KEY_NONE;

    switch (G2048_EscState)
    {
    case G2048_ESC_IDLE:
        if (Ch == G2048_CHR_ESC)
        {
            G2048_EscState = G2048_ESC_START;
        }
        else if ((Ch == 'q') || (Ch == 'Q'))
        {
            Key = G2048_KEY_QUIT;
        }
        break;

    case G2048_ESC_START:
        G2048_EscState = ((Ch == G2048_CHR_CSI) || (Ch == G2048_CHR_SS3)) ? G2048_ESC_SEQ : G2048_ESC_IDLE;
        break;

    default:
        G2048_EscState = G2048_ESC_IDLE;
        switch (Ch)
        {
        case G2048_CHR_UP:
            Key = G2048_KEY_UP;
            break;
        case G2048_CHR_DOWN:
            Key = G2048_KEY_DOWN;
            break;
        case G2048_CHR_RIGHT:
            Key = G2048_KEY_RIGHT;
            break;
        case G2048_CHR_LEFT:
            Key = G2048_KEY_LEFT;
            break;
        default:
            break;
        }
        break;
    }

    return Key;
}

/** Blocks until the next input byte of the shell app, returns EOF after Ctrl+C, even if keys are still queued */
static int G2048_GetChar(void)
{
    uint8_t Byte = 0U;
    size_t Length;
    int Ch = EOF;

    Length = PSH_AppRead(&Byte, sizeof(Byte), portMAX_DELAY);
    if ((Length != 0U) && !PSH_AppStopRequested())
    {
        Ch = Byte;
    }

    return Ch;
}

void G2048_Run(uint32_t Seed)
{
    int Ch;
    G2048_Key_t Key = G2048_KEY_NONE;

    G2048_Init(Seed);
    PSH_Send(TUI_HIDE_CURSOR);
    G2048_Draw();

    while (Key != G2048_KEY_QUIT)
    {
        Ch = G2048_GetChar();
        if (Ch == EOF)
        {
            Key = G2048_KEY_QUIT;
        }
        else
        {
            Key = G2048_DecodeKey(Ch);
            if ((Key != G2048_KEY_NONE) && (Key != G2048_KEY_QUIT))
            {
                if (G2048_CanMove() && G2048_Move(Key))
                {
                    G2048_SpawnTile();
                }
                G2048_Draw();
            }
        }
    }

    PSH_Send(TUI_SHOW_CURSOR);
}
