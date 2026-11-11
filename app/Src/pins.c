/**
 * @file    pins.c
 * @brief   GPIO app for the header pins of the board (bsp_pins.h), drawn with the tui library.
 *
 * State tab: the mode and the level of every pin, an output is toggled here, an analog pin shows its ADC value.
 * Mode tab: a pin is set to input, output or analog.
 * Only the cells that changed are redrawn: the shown value of every pin is kept and compared.
 * Fixed pins are shown but never changed. The shared pins (used by a driver, e.g. the SD card) are restored
 * on exit by the board support.
 */
#include "pins.h"
#include "FreeRTOS.h"
#include "bsp_pins.h"
#include "pshell.h"
#include "task.h"
#include "tui.h"
#include <stdio.h>

/** Layout: the tabs above the frame, a header row and one row per pin inside it, the help and the status below it */
#define PINS_TABS_ROW       (1U)
#define PINS_HEADER_ROW     (3U)
#define PINS_FIRST_ROW      (4U)
#define PINS_HELP_ROW       (21U)
#define PINS_STATUS_ROW     (22U)
#define PINS_LINE_WIDTH     (72U)

/** Cells of a row, a TUI_DrawItem() cell starts with one column of margin */
#define PINS_COL_PIN        (3U)
#define PINS_PIN_WIDTH      (5U)
#define PINS_COL_PORT       (9U)
#define PINS_PORT_WIDTH     (6U)
#define PINS_COL_MODE       (15U)
#define PINS_MODE_WIDTH     (8U)
#define PINS_COL_VALUE      (23U)
#define PINS_VALUE_WIDTH    (18U)
#define PINS_COL_OPTION     (15U)
#define PINS_OPTION_WIDTH   (12U)

/** Tabs */
#define PINS_TAB_COUNT      (2U)
#define PINS_TAB_STATE      (0U)
#define PINS_TAB_MODE       (1U)

/** Rows in the frame, more pins of a board are not shown */
#define PINS_MAX            (16U)

/** Polling period of the levels, refresh period of the ADC values */
#define PINS_POLL_MS        (100U)
#define PINS_ADC_PERIOD_MS  (500U)

/** Values without a level or a conversion result */
#define PINS_VALUE_NONE     (-1)
#define PINS_VALUE_ERROR    (-2)

/** Size of a value and of a status text, a value fits for any int32_t millivolts (at most 28 bytes) */
#define PINS_TEXT_SIZE      (32U)
#define PINS_STATUS_SIZE    (64U)

/** Modes, the first PINS_OPTION_COUNT can be selected on the Mode tab, the first four are the BSP modes */
typedef enum
{
    PINS_MODE_INPUT = BSP_PIN_MODE_INPUT,
    PINS_MODE_OUTPUT = BSP_PIN_MODE_OUTPUT,
    PINS_MODE_ANALOG = BSP_PIN_MODE_ANALOG,
    /* Alternate function, set by another driver */
    PINS_MODE_ALT = BSP_PIN_MODE_ALT,
    /* Used by the board */
    PINS_MODE_FIXED,
    PINS_MODE_COUNT
} PINS_Mode_e;

#define PINS_OPTION_COUNT   (3U)

static const TUI_Rect_t PINS_Frame = {.Row = 2U, .Col = 1U, .Height = 19U, .Width = 52U};

static const char *const PINS_TabNames[PINS_TAB_COUNT] = {"State", "Mode"};

static const char *const PINS_ModeNames[PINS_MODE_COUNT] = {"Input", "Output", "Analog", "Alt fn", "Fixed"};

/** Level of an input: HIGH in green, LOW in the default color */
static const TUI_ToggleStyle_t PINS_LevelStyle = {
    .pOnMark = "",
    .pOffMark = "",
    .OnColor = TUI_COLOR_GREEN,
    .OffColor = TUI_COLOR_DEFAULT,
};

/** A fixed pin in gray */
static const TUI_ToggleStyle_t PINS_FixedStyle = {
    .pOnMark = "",
    .pOffMark = "",
    .OnColor = TUI_COLOR_GRAY,
    .OffColor = TUI_COLOR_GRAY,
};

static uint8_t PINS_Count;
static PINS_Mode_e PINS_Mode[PINS_MAX];
static int32_t PINS_Value[PINS_MAX];
static int32_t PINS_Millivolts[PINS_MAX];
static int32_t PINS_Shown[PINS_MAX];
static uint8_t PINS_ActiveTab;
static uint8_t PINS_Selected;
static uint8_t PINS_Option;

/**
 * @brief Mode of a pin from the board
 * @param[in] Index Index of the pin
 * @return The mode
 */
static PINS_Mode_e PINS_ReadMode(uint8_t Index)
{
    PINS_Mode_e Mode = PINS_MODE_FIXED;

    if (!BSP_PIN_GetInfo(Index)->Fixed)
    {
        Mode = (PINS_Mode_e)BSP_PIN_GetMode(Index);
    }
    return Mode;
}

/**
 * @brief Current value of a pin: the level, or the ADC result of an analog pin
 * @param[in] Index Index of the pin
 * @param[in] ReadAdc Convert an analog pin, otherwise its last result is kept
 * @return 0/1, an ADC result, or a PINS_VALUE_x value
 */
static int32_t PINS_ReadValue(uint8_t Index, bool ReadAdc)
{
    int32_t Value = PINS_VALUE_NONE;

    switch (PINS_Mode[Index])
    {
    case PINS_MODE_INPUT:
    case PINS_MODE_OUTPUT:
        Value = BSP_PIN_Read(Index) ? 1 : 0;
        break;
    case PINS_MODE_ANALOG:
        if (!BSP_PIN_GetInfo(Index)->Analog)
        {
            Value = PINS_VALUE_NONE;
        }
        else if (ReadAdc)
        {
            Value = BSP_PIN_AdcRead(Index, &PINS_Millivolts[Index]);
            if (Value == BSP_PIN_ADC_ERROR)
            {
                Value = PINS_VALUE_ERROR;
            }
        }
        else
        {
            Value = PINS_Value[Index];
        }
        break;
    default:
        break;
    }
    return Value;
}

/**
 * @brief Set the mode of a pin
 * @param[in] Index Index of the pin
 * @param[in] Mode PINS_MODE_INPUT, PINS_MODE_OUTPUT or PINS_MODE_ANALOG
 */
static void PINS_SetMode(uint8_t Index, PINS_Mode_e Mode)
{
    BSP_PIN_SetMode(Index, (BSP_PIN_Mode_e)Mode);
    PINS_Mode[Index] = Mode;
    PINS_Value[Index] = PINS_ReadValue(Index, true);
}

/**
 * @brief Draw the value cell of a pin on the State tab
 * @param[in] Index Index of the pin
 */
static void PINS_DrawValue(uint8_t Index)
{
    TUI_Item_t Item = {.pText = "-"};
    char Text[PINS_TEXT_SIZE];
    int32_t Value = PINS_Value[Index];
    int32_t Millivolts = PINS_Millivolts[Index];

    switch (PINS_Mode[Index])
    {
    case PINS_MODE_INPUT:
        Item = (TUI_Item_t){.pText = (Value != 0) ? "HIGH" : "LOW", .pToggle = &PINS_LevelStyle, .On = (Value != 0)};
        break;
    case PINS_MODE_OUTPUT:
        Item = (TUI_Item_t){.pText = (Value != 0) ? "HIGH" : "LOW", .pToggle = &TUI_Checkbox, .On = (Value != 0)};
        break;
    case PINS_MODE_ANALOG:
        if (Value >= 0)
        {
            (void)snprintf(Text, sizeof(Text), "%4ld  %ld.%03ld V",
                           (long)Value, (long)(Millivolts / 1000), (long)(Millivolts % 1000));
            Item.pText = Text;
        }
        else if (Value == PINS_VALUE_ERROR)
        {
            Item.pText = "ADC error";
        }
        else
        {
            Item.pText = "no ADC";
        }
        break;
    default:
        break;
    }

    TUI_DrawItem((uint8_t)(PINS_FIRST_ROW + Index), PINS_COL_VALUE, PINS_VALUE_WIDTH, &Item, false);
    PINS_Shown[Index] = Value;
}

/**
 * @brief Draw the row of a pin on the active tab, the cursor in the selection colors
 * @param[in] Index Index of the pin
 */
static void PINS_DrawRow(uint8_t Index)
{
    const BSP_PIN_Info_t *pInfo = BSP_PIN_GetInfo(Index);
    uint8_t Row = (uint8_t)(PINS_FIRST_ROW + Index);
    TUI_Item_t Item = {.pText = pInfo->pName};
    uint8_t Option;

    if (pInfo->Fixed)
    {
        Item.pToggle = &PINS_FixedStyle;
    }
    TUI_DrawItem(Row, PINS_COL_PIN, PINS_PIN_WIDTH, &Item, Index == PINS_Selected);

    if (pInfo->Fixed)
    {
        TUI_SetColor(TUI_COLOR_GRAY, TUI_COLOR_DEFAULT);
    }
    TUI_MoveTo(Row, PINS_COL_PORT);
    TUI_PrintField(pInfo->pPortName, PINS_PORT_WIDTH);

    if (PINS_ActiveTab == PINS_TAB_STATE)
    {
        TUI_MoveTo(Row, PINS_COL_MODE);
        TUI_PrintField(PINS_ModeNames[PINS_Mode[Index]], PINS_MODE_WIDTH);
        TUI_ResetStyle();
        PINS_DrawValue(Index);
    }
    else if (pInfo->Fixed)
    {
        TUI_MoveTo(Row, PINS_COL_OPTION);
        TUI_PrintField(" Used by the board, not configurable", PINS_OPTION_COUNT * PINS_OPTION_WIDTH);
        TUI_ResetStyle();
    }
    else
    {
        for (Option = 0U; Option < PINS_OPTION_COUNT; Option++)
        {
            Item = (TUI_Item_t){.pText = PINS_ModeNames[Option], .pToggle = &TUI_Checkbox, .On = (PINS_Mode[Index] == (PINS_Mode_e)Option)};
            TUI_DrawItem(Row, (uint8_t)(PINS_COL_OPTION + (Option * PINS_OPTION_WIDTH)), PINS_OPTION_WIDTH, &Item,
                         (Index == PINS_Selected) && (Option == PINS_Option));
        }
    }
}

/**
 * @brief Print the status line below the help
 * @param[in] pText Text, "" clears the line
 */
static void PINS_DrawStatus(const char *pText)
{
    TUI_ResetStyle();
    TUI_MoveTo(PINS_STATUS_ROW, PINS_Frame.Col);
    TUI_PrintField(pText, PINS_LINE_WIDTH);
}

/**
 * @brief Draw the tabs, the frame, the header, every row and the help of the active tab
 */
static void PINS_DrawTab(void)
{
    uint8_t Index;

    TUI_DrawTabs(PINS_TABS_ROW, PINS_Frame.Col, PINS_TabNames, PINS_TAB_COUNT, PINS_ActiveTab);

    TUI_SetColor(TUI_COLOR_LIGHT_BLUE, TUI_COLOR_DEFAULT);
    TUI_DrawFrame(&PINS_Frame, PINS_TabNames[PINS_ActiveTab]);

    TUI_SetColor(TUI_COLOR_GRAY, TUI_COLOR_DEFAULT);
    TUI_PrintAt(PINS_HEADER_ROW, PINS_COL_PIN + 1U, (PINS_ActiveTab == PINS_TAB_STATE) ? "Pin  Port  Mode     State" : "Pin  Port  Mode");
    TUI_ResetStyle();

    for (Index = 0U; Index < PINS_Count; Index++)
    {
        PINS_DrawRow(Index);
    }

    TUI_SetColor(TUI_COLOR_GRAY, TUI_COLOR_DEFAULT);
    TUI_MoveTo(PINS_HELP_ROW, PINS_Frame.Col);
    TUI_PrintField((PINS_ActiveTab == PINS_TAB_STATE) ? "Tab: Mode tab  Up/Down: pin  Space: toggle output  q: quit"
                                                      : "Tab: State tab  Up/Down: pin  Left/Right: mode  Space: set  q: quit",
                   PINS_LINE_WIDTH);
    PINS_DrawStatus("");
}

/**
 * @brief Move the cursor to the next configurable pin, the fixed pins are skipped
 * @param[in] Step -1 up, +1 down
 */
static void PINS_MoveSelection(int8_t Step)
{
    uint8_t Old = PINS_Selected;
    int16_t Next = (int16_t)PINS_Selected + Step;

    while ((Next >= 0) && (Next < (int16_t)PINS_Count) && BSP_PIN_GetInfo((uint8_t)Next)->Fixed)
    {
        Next += Step;
    }

    if ((Next >= 0) && (Next < (int16_t)PINS_Count))
    {
        PINS_Selected = (uint8_t)Next;
        PINS_DrawRow(Old);
        PINS_DrawRow(PINS_Selected);
    }
}

/**
 * @brief Move the cursor between the modes on the Mode tab
 * @param[in] Step -1 left, +1 right
 */
static void PINS_MoveOption(int8_t Step)
{
    int16_t Next = (int16_t)PINS_Option + Step;

    if ((PINS_ActiveTab == PINS_TAB_MODE) && (Next >= 0) && (Next < (int16_t)PINS_OPTION_COUNT))
    {
        PINS_Option = (uint8_t)Next;
        PINS_DrawRow(PINS_Selected);
    }
}

/**
 * @brief Space or Enter: toggle the selected output on the State tab, set the selected mode on the Mode tab
 */
static void PINS_Apply(void)
{
    const BSP_PIN_Info_t *pInfo = BSP_PIN_GetInfo(PINS_Selected);
    char Text[PINS_STATUS_SIZE];

    if (pInfo->Fixed)
    {
        /* Only on a board without a configurable pin */
    }
    else if (PINS_ActiveTab == PINS_TAB_STATE)
    {
        if (PINS_Mode[PINS_Selected] == PINS_MODE_OUTPUT)
        {
            BSP_PIN_Toggle(PINS_Selected);
            PINS_Value[PINS_Selected] = PINS_ReadValue(PINS_Selected, false);
            PINS_DrawValue(PINS_Selected);
        }
        else
        {
            PINS_DrawStatus("Only an output can be toggled, set the mode on the Mode tab");
        }
    }
    else if (PINS_Mode[PINS_Selected] != (PINS_Mode_e)PINS_Option)
    {
        PINS_SetMode(PINS_Selected, (PINS_Mode_e)PINS_Option);
        PINS_DrawRow(PINS_Selected);

        if (pInfo->Shared)
        {
            (void)snprintf(Text, sizeof(Text), "%s: %s, shared pin, restored on exit", pInfo->pName, PINS_ModeNames[PINS_Option]);
        }
        else
        {
            (void)snprintf(Text, sizeof(Text), "%s: %s", pInfo->pName, PINS_ModeNames[PINS_Option]);
        }
        PINS_DrawStatus(Text);
    }
    else
    {
        /* Already in this mode */
    }
}

/**
 * @brief Handle one key
 * @param[in] Key Key of TUI_ReadKeyTimeout()
 * @return false if the app has to quit
 */
static bool PINS_HandleKey(int Key)
{
    bool Running = true;

    switch (Key)
    {
    case TUI_KEY_STOP:
    case 'q':
    case 'Q':
        Running = false;
        break;

    case TUI_KEY_TAB:
    case TUI_KEY_BACKTAB:
        PINS_ActiveTab = (uint8_t)((PINS_ActiveTab + 1U) % PINS_TAB_COUNT);
        PINS_DrawTab();
        break;

    case TUI_KEY_UP:
        PINS_MoveSelection(-1);
        break;

    case TUI_KEY_DOWN:
        PINS_MoveSelection(1);
        break;

    case TUI_KEY_LEFT:
        PINS_MoveOption(-1);
        break;

    case TUI_KEY_RIGHT:
        PINS_MoveOption(1);
        break;

    case TUI_KEY_SPACE:
    case TUI_KEY_ENTER:
        PINS_Apply();
        break;

    default:
        break;
    }
    return Running;
}

/**
 * @brief Read every pin, redraw the value cells that changed on the State tab
 * @param[in] ReadAdc Convert the analog pins too
 */
static void PINS_Refresh(bool ReadAdc)
{
    uint8_t Index;

    for (Index = 0U; Index < PINS_Count; Index++)
    {
        PINS_Value[Index] = PINS_ReadValue(Index, ReadAdc);
        if ((PINS_ActiveTab == PINS_TAB_STATE) && (PINS_Value[Index] != PINS_Shown[Index]))
        {
            PINS_DrawValue(Index);
        }
    }
}

/**
 * @brief Board pins, the modes and values read from the hardware, the cursor on the first configurable pin
 */
static void PINS_Init(void)
{
    uint8_t Index;

    BSP_PIN_Init();
    PINS_Count = (BSP_PIN_Count() < PINS_MAX) ? BSP_PIN_Count() : PINS_MAX;

    for (Index = 0U; Index < PINS_Count; Index++)
    {
        PINS_Millivolts[Index] = 0;
        PINS_Mode[Index] = PINS_ReadMode(Index);
        PINS_Value[Index] = PINS_ReadValue(Index, true);
    }

    PINS_ActiveTab = PINS_TAB_STATE;
    PINS_Selected = 0U;
    while (((PINS_Selected + 1U) < PINS_Count) && BSP_PIN_GetInfo(PINS_Selected)->Fixed)
    {
        PINS_Selected++;
    }
    PINS_Option = (uint8_t)PINS_MODE_INPUT;
}

/*****************************************************************************/
void PINS_Main(uint8_t Argc, char **ppArgv)
{
    TickType_t LastAdc;
    TickType_t Now;
    bool ReadAdc;
    bool Running = true;
    int Key;

    (void)Argc;
    (void)ppArgv;

    PINS_Init();
    TUI_Begin();
    PINS_DrawTab();
    LastAdc = xTaskGetTickCount();

    while (Running)
    {
        Key = TUI_ReadKeyTimeout(PINS_POLL_MS);
        Running = PINS_HandleKey(Key);

        Now = xTaskGetTickCount();
        ReadAdc = (TickType_t)(Now - LastAdc) >= pdMS_TO_TICKS(PINS_ADC_PERIOD_MS);
        if (ReadAdc)
        {
            LastAdc = Now;
        }
        if (Running && (PINS_Count > 0U))
        {
            PINS_Refresh(ReadAdc);
        }
    }

    BSP_PIN_Restore();
    TUI_End();
}
