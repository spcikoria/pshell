/**
 * @file    pshell.h
 * @brief   Shell command line interface, transport independent (UART, USB CDC, ...).
 *
 * Two types of commands:
 * - Command: runs in the shell task and has to return quickly (e.g. print a status)
 * - App: runs in the app task, which is blocked until the shell starts an app. It owns the command line
 *   until it returns. Only one app runs at a time, so all of them share one task and one stack.
 */
#pragma once

#include "error.h"
#include "FreeRTOS.h"
#include <stdbool.h>

/** Maximum number of commands and apps that can be registered. */
#define PSH_MAX_COMMANDS    (24U)
#define PSH_MAX_ARGC        (8U)
#define PSH_HISTORY_SIZE    (10U)
#define PSH_CMD_BUF_SIZE    (50U)
#define PSH_HISTORY_MAX_LEN (PSH_CMD_BUF_SIZE)
#define PSH_RX_SB_SIZE      (100U)
#define PSH_TX_SB_SIZE      (100U)
#define PSH_APP_RX_SB_SIZE  (100U)
#define PSH_PRINTF_BUF_SIZE (128U)

#define PSH_RX_SB_TIMEOUT   (100U)

/** Below the shell task, so an app that never blocks can not stop the input, the output and Ctrl+C */
#define PSH_APP_PRIORITY    (tskIDLE_PRIORITY + 1U)

/** Stack of the app task in words, has to fit the largest app. PSH_Printf() needs about 512. */
#define PSH_APP_STACK_SIZE  (512U)

/** Reaction time of PSH_AppDelay() to Ctrl+C */
#define PSH_APP_STOP_POLL_MS (10U)

/** Time an app has to return after Ctrl+C, then it is killed */
#define PSH_APP_STOP_TIMEOUT_MS (200U)

/** CLI key definitions */
#define PSH_KEY_BACKSPACE   (0x08)
#define PSH_KEY_DEL         (0x7F)
#define PSH_KEY_CTRL_C      (0x03)
#define PSH_KEY_ESC         (0x1B)
#define PSH_KEY_SPACE       (0x20)

/** Escape sequence state machine */
typedef enum
{
    PSH_STATE_NORMAL,
    PSH_STATE_ESC,
    PSH_STATE_CSI,
    PSH_STATE_CSI_PARAM,
} PSH_EscapeState_e;

/**
 * @brief  Prototype for a command handler and for the main function of an app.
 * @param[in] Argc  Number of the command arguments
 * @param[in] ppArgv  Pointer to the char* array that stores the arguments
 *                     ppArgv[0] points to the command string.
 */
typedef void (*PSH_Handler_t)(uint8_t Argc, char **ppArgv);

/**
 * @brief  Prototype for the logo printer, called in the shell task context, so PSH_Printf() can be used.
 */
typedef void (*PSH_LogoPrinter_t)(void);

/**
 * @brief  Prototype for the prompt printer, it prints the whole prompt, e.g. "/usr/bin> ".
 * @note   Called in the shell task, or in the app task when an app returns, so PSH_Printf() can be used.
 */
typedef void (*PSH_PromptPrinter_t)(void);

/** One entry in the command dispatch table. */
typedef struct
{
    /* Command keyword */
    const char *pCommand;
    /* Handler function */
    PSH_Handler_t Callback;
    /* Run the handler in the app task, not in the shell task */
    bool IsApp;
} PSH_CommandEntry_t;

/**
 * @brief Callback when new data is put into the TX buffer of the shell, override it in the transport!
 * @note Weak only in pshell.c, a weak declaration here would make the override weak as well
 */
void PSH_TxStart(void);

/**
 * @brief  Register a command and its handler in the command table.
 * @param[in]  pCommand  Command keyword string (null-terminated).
 * @param[in]   Callback  Handler function pointer.
 * @return      ERR_OK on success.
 */
ERR_t PSH_Register(const char *pCommand, PSH_Handler_t Callback);

/**
 * @brief  Register an app, it exits by returning from its main function.
 * @note   After Ctrl+C it has PSH_APP_STOP_TIMEOUT_MS to return, then it is killed.
 * @param[in]  pCommand  Command keyword string (null-terminated).
 * @param[in]   Main  Main function of the app
 * @return      ERR_OK on success.
 */
ERR_t PSH_RegisterApp(const char *pCommand, PSH_Handler_t Main);

/**
 * @brief  Get a registered command or app, to list them outside the shell (e.g. in /usr/bin)
 * @param[in]  Index  Index of the entry, from 0
 * @return      The entry, NULL past the last one
 */
const PSH_CommandEntry_t *PSH_GetCommand(uint8_t Index);

/**
 * @brief  Read the raw bytes the shell forwarded to the running app, app context only
 * @note   Nothing is echoed or edited. Ctrl+C arrives as a 0x03 byte, so a blocking read returns on it.
 * @param[out] pData  Buffer of the received bytes
 * @param[in]  MaxLength  Size of the buffer
 * @param[in]  Timeout  Maximum time to wait in ticks
 * @return      Number of bytes received, 0 on timeout
 */
size_t PSH_AppRead(uint8_t *pData, size_t MaxLength, TickType_t Timeout);

/**
 * @brief  Check if the user pressed Ctrl+C, the running app has to check it and return from its main
 * @return true if the app has to stop
 */
bool PSH_AppStopRequested(void);

/**
 * @brief  Delay of an app, returns early on Ctrl+C
 * @param[in]  Milliseconds  Length of the delay
 * @return true if the app can continue, false if it has to stop
 */
bool PSH_AppDelay(uint32_t Milliseconds);

/**
 * @brief  Set the logo printer, printed at the shell start-up. Call it before the shell task starts.
 * @param[in]  Callback  Logo printer, NULL disables the logo
 */
void PSH_SetLogoPrinter(PSH_LogoPrinter_t Callback);

/**
 * @brief  Set the prompt printer. Call it before the shell task starts, or the first prompt is the default one.
 * @param[in]  Callback  Prompt printer, NULL restores the default "> " prompt
 */
void PSH_SetPromptPrinter(PSH_PromptPrinter_t Callback);

/**
 * @brief  Shell task function
 * @param pParam  Pointer to task parameters
 */
void PSH_Task(void *pParam);

/**
 * @brief  Formatted print to the shell output stream buffer, task context only
 * @param[in]  pFormat  Standard printf format string
 * @param[in]  ...      Variable arguments for formatting
 */
void PSH_Printf(const char *pFormat, ...);

/**
 * @brief Send string to the shell output stream buffer
 * @param[in] pStr Pointer to a C string to send
 */
void PSH_Send(const char *pStr);

/**
 * @brief Send raw bytes to the shell output stream buffer, task context only
 * @note Data longer than the stream buffer is sent in chunks
 * @param[in] pData Data to send
 * @param[in] Length Length of the data
 */
void PSH_Write(const void *pData, size_t Length);

/**
 * @brief Pass the received data to the shell input stream buffer
 * @param[in] pData Pointer to the buffer to store received data
 * @param[in] Length Length of the data to receive
 */
void PSH_RxData(const uint8_t *pData, size_t Length);

/**
 * @brief Pass the received data to the shell input stream buffer from an ISR context
 * @param[in] pData Pointer to the buffer to store received data
 * @param[in] Length Length of the data to receive
 */
void PSH_RxDataFromISR(const uint8_t *pData, size_t Length);

/**
 * @brief Get data to transmit from the shell output stream buffer
 * @param[out] pData Pointer to the buffer to write the data to transmit
 * @param[in] MaxLength Length of the data to transmit
 * @return Number of bytes actually transmitted
 */
size_t PSH_TxData(uint8_t *pData, size_t MaxLength);

/**
 * @brief Get data to transmit from the shell output stream buffer from an ISR context
 * @param[out] pData Pointer to the buffer to write the data to transmit
 * @param[in] MaxLength Length of the data to transmit
 * @return Number of bytes actually transmitted
 */
size_t PSH_TxByteFromISR(uint8_t *pData, size_t MaxLength);
