#include "tui.h"
#include "pshell.h"
#include "standard.h"

/** Time to wait for the rest of an escape sequence, a lone ESC key sends nothing after it */
#define TUI_ESC_TIMEOUT_MS  (50U)

/** Buffer of TUI_Repeat(), one write instead of one per character */
#define TUI_REPEAT_BUF_SIZE (48U)

/** Smallest frame: two corners, "─ ", and " " after the title */
#define TUI_FRAME_MIN_WIDTH (5U)

/* Box elements (UTF-8) */
#define TUI_BOX_H  "\xE2\x94\x80" /* ─ */
#define TUI_BOX_V  "\xE2\x94\x82" /* │ */
#define TUI_BOX_TL "\xE2\x94\x8C" /* ┌ */
#define TUI_BOX_TR "\xE2\x94\x90" /* ┐ */
#define TUI_BOX_BL "\xE2\x94\x94" /* └ */
#define TUI_BOX_BR "\xE2\x94\x98" /* ┘ */
#define TUI_BOX_L  "\xE2\x94\x9C" /* ├ */
#define TUI_BOX_R  "\xE2\x94\xA4" /* ┤ */

/*****************************************************************************/
const TUI_ToggleStyle_t TUI_Checkbox = {
    .pOnMark = "[*] ",
    .pOffMark = "[ ] ",
    .OnColor = TUI_COLOR_LIGHT_BLUE,
    .OffColor = TUI_COLOR_DEFAULT,
};

/**
 * @brief Send a text several times
 * @param[in] pText Text, shorter than TUI_REPEAT_BUF_SIZE
 * @param[in] Count Number of times
 */
static void TUI_Repeat(const char *pText, uint8_t Count)
{
    char Buffer[TUI_REPEAT_BUF_SIZE];
    size_t Length = STD_Strlen(pText);
    size_t Used = 0U;

    while (Count > 0U)
    {
        if ((Used + Length) > sizeof(Buffer))
        {
            PSH_Write(Buffer, Used);
            Used = 0U;
        }
        STD_Memcpy(&Buffer[Used], pText, Length);
        Used += Length;
        Count--;
    }

    if (Used > 0U)
    {
        PSH_Write(Buffer, Used);
    }
}

/**
 * @brief Read one input byte of the app
 * @param[in] Timeout Maximum time to wait in ticks
 * @return The byte, TUI_KEY_NONE on timeout, TUI_KEY_STOP on Ctrl+C
 */
static int TUI_ReadByte(TickType_t Timeout)
{
    uint8_t Byte;
    int Key = TUI_KEY_NONE;

    if (PSH_AppRead(&Byte, sizeof(Byte), Timeout) != 0U)
    {
        Key = Byte;
    }
    if (PSH_AppStopRequested() || (Key == PSH_KEY_CTRL_C))
    {
        Key = TUI_KEY_STOP;
    }
    return Key;
}

/*****************************************************************************/
void TUI_Begin(void)
{
    PSH_Send("\x1b[0m\x1b[?25l\x1b[2J\x1b[H");
}

/*****************************************************************************/
void TUI_End(void)
{
    PSH_Send("\x1b[0m\x1b[2J\x1b[H\x1b[?25h");
}

/*****************************************************************************/
void TUI_Clear(void)
{
    PSH_Send("\x1b[2J\x1b[H");
}

/*****************************************************************************/
void TUI_MoveTo(uint8_t Row, uint8_t Col)
{
    PSH_Printf("\x1b[%u;%uH", (unsigned int)Row, (unsigned int)Col);
}

/*****************************************************************************/
void TUI_SetColor(int16_t Fg, int16_t Bg)
{
    if (Fg < 0)
    {
        PSH_Send("\x1b[39m");
    }
    else
    {
        PSH_Printf("\x1b[38;5;%um", (unsigned int)Fg);
    }

    if (Bg < 0)
    {
        PSH_Send("\x1b[49m");
    }
    else
    {
        PSH_Printf("\x1b[48;5;%um", (unsigned int)Bg);
    }
}

/*****************************************************************************/
void TUI_ResetStyle(void)
{
    PSH_Send("\x1b[0m");
}

/*****************************************************************************/
void TUI_PrintAt(uint8_t Row, uint8_t Col, const char *pText)
{
    TUI_MoveTo(Row, Col);
    PSH_Send(pText);
}

/*****************************************************************************/
void TUI_PrintField(const char *pText, uint8_t Width)
{
    size_t Length = STD_Strlen(pText);

    if (Length <= Width)
    {
        PSH_Write(pText, Length);
        TUI_Repeat(" ", (uint8_t)(Width - Length));
    }
    else if (Width >= 3U)
    {
        PSH_Write(pText, Width - 3U);
        PSH_Send("...");
    }
    else
    {
        PSH_Write(pText, Width);
    }
}

/*****************************************************************************/
void TUI_Shorten(char *pDest, const char *pSrc, uint8_t Width)
{
    size_t Length = STD_Strlen(pSrc);

    if (Length <= Width)
    {
        STD_Memcpy(pDest, pSrc, Length + 1U);
    }
    else if (Width >= 3U)
    {
        STD_Memcpy(pDest, pSrc, Width - 3U);
        STD_Memcpy(&pDest[Width - 3U], "...", 4U);
    }
    else
    {
        STD_Memcpy(pDest, pSrc, Width);
        pDest[Width] = '\0';
    }
}

/*****************************************************************************/
void TUI_FrameTop(uint8_t Width, const char *pTitle)
{
    uint8_t Inner = Width - 2U;
    size_t TitleLength = 0U;

    if (Width >= TUI_FRAME_MIN_WIDTH)
    {
        if (pTitle != NULL)
        {
            TitleLength = STD_Strlen(pTitle);
        }

        /* The title needs "─ " before it and " " after it */
        if (TitleLength > (size_t)(Inner - 3U))
        {
            TitleLength = Inner - 3U;
        }

        PSH_Send(TUI_BOX_TL);
        if (TitleLength > 0U)
        {
            PSH_Send(TUI_BOX_H " ");
            PSH_Write(pTitle, TitleLength);
            PSH_Send(" ");
            TUI_Repeat(TUI_BOX_H, (uint8_t)(Inner - 3U - TitleLength));
        }
        else
        {
            TUI_Repeat(TUI_BOX_H, Inner);
        }
        PSH_Send(TUI_BOX_TR);
    }
}

/*****************************************************************************/
void TUI_FrameRow(uint8_t Width, const char *pText)
{
    if (Width >= TUI_FRAME_MIN_WIDTH)
    {
        PSH_Send(TUI_BOX_V " ");
        TUI_PrintField(pText, Width - 4U);
        PSH_Send(" " TUI_BOX_V);
    }
}

/*****************************************************************************/
void TUI_FrameSeparator(uint8_t Width)
{
    if (Width >= TUI_FRAME_MIN_WIDTH)
    {
        PSH_Send(TUI_BOX_L);
        TUI_Repeat(TUI_BOX_H, Width - 2U);
        PSH_Send(TUI_BOX_R);
    }
}

/*****************************************************************************/
void TUI_FrameBottom(uint8_t Width)
{
    if (Width >= TUI_FRAME_MIN_WIDTH)
    {
        PSH_Send(TUI_BOX_BL);
        TUI_Repeat(TUI_BOX_H, Width - 2U);
        PSH_Send(TUI_BOX_BR);
    }
}

/*****************************************************************************/
void TUI_DrawFrame(const TUI_Rect_t *pRect, const char *pTitle)
{
    uint8_t Row;

    if (pRect->Height >= 2U)
    {
        TUI_MoveTo(pRect->Row, pRect->Col);
        TUI_FrameTop(pRect->Width, pTitle);

        /* An empty row clears the inside */
        for (Row = 1U; Row < (pRect->Height - 1U); Row++)
        {
            TUI_MoveTo(pRect->Row + Row, pRect->Col);
            TUI_FrameRow(pRect->Width, "");
        }

        TUI_MoveTo(pRect->Row + pRect->Height - 1U, pRect->Col);
        TUI_FrameBottom(pRect->Width);
    }
}

/*****************************************************************************/
void TUI_DrawTabs(uint8_t Row, uint8_t Col, const char *const *ppNames, uint8_t Count, uint8_t Active)
{
    uint8_t Index;

    TUI_MoveTo(Row, Col);
    for (Index = 0U; Index < Count; Index++)
    {
        if (Index == Active)
        {
            TUI_SetColor(TUI_COLOR_SELECT_FG, TUI_COLOR_SELECT_BG);
        }
        PSH_Printf(" %s ", ppNames[Index]);
        TUI_ResetStyle();
        PSH_Send(" ");
    }
}

/*****************************************************************************/
void TUI_DrawItem(uint8_t Row, uint8_t Col, uint8_t Width, const TUI_Item_t *pItem, bool Highlight)
{
    const char *pMark = "";
    size_t MarkLength;

    if (Width > 0U)
    {
        if (pItem->pToggle != NULL)
        {
            pMark = pItem->On ? pItem->pToggle->pOnMark : pItem->pToggle->pOffMark;
        }

        /* One column is the left margin */
        MarkLength = STD_Strlen(pMark);
        if (MarkLength > (size_t)(Width - 1U))
        {
            MarkLength = Width - 1U;
        }

        TUI_MoveTo(Row, Col);
        if (Highlight)
        {
            TUI_SetColor(TUI_COLOR_SELECT_FG, TUI_COLOR_SELECT_BG);
        }
        else if (pItem->pToggle != NULL)
        {
            TUI_SetColor(pItem->On ? pItem->pToggle->OnColor : pItem->pToggle->OffColor, TUI_COLOR_DEFAULT);
        }
        else
        {
            TUI_ResetStyle();
        }

        PSH_Send(" ");
        PSH_Write(pMark, MarkLength);
        TUI_PrintField(pItem->pText, (uint8_t)(Width - 1U - MarkLength));
        TUI_ResetStyle();
    }
}

/*****************************************************************************/
void TUI_DrawList(const TUI_Rect_t *pRect, const TUI_Item_t *pItems, uint8_t Count, uint8_t Selected)
{
    uint16_t First = 0U;
    uint16_t Index;
    uint8_t Row;

    /* Scroll so the selected item is on the last row */
    if (Selected >= pRect->Height)
    {
        First = (uint16_t)(Selected - pRect->Height + 1U);
    }

    TUI_ResetStyle();
    for (Row = 0U; Row < pRect->Height; Row++)
    {
        Index = First + Row;

        if (Index < Count)
        {
            TUI_DrawItem(pRect->Row + Row, pRect->Col, pRect->Width, &pItems[Index], Index == Selected);
        }
        else
        {
            TUI_MoveTo(pRect->Row + Row, pRect->Col);
            TUI_Repeat(" ", pRect->Width);
        }
    }
}

/*****************************************************************************/
bool TUI_Toggle(TUI_Item_t *pItem)
{
    if (pItem->pToggle != NULL)
    {
        pItem->On = !pItem->On;
    }
    return pItem->On;
}

/**
 * @brief Wait for a key and decode it
 * @param[in] Timeout Maximum time to wait for the first byte in ticks
 * @return A printable character, a TUI_KEY_x value, or TUI_KEY_NONE on timeout
 */
static int TUI_ReadKeyTicks(TickType_t Timeout)
{
    int Key = TUI_ReadByte(Timeout);
    int Next;

    if (Key == '\n')
    {
        Key = TUI_KEY_ENTER;
    }
    else if (Key == TUI_KEY_ESC)
    {
        Next = TUI_ReadByte(pdMS_TO_TICKS(TUI_ESC_TIMEOUT_MS));

        if ((Next == '[') || (Next == 'O'))
        {
            /* ESC [ x or ESC O x, the parameterized sequences are not decoded */
            Next = TUI_ReadByte(pdMS_TO_TICKS(TUI_ESC_TIMEOUT_MS));
            switch (Next)
            {
            case 'A':
                Key = TUI_KEY_UP;
                break;
            case 'B':
                Key = TUI_KEY_DOWN;
                break;
            case 'C':
                Key = TUI_KEY_RIGHT;
                break;
            case 'D':
                Key = TUI_KEY_LEFT;
                break;
            case 'Z':
                Key = TUI_KEY_BACKTAB;
                break;
            case TUI_KEY_STOP:
                Key = TUI_KEY_STOP;
                break;
            default:
                Key = TUI_KEY_NONE;
                break;
            }
        }
        else if (Next == TUI_KEY_STOP)
        {
            Key = TUI_KEY_STOP;
        }
        else if (Next != TUI_KEY_NONE)
        {
            /* Alt+key, not handled */
            Key = TUI_KEY_NONE;
        }
        else
        {
            /* Nothing followed: the ESC key itself */
        }
    }
    return Key;
}

/*****************************************************************************/
int TUI_ReadKey(void)
{
    return TUI_ReadKeyTicks(portMAX_DELAY);
}

/*****************************************************************************/
int TUI_ReadKeyTimeout(uint32_t TimeoutMs)
{
    return TUI_ReadKeyTicks(pdMS_TO_TICKS(TimeoutMs));
}
