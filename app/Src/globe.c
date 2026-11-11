/**
 * @file    globe.c
 * @brief   Rotating ASCII globe, a shell app.
 *
 * Every row of the map is one circle of latitude, stored as the full 360 degrees from 180W to the
 * east. A row is as long as its circle is on the screen: twice its visible width, so the polar
 * rows are short and the equator rows are long.
 *
 * All rows share one phase (0..65535 is one turn), a row starts at phase * length / 65536. So every
 * row turns by the same angle per frame, and the long rows near the equator move more characters
 * than the short ones near the poles. Only the front half of a row is printed, centered, streamed
 * from flash to the shell and wrapped around like a circular buffer. No frame buffer, the only state
 * is the phase.
 *
 * The rows are spaced evenly on the screen, not in latitude (sin(latitude) = 0.95, 0.85 ... -0.95,
 * 72N ... 72S), as the Earth is seen from space. '#' land (green), '*' ice (white), '.' sea (blue).
 * A color is only sent where it changes, across rows as well.
 */
#include "globe.h"
#include "pshell.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>

/* Escape sequences */
#define TUI_ESC          "\x1b"
#define TUI_HOME         TUI_ESC "[H"
#define TUI_CLEAR        TUI_ESC "[2J"
#define TUI_HIDE_CURSOR  TUI_ESC "[?25l"
#define TUI_SHOW_CURSOR  TUI_ESC "[?25h"
#define TUI_RESET        TUI_ESC "[0m"
#define TUI_BLUE         TUI_ESC "[34m"
#define TUI_GREEN        TUI_ESC "[32m"
#define TUI_WHITE        TUI_ESC "[97m"
#define TUI_NL           "\r\n"

/** Width of the equator on the screen, twice the number of rows: characters are about twice as high as wide */
#define GLOBE_WIDTH      (40U)

/** Frame period: a frame with the colors is about 1400 bytes, 15 ms at 921600 baud, the period sets the speed */
#define GLOBE_FRAME_MS   (150U)

/** Phase step per frame: the equator (80 characters) moves one character per frame, a turn takes 12 s */
#define GLOBE_STEP       (65536U / (2U * GLOBE_WIDTH))

/** No color sent yet */
#define GLOBE_NO_COLOR   ('\0')

/** One circle of latitude, Length is the full 360 degrees */
typedef struct
{
    const char *pMap;
    uint8_t Length;
} GLOBE_Row_t;

/** Row from a string literal, the length is known at compile time */
#define GLOBE_ROW(Str) { (Str), (uint8_t)(sizeof(Str) - 1U) }

/** The Earth from 72N to 72S, each row starts at 180W */
static const GLOBE_Row_t GLOBE_Map[] =
{
    GLOBE_ROW(".#######***..###########"),                                                         /* 72N */
    GLOBE_ROW("..##.#####..###.*....#.#.##############..#.."),                                     /* 58N */
    GLOBE_ROW("........##########.......######################....."),                             /* 49N */
    GLOBE_ROW(".........#########...........#..######.############..#......"),                     /* 41N */
    GLOBE_ROW("............#######.............####....#################.##........"),             /* 33N */
    GLOBE_ROW(".............####..#.............#############.#############............"),         /* 27N */
    GLOBE_ROW("................##.#.##...........################...###..###..............."),     /* 21N */
    GLOBE_ROW("...................#..............##############......#....##..#............"),     /* 15N */
    GLOBE_ROW(".....................######..........##############......#....##...#............"), /*  9N */
    GLOBE_ROW(".......................######.............########............#..#.............."), /*  3N */
    GLOBE_ROW("......................#########...........#######.............##.##.####........"), /*  3S */
    GLOBE_ROW("......................##########...........######..............#####...##......."), /*  9S */
    GLOBE_ROW("......................########...........######.#................##.#......."),     /* 15S */
    GLOBE_ROW(".......................#######...........####..#...............######......."),     /* 21S */
    GLOBE_ROW("......................####.............####................########....."),         /* 27S */
    GLOBE_ROW("....................####.............##.................#..####....."),             /* 33S */
    GLOBE_ROW("..................##.................................##...##"),                     /* 41S */
    GLOBE_ROW("...............#..................................#."),                             /* 49S */
    GLOBE_ROW(".............#.............................."),                                     /* 58S */
    GLOBE_ROW(".*******...************."),                                                         /* 72S */
};

#define GLOBE_ROWS (sizeof(GLOBE_Map) / sizeof(GLOBE_Map[0]))

/** Left margin that centers a row, the narrowest row needs the most */
static const char GLOBE_Spaces[] = "                    ";

/**
 * @brief Color escape sequence of a map character
 * @param[in] Ch Map character
 * @return Escape sequence
 */
static const char *GLOBE_ColorOf(char Ch)
{
    const char *pColor;

    switch (Ch)
    {
    case '#':
        pColor = TUI_GREEN;
        break;
    case '*':
        pColor = TUI_WHITE;
        break;
    default:
        pColor = TUI_BLUE;
        break;
    }

    return pColor;
}

/**
 * @brief Print the front half of one row, centered
 * @param[in] pRow Row to print
 * @param[in] Phase Rotation, 0..65535 is one turn
 * @param[in,out] pColor Map character of the color sent last, updated
 */
static void GLOBE_DrawRow(const GLOBE_Row_t *pRow, uint16_t Phase, char *pColor)
{
    uint8_t Remaining = pRow->Length / 2U;
    uint8_t Index = (uint8_t)(((uint32_t)Phase * pRow->Length) >> 16);
    size_t Margin = (GLOBE_WIDTH - Remaining) / 2U;
    uint8_t Run;
    char Ch;

    if (Margin > (sizeof(GLOBE_Spaces) - 1U))
    {
        Margin = sizeof(GLOBE_Spaces) - 1U;
    }
    PSH_Write(GLOBE_Spaces, Margin);

    /* Runs of the same character, a run ends at the end of the row: circular buffer */
    while (Remaining > 0U)
    {
        Ch = pRow->pMap[Index];
        Run = 1U;
        while ((Run < Remaining) && ((Index + Run) < pRow->Length) && (pRow->pMap[Index + Run] == Ch))
        {
            Run++;
        }

        if (Ch != *pColor)
        {
            PSH_Send(GLOBE_ColorOf(Ch));
            *pColor = Ch;
        }
        PSH_Write(&pRow->pMap[Index], Run);

        Index += Run;
        if (Index == pRow->Length)
        {
            Index = 0U;
        }
        Remaining -= Run;
    }
    PSH_Send(TUI_NL);
}

/**
 * @brief Check the keys pressed since the last frame, without waiting
 * @return true if one of them was q or Q
 */
static bool GLOBE_QuitPressed(void)
{
    uint8_t Keys[8];
    size_t Length;
    size_t Index;
    bool Quit = false;

    do
    {
        Length = PSH_AppRead(Keys, sizeof(Keys), 0U);
        for (Index = 0U; Index < Length; Index++)
        {
            if ((Keys[Index] == (uint8_t)'q') || (Keys[Index] == (uint8_t)'Q'))
            {
                Quit = true;
            }
        }
    } while (Length == sizeof(Keys));

    return Quit;
}

/*****************************************************************************/
void GLOBE_Main(uint8_t Argc, char **ppArgv)
{
    uint16_t Phase = 0U;
    uint8_t Row;
    char Color;
    TickType_t Start;
    uint32_t ElapsedMs;
    uint32_t DelayMs;

    (void)Argc;
    (void)ppArgv;

    PSH_Send(TUI_HIDE_CURSOR TUI_HOME TUI_CLEAR);
    do
    {
        Start = xTaskGetTickCount();

        /* Overwrite the previous frame in place, no clear, no flicker */
        PSH_Send(TUI_HOME);
        Color = GLOBE_NO_COLOR;
        for (Row = 0U; Row < GLOBE_ROWS; Row++)
        {
            GLOBE_DrawRow(&GLOBE_Map[Row], Phase, &Color);
        }
        PSH_Send(TUI_RESET "    q: quit" TUI_NL);

        /* The Earth turns to the east, its surface moves to the right: the window moves to the west */
        Phase = (uint16_t)(Phase - GLOBE_STEP);

        /* Steady frame rate: the time the frame took to send is part of the period */
        ElapsedMs = (uint32_t)(xTaskGetTickCount() - Start) * portTICK_PERIOD_MS;
        DelayMs = (ElapsedMs < GLOBE_FRAME_MS) ? (GLOBE_FRAME_MS - ElapsedMs) : 0U;
    } while (PSH_AppDelay(DelayMs) && !GLOBE_QuitPressed());

    PSH_Send(TUI_RESET TUI_SHOW_CURSOR);
}
