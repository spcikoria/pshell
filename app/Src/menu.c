#include "menu.h"
#include "pshell.h"
#include "tui.h"
#include <stdio.h>

/** Layout: the tabs above the frame, the list inside it, the status and the help below it */
#define MENU_TABS_ROW   (1U)
#define MENU_STATUS_ROW (18U)
#define MENU_HELP_ROW   (19U)
#define MENU_TAB_COUNT  (3U)

/** Size of the status line text */
#define MENU_STATUS_SIZE (64U)

/** Number of items of a static array */
#define MENU_COUNT(Array) ((uint8_t)(sizeof(Array) / sizeof((Array)[0])))

/** One tab: its items and its own selection */
typedef struct
{
    TUI_Item_t *pItems;
    uint8_t Count;
    uint8_t Selected;
} MENU_Tab_t;

static const TUI_Rect_t MENU_Frame = {.Row = 2U, .Col = 1U, .Height = 16U, .Width = 50U};

/** Inside the frame, one column of margin on both sides */
static const TUI_Rect_t MENU_List = {.Row = 3U, .Col = 3U, .Height = 14U, .Width = 46U};

static const char *const MENU_TabNames[MENU_TAB_COUNT] = {"Commands", "Settings", "About"};

/** A switch: the mark and the color change with the state, both colors are set */
static const TUI_ToggleStyle_t MENU_Switch = {
    .pOnMark = "ON  ",
    .pOffMark = "OFF ",
    .OnColor = TUI_COLOR_GREEN,
    .OffColor = TUI_COLOR_RED,
};

/** Demo settings, toggled by Space, nothing reads them */
static TUI_Item_t MENU_SettingsItems[] = {
    {.pText = "Echo", .pToggle = &TUI_Checkbox, .On = true},
    {.pText = "Colors", .pToggle = &TUI_Checkbox, .On = true},
    {.pText = "Command history", .pToggle = &TUI_Checkbox, .On = true},
    {.pText = "Start-up logo", .pToggle = &TUI_Checkbox, .On = false},
    {.pText = "Power save", .pToggle = &MENU_Switch, .On = false},
    {.pText = "Debug output", .pToggle = &MENU_Switch, .On = true},
};

static TUI_Item_t MENU_AboutItems[] = {
    {.pText = "PSHELL on STM32H5"},
    {.pText = "Cortex-M33, FreeRTOS"},
    {.pText = "Drawn with the tui library"},
    {.pText = "Tabs, a titled frame, a list and toggles"},
};

/** Filled at the start from the command table of the shell */
static TUI_Item_t MENU_CommandItems[PSH_MAX_COMMANDS];

static MENU_Tab_t MENU_Tabs[MENU_TAB_COUNT];
static uint8_t MENU_ActiveTab;

/**
 * @brief Fill the tabs, the first item of every tab is selected
 */
static void MENU_Init(void)
{
    const PSH_CommandEntry_t *pEntry;
    uint8_t Count = 0U;

    for (pEntry = PSH_GetCommand(Count); (pEntry != NULL) && (Count < PSH_MAX_COMMANDS); pEntry = PSH_GetCommand(Count))
    {
        MENU_CommandItems[Count] = (TUI_Item_t){.pText = pEntry->pCommand};
        Count++;
    }

    MENU_Tabs[0] = (MENU_Tab_t){.pItems = MENU_CommandItems, .Count = Count, .Selected = 0U};
    MENU_Tabs[1] = (MENU_Tab_t){.pItems = MENU_SettingsItems, .Count = MENU_COUNT(MENU_SettingsItems), .Selected = 0U};
    MENU_Tabs[2] = (MENU_Tab_t){.pItems = MENU_AboutItems, .Count = MENU_COUNT(MENU_AboutItems), .Selected = 0U};
    MENU_ActiveTab = 0U;
}

/**
 * @brief Draw the list of the active tab
 */
static void MENU_DrawList(void)
{
    const MENU_Tab_t *pTab = &MENU_Tabs[MENU_ActiveTab];

    TUI_DrawList(&MENU_List, pTab->pItems, pTab->Count, pTab->Selected);
}

/**
 * @brief Draw the tabs, and the frame and the list of the active tab, the frame title is the tab name
 */
static void MENU_DrawTab(void)
{
    TUI_DrawTabs(MENU_TABS_ROW, MENU_Frame.Col, MENU_TabNames, MENU_TAB_COUNT, MENU_ActiveTab);

    TUI_SetColor(TUI_COLOR_LIGHT_BLUE, TUI_COLOR_DEFAULT);
    TUI_DrawFrame(&MENU_Frame, MENU_TabNames[MENU_ActiveTab]);
    MENU_DrawList();
}

/**
 * @brief Print the status line below the frame
 * @param[in] pText Text, "" clears the line
 */
static void MENU_DrawStatus(const char *pText)
{
    TUI_ResetStyle();
    TUI_MoveTo(MENU_STATUS_ROW, MENU_Frame.Col);
    TUI_PrintField(pText, MENU_Frame.Width);
}

/**
 * @brief Show the chosen item of the active tab in the status line
 */
static void MENU_Choose(void)
{
    const MENU_Tab_t *pTab = &MENU_Tabs[MENU_ActiveTab];
    char Text[MENU_STATUS_SIZE];

    if (pTab->Count > 0U)
    {
        (void)snprintf(Text, sizeof(Text), "Chosen: %s > %s", MENU_TabNames[MENU_ActiveTab], pTab->pItems[pTab->Selected].pText);
        MENU_DrawStatus(Text);
    }
}

/**
 * @brief Flip the selected item of the active tab if it is a toggle
 */
static void MENU_ToggleSelected(void)
{
    const MENU_Tab_t *pTab = &MENU_Tabs[MENU_ActiveTab];
    TUI_Item_t *pItem;
    char Text[MENU_STATUS_SIZE];

    if (pTab->Count > 0U)
    {
        pItem = &pTab->pItems[pTab->Selected];
        if (pItem->pToggle != NULL)
        {
            (void)snprintf(Text, sizeof(Text), "%s: %s", pItem->pText, TUI_Toggle(pItem) ? "on" : "off");
            MENU_DrawList();
            MENU_DrawStatus(Text);
        }
    }
}

/*****************************************************************************/
void MENU_Main(uint8_t Argc, char **ppArgv)
{
    MENU_Tab_t *pTab;
    int Key = TUI_KEY_NONE;

    (void)Argc;
    (void)ppArgv;

    MENU_Init();

    TUI_Begin();
    MENU_DrawTab();
    MENU_DrawStatus("");
    TUI_SetColor(TUI_COLOR_GRAY, TUI_COLOR_DEFAULT);
    TUI_PrintAt(MENU_HELP_ROW, MENU_Frame.Col, "Tab: next tab  Up/Down: select  Space: toggle  Enter: choose  q: quit");

    while ((Key != TUI_KEY_STOP) && (Key != 'q') && (Key != 'Q'))
    {
        Key = TUI_ReadKey();
        pTab = &MENU_Tabs[MENU_ActiveTab];

        switch (Key)
        {
        case TUI_KEY_TAB:
        case TUI_KEY_RIGHT:
            MENU_ActiveTab = (uint8_t)((MENU_ActiveTab + 1U) % MENU_TAB_COUNT);
            MENU_DrawTab();
            break;

        case TUI_KEY_BACKTAB:
        case TUI_KEY_LEFT:
            MENU_ActiveTab = (uint8_t)((MENU_ActiveTab + MENU_TAB_COUNT - 1U) % MENU_TAB_COUNT);
            MENU_DrawTab();
            break;

        case TUI_KEY_UP:
            if (pTab->Selected > 0U)
            {
                pTab->Selected--;
                MENU_DrawList();
            }
            break;

        case TUI_KEY_DOWN:
            if ((pTab->Selected + 1U) < pTab->Count)
            {
                pTab->Selected++;
                MENU_DrawList();
            }
            break;

        case TUI_KEY_SPACE:
            MENU_ToggleSelected();
            break;

        case TUI_KEY_ENTER:
            MENU_Choose();
            break;

        default:
            break;
        }
    }

    TUI_End();
}
