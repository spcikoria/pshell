/**
 * @file    main.c
 * @brief   Application entry: board start-up, shell console and task creation. Portable, the hardware is in bsp/.
 */
#include "pshell.h"
#include "FreeRTOS.h"
#include "bsp.h"
#include "bsp_console.h"
#include "fs_commands.h"
#include "pshell_config.h"
#include "sandbox.h"
#include "task.h"
#include "vfs_fat.h"

/** Baud rate of the console, on the Nucleo-H503RB BRR = 250 MHz / 921600 = 271 (0.03 % error) */
#define MAIN_PSH_BAUD_RATE (921600U)

/** Stack of the shell task in words, PSH_Printf() needs about 512 */
#define MAIN_PSH_TASK_STACK_SIZE (512U)

/** Above the app task and the timer task, so a busy app can not stop the input and Ctrl+C */
#define MAIN_PSH_TASK_PRIORITY (tskIDLE_PRIORITY + 3U)

static StaticTask_t MAIN_PshTaskBuffer;
static StackType_t MAIN_PshTaskStack[MAIN_PSH_TASK_STACK_SIZE];

/** Color of the logo blocks (light blue), the outlines, lines and text keep the default color */
#define MAIN_LOGO_BLOCK_COLOR "\x1b[94m"
#define MAIN_LOGO_TEXT_COLOR  "\x1b[0m"

/**
 * @brief Print one line of the logo art, the full blocks in the block color, the rest in the text color
 * @param[in] pLine UTF-8 line
 */
static void MAIN_PrintLogoArt(const char *pLine)
{
    const char *pRun = pLine;
    bool InBlock = false;
    bool IsBlock;

    while (*pLine != '\0')
    {
        /* U+2588 full block */
        IsBlock = (pLine[0] == '\xE2') && (pLine[1] == '\x96') && (pLine[2] == '\x88');

        /* Spaces look the same in any color, they don't need a switch */
        if ((*pLine != ' ') && (IsBlock != InBlock))
        {
            PSH_Write(pRun, (size_t)(pLine - pRun));
            PSH_Send(IsBlock ? MAIN_LOGO_BLOCK_COLOR : MAIN_LOGO_TEXT_COLOR);
            InBlock = IsBlock;
            pRun = pLine;
        }
        pLine += IsBlock ? 3 : 1;
    }
    PSH_Write(pRun, (size_t)(pLine - pRun));
    PSH_Send(MAIN_LOGO_TEXT_COLOR);
}

/**
 * @brief Logo printer of the shell, printed at start-up and by the "logo" command
 */
static void MAIN_PrintLogo(void)
{
    PSH_Send("\r\n\r\n");
    MAIN_PrintLogoArt(" ██████╗  ███████╗ ██╗  ██╗ ███████╗ ██╗      ██╗\r\n");
    MAIN_PrintLogoArt(" ██╔══██╗ ██╔════╝ ██║  ██║ ██╔════╝ ██║      ██║\r\n");
    MAIN_PrintLogoArt(" ██████╔╝ ███████╗ ███████║ █████╗   ██║      ██║\r\n");
    MAIN_PrintLogoArt(" ██╔═══╝  ╚════██║ ██╔══██║ ██╔══╝   ██║      ██║\r\n");
    MAIN_PrintLogoArt(" ██║      ███████║ ██║  ██║ ███████╗ ███████╗ ███████╗\r\n");
    MAIN_PrintLogoArt(" ╚═╝      ╚══════╝ ╚═╝  ╚═╝ ╚══════╝ ╚══════╝ ╚══════╝\r\n");
    PSH_Send(" ─────────────────────────────────────────────────────\r\n");
    PSH_Printf("    Welcome to PSHELL on %s @ %lu MHz\r\n", BSP_pBoardName,
               (unsigned long)(SystemCoreClock / 1000000U));
    PSH_Send(" ─────────────────────────────────────────────────────\r\n");
}

#if PSHELL_CMD_LOGO
/**
 * @brief Shell "logo" command handler
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void MAIN_HandleLogoCommand(uint8_t Argc, char **ppArgv)
{
    (void)Argc;
    (void)ppArgv;

    MAIN_PrintLogo();
}
#endif /* PSHELL_CMD_LOGO */

/**
 * @brief FreeRTOS stack overflow hook, stops the system
 * @param[in] Task Unused
 * @param[in] pTaskName Unused
 */
void vApplicationStackOverflowHook(TaskHandle_t Task, char *pTaskName)
{
    (void)Task;
    (void)pTaskName;

    BSP_Halt();
}

/**
 * @brief Application entry point
 */
int main(void)
{
    /* Clocks, caches and the interrupt priority grouping of the board */
    BSP_Init();

    /* SD card file system, the card is accessed only by the first file operation */
    VFAT_Init();

    /* Shell logo, commands and apps, set before the shell task starts */
    PSH_SetLogoPrinter(MAIN_PrintLogo);
#if PSHELL_CMD_LOGO
    (void)PSH_Register("logo", MAIN_HandleLogoCommand);
#endif
    SANDBOX_Init();
    FSCMD_Init();

    /* Shell task */
    (void)xTaskCreateStatic(PSH_Task,
                            "PSH_Task",
                            MAIN_PSH_TASK_STACK_SIZE,
                            NULL,
                            MAIN_PSH_TASK_PRIORITY,
                            MAIN_PshTaskStack,
                            &MAIN_PshTaskBuffer);

    /* Creating the task left the interrupts masked, they stay masked until the scheduler starts */
    BSP_CON_Init(MAIN_PSH_BAUD_RATE);

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();

    /* Only reached if the scheduler could not start */
    BSP_Halt();

    return 0;
}
