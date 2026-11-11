#include "sandbox.h"
#include "pshell.h"
#include "game2048.h"
#include "globe.h"
#include "menu.h"
#include "pins.h"
#include "pshell_config.h"
#include "raycast.h"
#include "sokoban.h"
#include "task.h"
#include "tui.h"
#include <stdio.h>

#if PSHELL_CMD_STATS
/** Number of tasks the stats command can list */
#define SANDBOX_STATS_MAX_TASKS (8U)

/** Width of the stats frames, the table is 50 columns inside the borders and the margins */
#define SANDBOX_STATS_WIDTH (54U)

/** Longer task names are cut and end with "..." */
#define SANDBOX_STATS_NAME_WIDTH (12U)

/** Text of one stats line */
#define SANDBOX_STATS_LINE_SIZE (64U)
#endif /* PSHELL_CMD_STATS */

#if PSHELL_CMD_CLEAR
/**
 * @brief "clear" command: erases the terminal and moves the cursor to the top left corner
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void SANDBOX_HandleClearCommand(uint8_t Argc, char **ppArgv)
{
    (void)Argc;
    (void)ppArgv;

    TUI_Clear();
}
#endif /* PSHELL_CMD_CLEAR */

#if PSHELL_CMD_HELP
/**
 * @brief "help" command: lists the registered commands and apps
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void SANDBOX_HandleHelpCommand(uint8_t Argc, char **ppArgv)
{
    const PSH_CommandEntry_t *pEntry;
    uint8_t Index = 0U;

    (void)Argc;
    (void)ppArgv;

    PSH_Send("Available commands:\r\n");

    for (pEntry = PSH_GetCommand(Index); pEntry != NULL; pEntry = PSH_GetCommand(Index))
    {
        PSH_Printf("    %-10s %s\r\n", pEntry->pCommand, pEntry->IsApp ? "app" : "command");
        Index++;
    }
}
#endif /* PSHELL_CMD_HELP */

#if PSHELL_CMD_STATS
/**
 * @brief Name of a task state
 * @param[in] State State of the task
 * @return Name, never NULL
 */
static const char *SANDBOX_TaskStateName(eTaskState State)
{
    const char *pName;

    switch (State)
    {
    case eRunning:
        pName = "Running";
        break;
    case eReady:
        pName = "Ready";
        break;
    case eBlocked:
        pName = "Blocked";
        break;
    case eSuspended:
        pName = "Suspended";
        break;
    case eDeleted:
        pName = "Deleted";
        break;
    default:
        pName = "Invalid";
        break;
    }
    return pName;
}

/**
 * @brief Stack size of a task in bytes
 * @note  The recorded end is the top of the stack aligned down to portBYTE_ALIGNMENT, the size is rounded back up
 * @param[in] pTask The task
 * @return Size in bytes
 */
static unsigned long SANDBOX_StackSize(const TaskStatus_t *pTask)
{
    unsigned long Size = (unsigned long)((pTask->pxEndOfStack - pTask->pxStackBase) + 1) * sizeof(StackType_t);

    return (Size + portBYTE_ALIGNMENT_MASK) & ~(unsigned long)portBYTE_ALIGNMENT_MASK;
}

/**
 * @brief Print one line inside a stats frame
 * @param[in] pText Text
 */
static void SANDBOX_StatsRow(const char *pText)
{
    TUI_FrameRow(SANDBOX_STATS_WIDTH, pText);
    PSH_Send("\r\n");
}

/**
 * @brief "stats" command: FreeRTOS run time stats, a system frame and a task table frame
 * @note  The CPU usage is counted since the start
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void SANDBOX_HandleStatsCommand(uint8_t Argc, char **ppArgv)
{
    /* Static, only the shell task runs commands */
    static TaskStatus_t Tasks[SANDBOX_STATS_MAX_TASKS];
    char Line[SANDBOX_STATS_LINE_SIZE];
    char Name[SANDBOX_STATS_NAME_WIDTH + 1U];
    configRUN_TIME_COUNTER_TYPE TotalRunTime = 0U;
    TickType_t Ticks = xTaskGetTickCount();
    UBaseType_t Count;
    UBaseType_t Index;
    uint32_t Permille;
    uint32_t Divisor;

    (void)Argc;
    (void)ppArgv;

    Count = uxTaskGetSystemState(Tasks, SANDBOX_STATS_MAX_TASKS, &TotalRunTime);

    /* System frame */
    TUI_FrameTop(SANDBOX_STATS_WIDTH, "System");
    PSH_Send("\r\n");
    (void)snprintf(Line, sizeof(Line), "Uptime: %lu.%02lu s",
                   (unsigned long)(Ticks / configTICK_RATE_HZ),
                   (unsigned long)(((Ticks % configTICK_RATE_HZ) * 100U) / configTICK_RATE_HZ));
    SANDBOX_StatsRow(Line);
    (void)snprintf(Line, sizeof(Line), "Heap:   %lu of %lu bytes free",
                   (unsigned long)xPortGetFreeHeapSize(),
                   (unsigned long)configTOTAL_HEAP_SIZE);
    SANDBOX_StatsRow(Line);
    (void)snprintf(Line, sizeof(Line), "Tasks:  %lu", (unsigned long)uxTaskGetNumberOfTasks());
    SANDBOX_StatsRow(Line);
    TUI_FrameBottom(SANDBOX_STATS_WIDTH);
    PSH_Send("\r\n");

    /* Task table frame */
    TUI_FrameTop(SANDBOX_STATS_WIDTH, "Tasks");
    PSH_Send("\r\n");

    if (Count == 0U)
    {
        SANDBOX_StatsRow("Too many tasks, increase SANDBOX_STATS_MAX_TASKS");
    }
    else
    {
        (void)snprintf(Line, sizeof(Line), "%-12s %-9s %4s %6s %6s %8s", "Task", "State", "Prio", "Stack", "Free", "CPU");
        SANDBOX_StatsRow(Line);
        TUI_FrameSeparator(SANDBOX_STATS_WIDTH);
        PSH_Send("\r\n");
    }

    /* Divide the total instead of multiplying the counters, like FreeRTOS does: no 64 bit division (__udivmoddi4).
       Exact enough after the first seconds, the divisor grows by 10 per second. */
    Divisor = (uint32_t)(TotalRunTime / 1000U);

    for (Index = 0U; Index < Count; Index++)
    {
        Permille = 0U;
        if (Divisor != 0U)
        {
            Permille = (uint32_t)(Tasks[Index].ulRunTimeCounter / Divisor);
        }
        if (Permille > 1000U)
        {
            Permille = 1000U;
        }

        TUI_Shorten(Name, Tasks[Index].pcTaskName, SANDBOX_STATS_NAME_WIDTH);
        (void)snprintf(Line, sizeof(Line), "%-12s %-9s %4lu %6lu %6lu %5lu.%lu%%",
                       Name,
                       SANDBOX_TaskStateName(Tasks[Index].eCurrentState),
                       (unsigned long)Tasks[Index].uxCurrentPriority,
                       SANDBOX_StackSize(&Tasks[Index]),
                       (unsigned long)(Tasks[Index].usStackHighWaterMark * sizeof(StackType_t)),
                       (unsigned long)(Permille / 10U),
                       (unsigned long)(Permille % 10U));
        SANDBOX_StatsRow(Line);
    }

    TUI_FrameBottom(SANDBOX_STATS_WIDTH);
    PSH_Send("\r\n");
}
#endif /* PSHELL_CMD_STATS */

#if PSHELL_APP_COUNT
/**
 * @brief App: prints its arguments, then counts until Ctrl+C
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void SANDBOX_CountMain(uint8_t Argc, char **ppArgv)
{
    uint8_t Index;
    uint32_t Count = 0U;

    for (Index = 0U; Index < Argc; Index++)
    {
        PSH_Printf("Argument %u: %s\r\n", (unsigned int)Index, ppArgv[Index]);
    }

    while (PSH_AppDelay(1000U))
    {
        PSH_Printf("%lu\r\n", (unsigned long)Count);
        Count++;
    }
}
#endif /* PSHELL_APP_COUNT */

#if PSHELL_APP_2048
/**
 * @brief App: the 2048 game until q or Ctrl+C
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void SANDBOX_Game2048Main(uint8_t Argc, char **ppArgv)
{
    (void)Argc;
    (void)ppArgv;

    /* The time the user started the game makes every game different */
    G2048_Run((uint32_t)xTaskGetTickCount());
}
#endif /* PSHELL_APP_2048 */

#if PSHELL_APP_SOKOBAN
/**
 * @brief App: the Sokoban game until q or Ctrl+C
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void SANDBOX_SokobanMain(uint8_t Argc, char **ppArgv)
{
    (void)Argc;
    (void)ppArgv;

    SOKO_Run();
}
#endif /* PSHELL_APP_SOKOBAN */

/*****************************************************************************/
void SANDBOX_Init(void)
{
#if PSHELL_CMD_HELP
    (void)PSH_Register("help", SANDBOX_HandleHelpCommand);
#endif
#if PSHELL_CMD_CLEAR
    (void)PSH_Register("clear", SANDBOX_HandleClearCommand);
#endif
#if PSHELL_CMD_STATS
    (void)PSH_Register("stats", SANDBOX_HandleStatsCommand);
#endif
#if PSHELL_APP_COUNT
    (void)PSH_RegisterApp("count", SANDBOX_CountMain);
#endif
#if PSHELL_APP_2048
    (void)PSH_RegisterApp("2048", SANDBOX_Game2048Main);
#endif
#if PSHELL_APP_SOKOBAN
    (void)PSH_RegisterApp("sokoban", SANDBOX_SokobanMain);
#endif
#if PSHELL_APP_GLOBE
    (void)PSH_RegisterApp("globe", GLOBE_Main);
#endif
#if PSHELL_APP_MENU
    (void)PSH_RegisterApp("menu", MENU_Main);
#endif
#if PSHELL_APP_GPIO
    (void)PSH_RegisterApp("gpio", PINS_Main);
#endif
#if PSHELL_APP_RAYCAST
    (void)PSH_RegisterApp("raycast", RAY_Main);
#endif
}
