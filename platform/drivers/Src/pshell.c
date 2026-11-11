#include "pshell.h"
#include "semphr.h"
#include "standard.h"
#include "stream_buffer.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>

/** Argument pointers parsed from the command line */
static char *PSH_Argv[PSH_MAX_ARGC];

/** CLI RX/TX buffer and state: PSH_RxIndex is the length of the line, PSH_CursorPos the cursor in it */
static char PSH_RxBuffer[PSH_CMD_BUF_SIZE];
static uint16_t PSH_RxIndex = 0;
static uint16_t PSH_CursorPos = 0;

static uint8_t PSH_RxStreamBufferStorage[PSH_RX_SB_SIZE];
static StaticStreamBuffer_t PSH_RxStreamBufferStruct;
static StreamBufferHandle_t PSH_RxStreamBuffer;

static uint8_t PSH_TxStreamBufferStorage[PSH_TX_SB_SIZE];
static StaticStreamBuffer_t PSH_TxStreamBufferStruct;
static StreamBufferHandle_t PSH_TxStreamBuffer;

static StaticSemaphore_t PSH_PrintMutexStruct;
static SemaphoreHandle_t PSH_PrintMutex;

/** Input of the running app, written by the shell task, read by the app */
static uint8_t PSH_AppRxStreamBufferStorage[PSH_APP_RX_SB_SIZE];
static StaticStreamBuffer_t PSH_AppRxStreamBufferStruct;
static StreamBufferHandle_t PSH_AppRxStreamBuffer;

/** App task, shared by all apps */
static StaticTask_t PSH_AppTaskBuffer;
static StackType_t PSH_AppStack[PSH_APP_STACK_SIZE];
static TaskHandle_t PSH_AppTaskHandle;
static StaticSemaphore_t PSH_AppStartStruct;
static SemaphoreHandle_t PSH_AppStart;

/** Main function of the running app */
static PSH_Handler_t PSH_AppMain = NULL;

/** An app owns the command line */
static volatile bool PSH_AppRunning = false;

/** Ctrl+C received, cleared when the next app starts */
static volatile bool PSH_AppStop = false;

/** Argument number of the running app, its arguments stay in PSH_Argv and PSH_RxBuffer */
static uint8_t PSH_AppArgc = 0U;

/** Command history */
static char PSH_History[PSH_HISTORY_SIZE][PSH_HISTORY_MAX_LEN];
static uint8_t PSH_HistoryCount = 0;
static int8_t PSH_HistoryIndex = -1;

/** Escape sequence state */
static PSH_EscapeState_e PSH_EscState = PSH_STATE_NORMAL;

/** Command table */
static PSH_CommandEntry_t PSH_CommandTable[PSH_MAX_COMMANDS];
static uint8_t PSH_CommandCount = 0U;

/** Logo printer, called at start-up */
static PSH_LogoPrinter_t PSH_LogoPrinter = NULL;

/** Prompt printer, NULL means the default prompt */
static PSH_PromptPrinter_t PSH_PromptPrinter = NULL;

/**
 * @brief Send the prompt, by the prompt printer if one is set
 */
static void PSH_SendPrompt(void)
{
    if (PSH_PromptPrinter != NULL)
    {
        PSH_PromptPrinter();
    }
    else
    {
        PSH_Send("> ");
    }
}

/**
 * @brief Command argument parser
 * @note Replaces the spaces by \0 and puts the pointers to the argument pointer array
 * @param[in] pLine String to parse
 * @return Number of arguments
 */
static uint8_t PSH_ParseArguments(char *pLine)
{
    uint8_t Argc = 0;
    bool InWord = false;

    while ((*pLine != '\0') && (Argc < PSH_MAX_ARGC))
    {
        if (*pLine == ' ')
        {
            /* Found a space: replace with null terminator */
            *pLine = '\0';
            InWord = false;
        }
        else
        {
            /* Found a character: if it's the start of a word, save the address */
            if (!InWord)
            {
                PSH_Argv[Argc] = pLine;
                Argc++;
                InWord = true;
            }
        }
        pLine++;
    }
    return Argc;
}

/**
 * @brief Clear the command line and send the prompt
 */
static void PSH_FinishCommand(void)
{
    PSH_RxIndex = 0U;
    PSH_CursorPos = 0U;
    PSH_RxBuffer[0] = '\0';
    PSH_SendPrompt();
}

/**
 * @brief Move the terminal cursor to the left
 * @param[in] Count Number of columns, 0 does nothing
 */
static void PSH_CursorLeft(uint16_t Count)
{
    if (Count > 0U)
    {
        PSH_Printf("\x1b[%uD", (unsigned int)Count);
    }
}

/**
 * @brief Handle backspace: deletes the character before the cursor, also in the middle of the line
 */
static void PSH_HandleBackspace(void)
{
    uint16_t Index;

    if (PSH_CursorPos > 0U)
    {
        PSH_CursorPos--;
        PSH_RxIndex--;

        /* Shift the rest of the line left over the deleted character */
        for (Index = PSH_CursorPos; Index < PSH_RxIndex; Index++)
        {
            PSH_RxBuffer[Index] = PSH_RxBuffer[Index + 1U];
        }
        PSH_RxBuffer[PSH_RxIndex] = '\0';

        /* Step back, reprint the rest of the line, blank the last column, then return to the cursor */
        PSH_Send("\b");
        PSH_Write(&PSH_RxBuffer[PSH_CursorPos], PSH_RxIndex - PSH_CursorPos);
        PSH_Send(" ");
        PSH_CursorLeft(PSH_RxIndex - PSH_CursorPos + 1U);
    }
}

/**
 * @brief Arrow left: moves the cursor one character left
 */
static void PSH_HandleArrowLeft(void)
{
    if (PSH_CursorPos > 0U)
    {
        PSH_CursorPos--;
        PSH_CursorLeft(1U);
    }
}

/**
 * @brief Arrow right: moves the cursor one character right, not past the end of the line
 */
static void PSH_HandleArrowRight(void)
{
    if (PSH_CursorPos < PSH_RxIndex)
    {
        PSH_CursorPos++;
        PSH_Send("\x1b[C");
    }
}

/**
 * @brief Replace the command line on the terminal and in the buffer
 * @param[in] pLine New line, an empty string clears the line
 */
static void PSH_ReplaceLine(const char *pLine)
{
    /* Back to the start of the line, then erase to the end of the terminal line */
    PSH_CursorLeft(PSH_CursorPos);
    PSH_Send("\x1b[K");

    STD_Strncpy(PSH_RxBuffer, pLine, sizeof(PSH_RxBuffer));
    PSH_RxIndex = STD_Strlen(PSH_RxBuffer);
    PSH_CursorPos = PSH_RxIndex;
    PSH_Send(PSH_RxBuffer);
}

/**
 * @brief Arrow up handler function, navigates the history
 */
static void PSH_HandleArrowUp(void)
{
    /* Navigate history upward */
    if ((PSH_HistoryCount != 0) && (PSH_HistoryIndex < (int8_t)(PSH_HistoryCount - 1)))
    {
        PSH_HistoryIndex++;
        PSH_ReplaceLine(PSH_History[PSH_HistoryIndex]);
    }
}

/**
 * @brief Arrow down handler function
 */
static void PSH_HandleArrowDown(void)
{
    if (PSH_HistoryIndex > 0)
    {
        PSH_HistoryIndex--;
        PSH_ReplaceLine(PSH_History[PSH_HistoryIndex]);
    }
    else if (PSH_HistoryIndex == 0)
    {
        /* Back to an empty line */
        PSH_HistoryIndex = -1;
        PSH_ReplaceLine("");
    }
}

/**
 * @brief Add command to the history
 * @param[in] pCommand The command string to add to the history
 */
static void PSH_AddToHistory(const char *pCommand)
{
    uint8_t Index;

    if (STD_Strlen(pCommand) != 0)
    {
        /* Shift history down */
        if (PSH_HistoryCount < PSH_HISTORY_SIZE)
        {
            PSH_HistoryCount++;
        }

        for (Index = PSH_HistoryCount - 1; Index > 0; Index--)
        {
            STD_Strncpy(PSH_History[Index], PSH_History[Index - 1], sizeof(PSH_History[Index]));
        }

        /* Add new command */
        STD_Strncpy(PSH_History[0], pCommand, sizeof(PSH_History[0]));
        PSH_HistoryIndex = -1;
    }
}

/**
 * @brief Process escape sequences (arrow keys, etc.)
 * @param[in] ActualByte Received byte
 */
static void PSH_ProcessEscapeSequence(uint8_t ActualByte)
{
    switch (PSH_EscState)
    {
    case PSH_STATE_NORMAL:
        if (ActualByte == PSH_KEY_ESC)
        {
            PSH_EscState = PSH_STATE_ESC;
        }
        break;

    case PSH_STATE_ESC:
        if (ActualByte == '[')
        {
            PSH_EscState = PSH_STATE_CSI;
        }
        else
        {
            PSH_EscState = PSH_STATE_NORMAL;
        }
        break;

    case PSH_STATE_CSI:
        if ((ActualByte >= '0') && (ActualByte <= '9'))
        {
            PSH_EscState = PSH_STATE_CSI_PARAM;
        }
        else if (ActualByte == 'A') /* Arrow up */
        {
            PSH_HandleArrowUp();
            PSH_EscState = PSH_STATE_NORMAL;
        }
        else if (ActualByte == 'B') /* Arrow down */
        {
            PSH_HandleArrowDown();
            PSH_EscState = PSH_STATE_NORMAL;
        }
        else if (ActualByte == 'C') /* Arrow right */
        {
            PSH_HandleArrowRight();
            PSH_EscState = PSH_STATE_NORMAL;
        }
        else if (ActualByte == 'D') /* Arrow left */
        {
            PSH_HandleArrowLeft();
            PSH_EscState = PSH_STATE_NORMAL;
        }
        else
        {
            PSH_EscState = PSH_STATE_NORMAL;
        }
        break;

    case PSH_STATE_CSI_PARAM:
        /* Parameterized sequences are not handled */
        PSH_EscState = PSH_STATE_NORMAL;
        break;
    }
}

/**
 * @brief Look up a command word in the table.
 * @param[in] pCommand Null-terminated command line, the first word is the command
 * @return Matching table entry, or NULL if not found
 */
static const PSH_CommandEntry_t *PSH_FindCommand(const char *pCommand)
{
    uint8_t Index;
    size_t Length;
    const PSH_CommandEntry_t *pResult = NULL;

    for (Index = 0U; Index < PSH_CommandCount; Index++)
    {
        Length = STD_Strlen(PSH_CommandTable[Index].pCommand);

        /* The whole first word has to match, not only its beginning */
        if ((STD_Strncmp(PSH_CommandTable[Index].pCommand, pCommand, Length) == 0)
            && ((pCommand[Length] == '\0') || (pCommand[Length] == ' ')))
        {
            pResult = &PSH_CommandTable[Index];
            break;
        }
    }
    return pResult;
}

/**
 * @brief Handles a newline/carriage return character.
 * @return true if a command line is complete
 */
static bool PSH_HandleNewline(void)
{
    bool Result = false;

    if (PSH_RxIndex > 0)
    {
        PSH_RxBuffer[PSH_RxIndex] = '\0';
        PSH_Send("\r\n");
        PSH_AddToHistory(PSH_RxBuffer);
        Result = true;
    }
    else
    {
        PSH_Send("\r\n");
        PSH_SendPrompt();
    }
    return Result;
}

/**
 * @brief Handles a printable character: inserts it at the cursor, also in the middle of the line
 * @param[in] ActualByte The received byte to handle
 */
static void PSH_HandlePrintableChar(uint8_t ActualByte)
{
    uint16_t Index;

    if (PSH_RxIndex < PSH_CMD_BUF_SIZE - 1)
    {
        /* Shift the rest of the line right to make room */
        for (Index = PSH_RxIndex; Index > PSH_CursorPos; Index--)
        {
            PSH_RxBuffer[Index] = PSH_RxBuffer[Index - 1U];
        }
        PSH_RxBuffer[PSH_CursorPos] = (char)ActualByte;
        PSH_RxIndex++;

        /* Echo from the new character to the end of the line, then return behind the new character */
        PSH_Write(&PSH_RxBuffer[PSH_CursorPos], PSH_RxIndex - PSH_CursorPos);
        PSH_CursorPos++;
        PSH_CursorLeft(PSH_RxIndex - PSH_CursorPos);
    }
}

/**
 * @brief App task: blocked by default, returns to the blocked state when the main function of the app returns
 * @param[in] pParam Unused
 */
static void PSH_AppTask(void *pParam)
{
    (void)pParam;

    for (;;)
    {
        /* A semaphore does not lose a start that arrives before the task is blocked again */
        (void)xSemaphoreTake(PSH_AppStart, portMAX_DELAY);

        PSH_AppMain(PSH_AppArgc, PSH_Argv);

        /* The shell task does not touch the command line until this flag is cleared */
        PSH_FinishCommand();
        PSH_AppRunning = false;
    }
}

/**
 * @brief Create the app task, used at start-up and to replace a killed app
 */
static void PSH_CreateAppTask(void)
{
    PSH_AppTaskHandle = xTaskCreateStatic(PSH_AppTask,
                                          "PSH_App",
                                          PSH_APP_STACK_SIZE,
                                          NULL,
                                          PSH_APP_PRIORITY,
                                          PSH_AppStack,
                                          &PSH_AppTaskBuffer);
}

/**
 * @brief Initialize the shell
 */
static void PSH_Init(void)
{
    /* Initialize RX, TX stream buffers and printf mutex */
    PSH_RxStreamBuffer = xStreamBufferCreateStatic(PSH_RX_SB_SIZE,
                                                   1,
                                                   PSH_RxStreamBufferStorage,
                                                   &PSH_RxStreamBufferStruct);
    PSH_TxStreamBuffer = xStreamBufferCreateStatic(PSH_TX_SB_SIZE,
                                                   1,
                                                   PSH_TxStreamBufferStorage,
                                                   &PSH_TxStreamBufferStruct);

    PSH_PrintMutex = xSemaphoreCreateMutexStatic(&PSH_PrintMutexStruct);
    PSH_AppStart = xSemaphoreCreateBinaryStatic(&PSH_AppStartStruct);

    configASSERT(PSH_RxStreamBuffer != NULL);
    configASSERT(PSH_TxStreamBuffer != NULL);
    configASSERT(PSH_PrintMutex != NULL);
    configASSERT(PSH_AppStart != NULL);

    PSH_CreateAppTask();

    /* Send welcome message */
    PSH_Send("\r\n");
    if (PSH_LogoPrinter != NULL)
    {
        PSH_LogoPrinter();
    }
    PSH_SendPrompt();
}

/*****************************************************************************/
__attribute__((weak)) void PSH_TxStart(void)
{
}

/**
 * @brief  Provide a new char from the HW interface to the shell
 * @param[in] NewChar  The new character received from the HW interface
 * @return true if a complete command line has been received, false otherwise
 */
static bool PSH_ProcessNewChar(char NewChar)
{
    bool Result = false;

    /* Check if it's part of escape sequence */
    if ((PSH_EscState != PSH_STATE_NORMAL) || (NewChar == PSH_KEY_ESC))
    {
        PSH_ProcessEscapeSequence(NewChar);
    }
    else if ((NewChar == '\r') || (NewChar == '\n'))
    {
        Result = PSH_HandleNewline();
    }
    else if ((NewChar == PSH_KEY_BACKSPACE) || (NewChar == PSH_KEY_DEL))
    {
        PSH_HandleBackspace();
    }
    else if (NewChar == PSH_KEY_CTRL_C)
    {
        PSH_Send("^C\r\n");
        PSH_FinishCommand();
    }
    else if ((NewChar >= PSH_KEY_SPACE) && (NewChar < PSH_KEY_DEL))
    {
        PSH_HandlePrintableChar(NewChar);
    }
    return Result;
}

/**
 * @brief Stop the running app: it gets some time to return, then it is killed
 */
static void PSH_StopApp(void)
{
    uint32_t Waited = 0U;
    bool Killed = false;

    PSH_AppStop = true;
    PSH_Send("^C\r\n");

    while (PSH_AppRunning && (Waited < PSH_APP_STOP_TIMEOUT_MS))
    {
        vTaskDelay(pdMS_TO_TICKS(PSH_APP_STOP_POLL_MS));
        Waited += PSH_APP_STOP_POLL_MS;
    }

    /* While the shell task holds the mutex, the app can not hold it, so killing it can not block the output */
    if (xSemaphoreTake(PSH_PrintMutex, portMAX_DELAY) == pdTRUE)
    {
        if (PSH_AppRunning)
        {
            vTaskDelete(PSH_AppTaskHandle);
            PSH_CreateAppTask();
            Killed = true;
        }
        xSemaphoreGive(PSH_PrintMutex);
    }

    if (Killed)
    {
        PSH_Send("Killed\r\n");
        PSH_FinishCommand();
        PSH_AppRunning = false;
    }
}

/**
 * @brief Pass a received byte to the running app
 * @note Never blocks, otherwise a full buffer would hide Ctrl+C. Bytes are dropped if the app does not read them.
 * @param[in] RxByte Received byte
 */
static void PSH_ForwardToApp(uint8_t RxByte)
{
    /* A byte wakes up a blocked read without a race, full buffer means the app is not blocked */
    (void)xStreamBufferSend(PSH_AppRxStreamBuffer, &RxByte, sizeof(RxByte), 0U);

    if (RxByte == PSH_KEY_CTRL_C)
    {
        PSH_StopApp();
    }
}

/**
 * @brief Give the command line to an app and unblock the app task
 * @param[in] Main Main function of the app
 * @param[in] Argc Argument number
 */
static void PSH_StartApp(PSH_Handler_t Main, uint8_t Argc)
{
    /* Created again, as a killed app can leave its blocked read registered in the buffer */
    PSH_AppRxStreamBuffer = xStreamBufferCreateStatic(PSH_APP_RX_SB_SIZE,
                                                      1,
                                                      PSH_AppRxStreamBufferStorage,
                                                      &PSH_AppRxStreamBufferStruct);
    PSH_AppStop = false;
    PSH_AppArgc = Argc;
    PSH_AppMain = Main;
    PSH_AppRunning = true;
    (void)xSemaphoreGive(PSH_AppStart);
}

/**
 * @brief Execute the complete command line
 */
static void PSH_Dispatch(void)
{
    const PSH_CommandEntry_t *pEntry;
    uint8_t Argc;
    bool AppStarted = false;

    pEntry = PSH_FindCommand(PSH_RxBuffer);

    if (pEntry == NULL)
    {
        PSH_Printf("Unknown command: '%s'\r\n", PSH_RxBuffer);
    }
    else
    {
        Argc = PSH_ParseArguments(PSH_RxBuffer);

        if (pEntry->IsApp)
        {
            PSH_StartApp(pEntry->Callback, Argc);
            AppStarted = true;
        }
        else
        {
            pEntry->Callback(Argc, PSH_Argv);
        }
    }

    /* The arguments of a running app stay in the command line, the app finishes the command */
    if (!AppStarted)
    {
        PSH_FinishCommand();
    }
}

/**
 * @brief Add an entry to the command table
 * @param[in] pCommand Command keyword string (null-terminated)
 * @param[in] Callback Handler function pointer
 * @param[in] IsApp Run the handler in the app task
 * @return ERR_OK on success
 */
static ERR_t PSH_AddCommand(const char *pCommand, PSH_Handler_t Callback, bool IsApp)
{
    ERR_t RetValue = ERR_OK;

    if ((pCommand == NULL) || (Callback == NULL))
    {
        RetValue = ERR_INVALID_PARAMETER;
    }
    else if (PSH_CommandCount >= PSH_MAX_COMMANDS)
    {
        RetValue = ERR_FULL;
    }
    else
    {
        PSH_CommandTable[PSH_CommandCount].pCommand = pCommand;
        PSH_CommandTable[PSH_CommandCount].Callback = Callback;
        PSH_CommandTable[PSH_CommandCount].IsApp = IsApp;
        PSH_CommandCount++;
    }
    return RetValue;
}

/*****************************************************************************/
ERR_t PSH_Register(const char *pCommand, PSH_Handler_t Callback)
{
    return PSH_AddCommand(pCommand, Callback, false);
}

/*****************************************************************************/
ERR_t PSH_RegisterApp(const char *pCommand, PSH_Handler_t Main)
{
    return PSH_AddCommand(pCommand, Main, true);
}

/*****************************************************************************/
const PSH_CommandEntry_t *PSH_GetCommand(uint8_t Index)
{
    const PSH_CommandEntry_t *pResult = NULL;

    if (Index < PSH_CommandCount)
    {
        pResult = &PSH_CommandTable[Index];
    }
    return pResult;
}

/*****************************************************************************/
size_t PSH_AppRead(uint8_t *pData, size_t MaxLength, TickType_t Timeout)
{
    return xStreamBufferReceive(PSH_AppRxStreamBuffer, pData, MaxLength, Timeout);
}

/*****************************************************************************/
bool PSH_AppStopRequested(void)
{
    return PSH_AppStop;
}

/*****************************************************************************/
bool PSH_AppDelay(uint32_t Milliseconds)
{
    TickType_t Remaining = pdMS_TO_TICKS(Milliseconds);
    TickType_t Slice;

    while ((Remaining != 0U) && !PSH_AppStop)
    {
        Slice = pdMS_TO_TICKS(PSH_APP_STOP_POLL_MS);
        if (Slice > Remaining)
        {
            Slice = Remaining;
        }
        vTaskDelay(Slice);
        Remaining -= Slice;
    }
    return !PSH_AppStop;
}

/*****************************************************************************/
void PSH_SetLogoPrinter(PSH_LogoPrinter_t Callback)
{
    PSH_LogoPrinter = Callback;
}

/*****************************************************************************/
void PSH_SetPromptPrinter(PSH_PromptPrinter_t Callback)
{
    PSH_PromptPrinter = Callback;
}

/*****************************************************************************/
void PSH_Task(void *pParam)
{
    uint8_t RxByte;
    size_t BytesReceived;

    (void)pParam;

    PSH_Init();

    for (;;)
    {
        BytesReceived = xStreamBufferReceive(
            PSH_RxStreamBuffer,
            &RxByte,
            sizeof(RxByte),
            portMAX_DELAY);

        if (BytesReceived != 0U)
        {
            /* Every byte arrives here first, a running app owns the command line */
            if (PSH_AppRunning)
            {
                PSH_ForwardToApp(RxByte);
            }
            else if (PSH_ProcessNewChar(RxByte))
            {
                PSH_Dispatch();
            }
        }
    }
}

/*****************************************************************************/
void PSH_Printf(const char *pFormat, ...)
{
    char Buffer[PSH_PRINTF_BUF_SIZE];
    va_list Args;
    int Length;

    va_start(Args, pFormat);
    Length = vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
    va_end(Args);

    /* vsnprintf returns the untruncated length */
    if (Length > (int)(sizeof(Buffer) - 1U))
    {
        Length = (int)(sizeof(Buffer) - 1U);
    }

    if (Length > 0)
    {
        PSH_Write(Buffer, (size_t)Length);
    }
}

/*****************************************************************************/
void PSH_Send(const char *pStr)
{
    if (pStr != NULL)
    {
        PSH_Write(pStr, STD_Strlen(pStr));
    }
}

/*****************************************************************************/
void PSH_Write(const void *pData, size_t Length)
{
    const uint8_t *pBytes = (const uint8_t *)pData;
    size_t Sent = 0U;

    if (xSemaphoreTake(PSH_PrintMutex, portMAX_DELAY) == pdTRUE)
    {
        while (Sent < Length)
        {
            Sent += xStreamBufferSend(PSH_TxStreamBuffer, &pBytes[Sent], Length - Sent, portMAX_DELAY);
            PSH_TxStart();
        }
        xSemaphoreGive(PSH_PrintMutex);
    }
}

/*****************************************************************************/
void PSH_RxData(const uint8_t *pData, size_t Length)
{
    xStreamBufferSend(PSH_RxStreamBuffer, pData, Length, PSH_RX_SB_TIMEOUT);
}

/*****************************************************************************/
void PSH_RxDataFromISR(const uint8_t *pData, size_t Length)
{
    BaseType_t HigherPriorityTaskWoken = pdFALSE;

    /* The transport can receive before the shell task created the buffer, that data is dropped */
    if (PSH_RxStreamBuffer != NULL)
    {
        (void)xStreamBufferSendFromISR(PSH_RxStreamBuffer, pData, Length, &HigherPriorityTaskWoken);
        portYIELD_FROM_ISR(HigherPriorityTaskWoken);
    }
}

/*****************************************************************************/
size_t PSH_TxData(uint8_t *pData, size_t MaxLength)
{
    return xStreamBufferReceive(PSH_TxStreamBuffer, pData, MaxLength, 0);
}

/*****************************************************************************/
size_t PSH_TxByteFromISR(uint8_t *pData, size_t MaxLength)
{
    return xStreamBufferReceiveFromISR(PSH_TxStreamBuffer, pData, MaxLength, NULL);
}
