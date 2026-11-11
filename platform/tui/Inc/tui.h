/**
 * @file    tui.h
 * @brief   Minimal text UI for the shell: cursor, colors, frames, tabs, selection lists, toggles and key input.
 *
 * A small replacement of ncurses: no screen buffer, every call writes the ANSI escape sequences directly
 * with PSH_Write(), so the caller redraws what changed. Rows and columns start from 1, like in the terminal.
 * The drawing functions work in any task (a command can draw frames too), TUI_ReadKey() only in an app.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Colors of the 256 color palette, TUI_COLOR_DEFAULT is the default color of the terminal */
#define TUI_COLOR_DEFAULT    (-1)
#define TUI_COLOR_BLACK      (0)
#define TUI_COLOR_WHITE      (15)
#define TUI_COLOR_GRAY       (245)
#define TUI_COLOR_GREEN      (114)
#define TUI_COLOR_RED        (203)
#define TUI_COLOR_LIGHT_BLUE (117)

/** Colors of the highlighted list item and of the active tab */
#define TUI_COLOR_SELECT_FG  TUI_COLOR_BLACK
#define TUI_COLOR_SELECT_BG  TUI_COLOR_LIGHT_BLUE

/** Keys of TUI_ReadKey(), a printable character is returned as itself */
#define TUI_KEY_STOP         (-1)  /* Ctrl+C, the app has to return */
#define TUI_KEY_NONE         (0)   /* Unknown escape sequence */
#define TUI_KEY_TAB          ('\t')
#define TUI_KEY_ENTER        ('\r')
#define TUI_KEY_SPACE        (' ')
#define TUI_KEY_ESC          (0x1B)
#define TUI_KEY_UP           (0x101)
#define TUI_KEY_DOWN         (0x102)
#define TUI_KEY_RIGHT        (0x103)
#define TUI_KEY_LEFT         (0x104)
#define TUI_KEY_BACKTAB      (0x105) /* Shift+Tab */

/** A rectangle on the screen */
typedef struct
{
    uint8_t Row;
    uint8_t Col;
    uint8_t Height;
    uint8_t Width;
} TUI_Rect_t;

/**
 * Look of a toggle item in its two states: a mark before the text and a text color.
 * E.g. a checkbox: "[*] " / "[ ] ", or a switch: "ON  " / "OFF ".
 */
typedef struct
{
    const char *pOnMark;
    const char *pOffMark;
    int16_t OnColor;
    int16_t OffColor;
} TUI_ToggleStyle_t;

/** Checkbox: "[*] " in light blue when on, "[ ] " in the default color when off */
extern const TUI_ToggleStyle_t TUI_Checkbox;

/** An item of a selection list, a plain text or a toggle */
typedef struct
{
    const char *pText;
    /* NULL for a plain item */
    const TUI_ToggleStyle_t *pToggle;
    /* State of a toggle */
    bool On;
} TUI_Item_t;

/**
 * @brief Start the full screen mode: hides the cursor and clears the screen
 */
void TUI_Begin(void);

/**
 * @brief End the full screen mode: resets the colors, clears the screen and shows the cursor
 */
void TUI_End(void);

/**
 * @brief Clear the screen and move the cursor to the top left corner
 */
void TUI_Clear(void);

/**
 * @brief Move the cursor
 * @param[in] Row Row, from 1
 * @param[in] Col Column, from 1
 */
void TUI_MoveTo(uint8_t Row, uint8_t Col);

/**
 * @brief Set the colors of the next output
 * @param[in] Fg Text color, 0..255 or TUI_COLOR_DEFAULT
 * @param[in] Bg Background color, 0..255 or TUI_COLOR_DEFAULT
 */
void TUI_SetColor(int16_t Fg, int16_t Bg);

/**
 * @brief Back to the default colors and attributes
 */
void TUI_ResetStyle(void);

/**
 * @brief Print a text at a position, in the current colors
 * @param[in] Row Row, from 1
 * @param[in] Col Column, from 1
 * @param[in] pText Text
 */
void TUI_PrintAt(uint8_t Row, uint8_t Col, const char *pText);

/**
 * @brief Print a text at the cursor in a field: a longer text is cut and ends with "...",
 *        a shorter one is padded with spaces
 * @param[in] pText ASCII text
 * @param[in] Width Width of the field
 */
void TUI_PrintField(const char *pText, uint8_t Width);

/**
 * @brief Copy a text, a text longer than Width is cut and ends with "...", e.g. for a printf column
 * @param[out] pDest Destination, at least Width + 1 bytes
 * @param[in] pSrc ASCII text
 * @param[in] Width Maximum length of the copy
 */
void TUI_Shorten(char *pDest, const char *pSrc, uint8_t Width);

/**
 * @brief Print the top line of a frame at the cursor, the title interrupts the line: ┌─ Title ─────┐
 * @note  The inline frame functions draw a frame in the scrolling output (e.g. in a command):
 *        the caller ends every line with "\r\n". The frame is at least 5 columns wide.
 * @param[in] Width Width of the frame
 * @param[in] pTitle Title, NULL or "" for none
 */
void TUI_FrameTop(uint8_t Width, const char *pTitle);

/**
 * @brief Print a line of a frame at the cursor: │ text │, with one space of margin on both sides
 * @param[in] Width Width of the frame
 * @param[in] pText ASCII text, cut or padded to the inside of the frame
 */
void TUI_FrameRow(uint8_t Width, const char *pText);

/**
 * @brief Print a separator line inside a frame at the cursor: ├─────┤, e.g. below the column names of a table
 * @param[in] Width Width of the frame
 */
void TUI_FrameSeparator(uint8_t Width);

/**
 * @brief Print the bottom line of a frame at the cursor: └─────┘
 * @param[in] Width Width of the frame
 */
void TUI_FrameBottom(uint8_t Width);

/**
 * @brief Draw a frame at a position and clear its inside, the title interrupts the top line
 * @note  Drawn in the current colors, the frame is at least 2 rows and 5 columns
 * @param[in] pRect Outer rectangle of the frame
 * @param[in] pTitle Title, NULL or "" for none
 */
void TUI_DrawFrame(const TUI_Rect_t *pRect, const char *pTitle);

/**
 * @brief Draw a row of tabs, the active one in the selection colors
 * @param[in] Row Row, from 1
 * @param[in] Col Column of the first tab, from 1
 * @param[in] ppNames Names of the tabs
 * @param[in] Count Number of tabs
 * @param[in] Active Index of the active tab
 */
void TUI_DrawTabs(uint8_t Row, uint8_t Col, const char *const *ppNames, uint8_t Count, uint8_t Active);

/**
 * @brief Draw one list item: a toggle shows the mark and the color of its state,
 *        a highlighted item is drawn in the selection colors
 * @param[in] Row Row, from 1
 * @param[in] Col Column, from 1
 * @param[in] Width Width of the item, the rest of the field is cleared
 * @param[in] pItem The item
 * @param[in] Highlight Draw it as the selected item of the list
 */
void TUI_DrawItem(uint8_t Row, uint8_t Col, uint8_t Width, const TUI_Item_t *pItem, bool Highlight);

/**
 * @brief Draw a selection list, the selected item in the selection colors
 * @note  A list longer than the rectangle scrolls, so the selected item is always visible
 * @param[in] pRect Rectangle of the list, every row is cleared
 * @param[in] pItems Items
 * @param[in] Count Number of items
 * @param[in] Selected Index of the selected item
 */
void TUI_DrawList(const TUI_Rect_t *pRect, const TUI_Item_t *pItems, uint8_t Count, uint8_t Selected);

/**
 * @brief Flip the state of a toggle item, a plain item does not change
 * @param[in,out] pItem The item
 * @return The new state
 */
bool TUI_Toggle(TUI_Item_t *pItem);

/**
 * @brief Wait for a key, decodes the arrow keys, Tab and Shift+Tab, app context only
 * @return A printable character, or a TUI_KEY_x value
 */
int TUI_ReadKey(void);

/**
 * @brief Wait for a key with a timeout, like TUI_ReadKey(), app context only
 * @param[in] TimeoutMs Maximum time to wait in milliseconds
 * @return A printable character, a TUI_KEY_x value, or TUI_KEY_NONE on timeout
 */
int TUI_ReadKeyTimeout(uint32_t TimeoutMs);
