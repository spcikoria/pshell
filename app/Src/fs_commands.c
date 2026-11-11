#include "fs_commands.h"
#include "pshell.h"
#include "fs_tree.h"
#include "pshell_config.h"
#include "standard.h"
#include "vfs.h"
#include <errno.h>

/** Any command that takes a path */
#define FSCMD_USES_PATH                                                                                            \
    (PSHELL_CMD_LS || PSHELL_CMD_CAT || PSHELL_CMD_GREP || PSHELL_CMD_ECHO || PSHELL_CMD_CD || PSHELL_CMD_RM \
     || PSHELL_CMD_MKDIR || PSHELL_CMD_TOUCH)

/** ls colors, like the default LS_COLORS of Linux */
#define FSCMD_COLOR_DIR    "\x1b[1;34m"
#define FSCMD_COLOR_DEVICE "\x1b[1;33m"
#define FSCMD_COLOR_EXEC   "\x1b[1;32m"
#define FSCMD_COLOR_RESET  "\x1b[0m"

/**
 * Prompt colors. The host is sky blue from the 256 color palette: a terminal that shows bold as bright would make
 * any 16 color green look like the executables, and a 16 color blue like the folder.
 */
#define FSCMD_COLOR_HOST   "\x1b[38;5;117m"
#define FSCMD_COLOR_CWD    "\x1b[1;34m"

/** Read chunk of cat and grep */
#define FSCMD_CAT_BUF_SIZE (32U)

/** Longest line grep checks, a longer line is cut */
#define FSCMD_GREP_LINE_SIZE (128U)

/**
 * Current folder of the shell, canonical. Only cd changes it, in the shell task. An app (cat) reads it, but
 * while an app runs the shell runs no command, so no lock is needed.
 */
static char FSCMD_Cwd[VFS_PATH_MAX] = "/";

#if FSCMD_USES_PATH
/**
 * @brief Make a path absolute: a relative path starts from the current folder
 * @param[in] pPath Absolute or relative path
 * @param[out] pOut Canonical absolute path, VFS_PATH_MAX long
 * @return 0 or a negative errno value of VFS_JoinPath()
 */
static int32_t FSCMD_ResolvePath(const char *pPath, char *pOut)
{
    return VFS_JoinPath(FSCMD_Cwd, pPath, pOut, VFS_PATH_MAX);
}
#endif /* FSCMD_USES_PATH */

#if PSHELL_CMD_LS
/**
 * @brief Print one line of ls
 * @param[in] pInfo The node
 * @param[in] pName Printed name
 * @param[in] Long Print the type, the mode and the size too, like ls -l
 */
static void FSCMD_PrintEntry(const VFS_NodeInfo_t *pInfo, const char *pName, bool Long)
{
    const char *pColor = "";
    const char *pReset = "";
    char Type = '-';

    if (pInfo->Type == VFS_TYPE_DIR)
    {
        Type = 'd';
        pColor = FSCMD_COLOR_DIR;
    }
    else if (pInfo->Type == VFS_TYPE_DEVICE)
    {
        Type = 'c';
        pColor = FSCMD_COLOR_DEVICE;
    }
    else if ((pInfo->Mode & VFS_MODE_X) != 0U)
    {
        pColor = FSCMD_COLOR_EXEC;
    }

    if (pColor[0] != '\0')
    {
        pReset = FSCMD_COLOR_RESET;
    }

    if (Long)
    {
        PSH_Printf("%c%c%c%c %8lu  ",
                   Type,
                   ((pInfo->Mode & VFS_MODE_R) != 0U) ? 'r' : '-',
                   ((pInfo->Mode & VFS_MODE_W) != 0U) ? 'w' : '-',
                   ((pInfo->Mode & VFS_MODE_X) != 0U) ? 'x' : '-',
                   (unsigned long)pInfo->Size);
    }
    PSH_Printf("%s%s%s\r\n", pColor, pName, pReset);
}

/**
 * @brief "ls [-l] [path]" command: lists a folder, or prints a file as it was given
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleLs(uint8_t Argc, char **ppArgv)
{
    const char *pPath = ".";
    char Path[VFS_PATH_MAX];
    VFS_NodeInfo_t Info;
    VFS_Dir_t Dir;
    bool Long = false;
    int32_t Result;
    uint8_t Index;

    for (Index = 1U; Index < Argc; Index++)
    {
        /* The terminator is compared too, so only "-l" matches */
        if (STD_Strncmp(ppArgv[Index], "-l", 3U) == 0)
        {
            Long = true;
        }
        else
        {
            pPath = ppArgv[Index];
        }
    }

    Result = FSCMD_ResolvePath(pPath, Path);
    if (Result == 0)
    {
        Result = VFS_Stat(Path, &Info);
    }

    if ((Result == 0) && (Info.Type != VFS_TYPE_DIR))
    {
        FSCMD_PrintEntry(&Info, pPath, Long);
    }
    else if (Result == 0)
    {
        /* A mounted folder can fail here, e.g. without a card */
        Result = VFS_OpenDir(Path, &Dir);
        if (Result == 0)
        {
            while (VFS_ReadDir(&Dir, &Info))
            {
                FSCMD_PrintEntry(&Info, Info.pName, Long);
            }
            VFS_CloseDir(&Dir);
        }
    }
    else
    {
        /* The error is printed below */
    }

    if (Result != 0)
    {
        PSH_Printf("ls: %s: %s\r\n", pPath, VFS_StrError(Result));
    }
}
#endif /* PSHELL_CMD_LS */

#if PSHELL_CMD_CAT
/**
 * @brief Send file content to the terminal, a line end becomes CR LF (like onlcr of a Linux tty)
 * @param[in] pData Data
 * @param[in] Length Length of the data
 */
static void FSCMD_WriteTerminal(const char *pData, size_t Length)
{
    size_t Start = 0U;
    size_t Index;

    for (Index = 0U; Index < Length; Index++)
    {
        if (pData[Index] == '\n')
        {
            PSH_Write(&pData[Start], Index - Start);
            PSH_Send("\r\n");
            Start = Index + 1U;
        }
    }
    PSH_Write(&pData[Start], Length - Start);
}

/**
 * @brief Print a whole file, stops on Ctrl+C
 * @param[in] pPath Path of the file
 */
static void FSCMD_CatFile(const char *pPath)
{
    char Buffer[FSCMD_CAT_BUF_SIZE];
    char Path[VFS_PATH_MAX];
    int32_t Fd;
    int32_t Length;

    Fd = FSCMD_ResolvePath(pPath, Path);
    if (Fd == 0)
    {
        Fd = VFS_Open(Path, O_RDONLY);
    }

    if (Fd < 0)
    {
        PSH_Printf("cat: %s: %s\r\n", pPath, VFS_StrError(Fd));
    }
    else
    {
        do
        {
            Length = VFS_Read(Fd, Buffer, sizeof(Buffer));
            if (Length > 0)
            {
                FSCMD_WriteTerminal(Buffer, (size_t)Length);
            }
        } while ((Length > 0) && !PSH_AppStopRequested());

        if (Length < 0)
        {
            PSH_Printf("cat: %s: %s\r\n", pPath, VFS_StrError(Length));
        }
        (void)VFS_Close(Fd);
    }
}

/**
 * @brief "cat <file>..." app: prints files. An app, so a file without end (a device) can be stopped by Ctrl+C.
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_CatMain(uint8_t Argc, char **ppArgv)
{
    uint8_t Index;

    if (Argc < 2U)
    {
        PSH_Send("Usage: cat <file>...\r\n");
    }

    for (Index = 1U; (Index < Argc) && !PSH_AppStopRequested(); Index++)
    {
        FSCMD_CatFile(ppArgv[Index]);
    }
}
#endif /* PSHELL_CMD_CAT */

#if PSHELL_CMD_GREP
/**
 * @brief Print a line if it contains the pattern
 * @param[in] pLine Line without its line end, not terminated
 * @param[in] Length Length of the line
 * @param[in] pPattern Searched text
 * @param[in] pName File name printed before the line, or NULL
 */
static void FSCMD_GrepLine(const char *pLine, size_t Length, const char *pPattern, const char *pName)
{
    size_t PatternLength = STD_Strlen(pPattern);
    size_t Index;
    bool Match = false;

    for (Index = 0U; ((Index + PatternLength) <= Length) && !Match; Index++)
    {
        Match = (STD_Strncmp(&pLine[Index], pPattern, PatternLength) == 0);
    }

    if (Match)
    {
        if (pName != NULL)
        {
            PSH_Printf("%s:", pName);
        }
        PSH_Write(pLine, Length);
        PSH_Send("\r\n");
    }
}

/**
 * @brief Print the lines of a file that contain the pattern, stops on Ctrl+C
 * @param[in] pPattern Searched text
 * @param[in] pPath Path of the file
 * @param[in] ShowName Print the file name before the lines
 */
static void FSCMD_GrepFile(const char *pPattern, const char *pPath, bool ShowName)
{
    char Buffer[FSCMD_CAT_BUF_SIZE];
    char Line[FSCMD_GREP_LINE_SIZE];
    char Path[VFS_PATH_MAX];
    const char *pName = ShowName ? pPath : NULL;
    size_t LineLength = 0U;
    int32_t Fd;
    int32_t Length;
    int32_t Index;

    Fd = FSCMD_ResolvePath(pPath, Path);
    if (Fd == 0)
    {
        Fd = VFS_Open(Path, O_RDONLY);
    }

    if (Fd < 0)
    {
        PSH_Printf("grep: %s: %s\r\n", pPath, VFS_StrError(Fd));
    }
    else
    {
        do
        {
            Length = VFS_Read(Fd, Buffer, sizeof(Buffer));
            for (Index = 0; Index < Length; Index++)
            {
                if (Buffer[Index] == '\n')
                {
                    FSCMD_GrepLine(Line, LineLength, pPattern, pName);
                    LineLength = 0U;
                }
                else if (LineLength < sizeof(Line))
                {
                    Line[LineLength] = Buffer[Index];
                    LineLength++;
                }
                else
                {
                    /* Too long line, the rest is dropped */
                }
            }
        } while ((Length > 0) && !PSH_AppStopRequested());

        /* Last line without a line end */
        if (LineLength > 0U)
        {
            FSCMD_GrepLine(Line, LineLength, pPattern, pName);
        }

        if (Length < 0)
        {
            PSH_Printf("grep: %s: %s\r\n", pPath, VFS_StrError(Length));
        }
        (void)VFS_Close(Fd);
    }
}

/**
 * @brief "grep <pattern> <file>..." app: prints the lines that contain the pattern, plain text match
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_GrepMain(uint8_t Argc, char **ppArgv)
{
    uint8_t Index;

    if (Argc < 3U)
    {
        PSH_Send("Usage: grep <pattern> <file>...\r\n");
    }

    for (Index = 2U; (Index < Argc) && !PSH_AppStopRequested(); Index++)
    {
        FSCMD_GrepFile(ppArgv[1], ppArgv[Index], Argc > 3U);
    }
}
#endif /* PSHELL_CMD_GREP */

#if PSHELL_CMD_ECHO
/**
 * @brief Write a line to a file, in one write
 * @param[in] pPath Path of the file, absolute or relative
 * @param[in] pLine Line with its line end
 * @param[in] Length Length of the line
 * @param[in] Append true for >>, false for >
 */
static void FSCMD_WriteFile(const char *pPath, const char *pLine, size_t Length, bool Append)
{
    char Path[VFS_PATH_MAX];
    int32_t Fd;
    int32_t Result;
    int32_t CloseResult;

    Fd = FSCMD_ResolvePath(pPath, Path);
    if (Fd == 0)
    {
        /* The VFS files are handlers, they decide what truncate and append mean */
        Fd = VFS_Open(Path, O_WRONLY | O_CREAT | (Append ? O_APPEND : O_TRUNC));
    }

    if (Fd < 0)
    {
        Result = Fd;
    }
    else
    {
        Result = VFS_Write(Fd, pLine, Length);

        /* A file system writes its buffers at the close, it can fail there */
        CloseResult = VFS_Close(Fd);
        if (Result >= 0)
        {
            Result = CloseResult;
        }
    }

    if (Result < 0)
    {
        PSH_Printf("echo: %s: %s\r\n", pPath, VFS_StrError(Result));
    }
}

/**
 * @brief "echo [text...] [> file | >> file]" command: prints the text, or writes it with a line end to a file
 * @note  The redirection can stand anywhere, with or without a space: "> file", ">file", ">> file", ">>file"
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleEcho(uint8_t Argc, char **ppArgv)
{
    /* The words come from the command line, joined with the line end they can not be longer */
    char Line[PSH_CMD_BUF_SIZE + 1U];
    const char *pPath = NULL;
    bool Append = false;
    bool Valid = true;
    size_t Length = 0U;
    size_t WordLength;
    uint8_t Index;

    for (Index = 1U; Index < Argc; Index++)
    {
        if (ppArgv[Index][0] == '>')
        {
            Append = (ppArgv[Index][1] == '>');
            pPath = &ppArgv[Index][Append ? 2U : 1U];

            /* "> file": the file name is the next word */
            if ((*pPath == '\0') && ((Index + 1U) < Argc))
            {
                Index++;
                pPath = ppArgv[Index];
            }
            if (*pPath == '\0')
            {
                Valid = false;
            }
        }
        else
        {
            /* The parser split the text at the spaces, the words are joined with single spaces again */
            if (Length > 0U)
            {
                Line[Length] = ' ';
                Length++;
            }
            WordLength = STD_Strlen(ppArgv[Index]);
            STD_Memcpy(&Line[Length], ppArgv[Index], WordLength);
            Length += WordLength;
        }
    }

    if (!Valid)
    {
        PSH_Send("echo: missing file name after >\r\n");
    }
    else if (pPath != NULL)
    {
        Line[Length] = '\n';
        FSCMD_WriteFile(pPath, Line, Length + 1U, Append);
    }
    else
    {
        PSH_Write(Line, Length);
        PSH_Send("\r\n");
    }
}
#endif /* PSHELL_CMD_ECHO */

#if PSHELL_CMD_CD
/**
 * @brief "cd [folder]" command: changes the current folder, without an argument back to the root
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleCd(uint8_t Argc, char **ppArgv)
{
    const char *pPath = "/";
    char Path[VFS_PATH_MAX];
    VFS_NodeInfo_t Info;
    int32_t Result;

    if (Argc > 1U)
    {
        pPath = ppArgv[1];
    }

    Result = FSCMD_ResolvePath(pPath, Path);
    if (Result == 0)
    {
        Result = VFS_Stat(Path, &Info);
    }
    if ((Result == 0) && (Info.Type != VFS_TYPE_DIR))
    {
        Result = -ENOTDIR;
    }

    if (Result == 0)
    {
        STD_Strncpy(FSCMD_Cwd, Path, sizeof(FSCMD_Cwd));
    }
    else
    {
        PSH_Printf("cd: %s: %s\r\n", pPath, VFS_StrError(Result));
    }
}
#endif /* PSHELL_CMD_CD */

#if PSHELL_CMD_PWD
/**
 * @brief "pwd" command: prints the current folder
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
static void FSCMD_HandlePwd(uint8_t Argc, char **ppArgv)
{
    (void)Argc;
    (void)ppArgv;

    PSH_Printf("%s\r\n", FSCMD_Cwd);
}
#endif /* PSHELL_CMD_PWD */

#if PSHELL_CMD_RM
/**
 * @brief Remove one file, or an empty folder if allowed
 * @param[in] pPath Path as given
 * @param[in] RemoveDirs Empty folders can be removed too
 */
static void FSCMD_RemovePath(const char *pPath, bool RemoveDirs)
{
    char Path[VFS_PATH_MAX];
    VFS_NodeInfo_t Info;
    int32_t Result;

    Result = FSCMD_ResolvePath(pPath, Path);
    if (Result == 0)
    {
        Result = VFS_Stat(Path, &Info);
    }

    if (Result != 0)
    {
        /* The error is printed below */
    }
    else if ((Info.Type == VFS_TYPE_DIR) && RemoveDirs)
    {
        Result = VFS_Rmdir(Path);
    }
    else
    {
        /* A folder gives -EISDIR */
        Result = VFS_Unlink(Path);
    }

    if (Result != 0)
    {
        PSH_Printf("rm: %s: %s\r\n", pPath, VFS_StrError(Result));
    }
}

/**
 * @brief "rm [-d] <path>..." command: removes files, with -d empty folders too
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleRm(uint8_t Argc, char **ppArgv)
{
    bool RemoveDirs = false;
    uint8_t Count = 0U;
    uint8_t Index;

    for (Index = 1U; Index < Argc; Index++)
    {
        if (STD_Strncmp(ppArgv[Index], "-d", 3U) == 0)
        {
            RemoveDirs = true;
        }
    }

    for (Index = 1U; Index < Argc; Index++)
    {
        if (STD_Strncmp(ppArgv[Index], "-d", 3U) != 0)
        {
            FSCMD_RemovePath(ppArgv[Index], RemoveDirs);
            Count++;
        }
    }

    if (Count == 0U)
    {
        PSH_Send("Usage: rm [-d] <path>...\r\n");
    }
}
#endif /* PSHELL_CMD_RM */

#if PSHELL_CMD_MKDIR
/**
 * @brief "mkdir <folder>..." command: creates folders, the parent has to exist
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleMkdir(uint8_t Argc, char **ppArgv)
{
    char Path[VFS_PATH_MAX];
    int32_t Result;
    uint8_t Index;

    if (Argc < 2U)
    {
        PSH_Send("Usage: mkdir <folder>...\r\n");
    }

    for (Index = 1U; Index < Argc; Index++)
    {
        Result = FSCMD_ResolvePath(ppArgv[Index], Path);
        if (Result == 0)
        {
            Result = VFS_Mkdir(Path);
        }
        if (Result != 0)
        {
            PSH_Printf("mkdir: %s: %s\r\n", ppArgv[Index], VFS_StrError(Result));
        }
    }
}
#endif /* PSHELL_CMD_MKDIR */

#if PSHELL_CMD_TOUCH
/**
 * @brief Create an empty file if it does not exist, an existing file is not changed (no clock for the time)
 * @param[in] pPath Path as given
 */
static void FSCMD_TouchFile(const char *pPath)
{
    char Path[VFS_PATH_MAX];
    VFS_NodeInfo_t Info;
    int32_t Result;

    Result = FSCMD_ResolvePath(pPath, Path);
    if (Result == 0)
    {
        Result = VFS_Stat(Path, &Info);
    }

    if (Result == -ENOENT)
    {
        Result = VFS_Open(Path, O_WRONLY | O_CREAT);
        if (Result >= 0)
        {
            Result = VFS_Close(Result);
        }
    }

    if (Result < 0)
    {
        PSH_Printf("touch: %s: %s\r\n", pPath, VFS_StrError(Result));
    }
}

/**
 * @brief "touch <file>..." command: creates empty files
 * @param[in] Argc Argument number
 * @param[in] ppArgv Arguments
 */
static void FSCMD_HandleTouch(uint8_t Argc, char **ppArgv)
{
    uint8_t Index;

    if (Argc < 2U)
    {
        PSH_Send("Usage: touch <file>...\r\n");
    }

    for (Index = 1U; Index < Argc; Index++)
    {
        FSCMD_TouchFile(ppArgv[Index]);
    }
}
#endif /* PSHELL_CMD_TOUCH */

/**
 * @brief Prompt printer of the shell: "host:/current/folder$ " in color
 * @note Runs in the shell task, or in the app task when an app returns. cd and the host name write
 *       run in the shell task as commands, so they can not change the strings while they are printed.
 */
static void FSCMD_PrintPrompt(void)
{
    PSH_Printf(FSCMD_COLOR_HOST "%s" FSCMD_COLOR_RESET ":" FSCMD_COLOR_CWD "%s" FSCMD_COLOR_RESET "$ ",
               FSTREE_GetHostname(),
               FSCMD_Cwd);
}

/*****************************************************************************/
void FSCMD_Init(void)
{
    PSH_SetPromptPrinter(FSCMD_PrintPrompt);

#if PSHELL_CMD_LS
    (void)PSH_Register("ls", FSCMD_HandleLs);
#endif
#if PSHELL_CMD_CAT
    (void)PSH_RegisterApp("cat", FSCMD_CatMain);
#endif
#if PSHELL_CMD_GREP
    (void)PSH_RegisterApp("grep", FSCMD_GrepMain);
#endif
#if PSHELL_CMD_ECHO
    (void)PSH_Register("echo", FSCMD_HandleEcho);
#endif
#if PSHELL_CMD_CD
    (void)PSH_Register("cd", FSCMD_HandleCd);
#endif
#if PSHELL_CMD_PWD
    (void)PSH_Register("pwd", FSCMD_HandlePwd);
#endif
#if PSHELL_CMD_RM
    (void)PSH_Register("rm", FSCMD_HandleRm);
#endif
#if PSHELL_CMD_MKDIR
    (void)PSH_Register("mkdir", FSCMD_HandleMkdir);
#endif
#if PSHELL_CMD_TOUCH
    (void)PSH_Register("touch", FSCMD_HandleTouch);
#endif
}
