/**
 * @file    pshell_config.h
 * @brief   Commands and apps of the shell: 1 builds one into the binary, 0 leaves it out.
 *
 * Change a value here, or override it from the build, e.g. meson setup ... -Dc_args=-DPSHELL_CMD_CAT=0
 */
#pragma once

/* File system commands, fs_commands.c */
#ifndef PSHELL_CMD_LS
#define PSHELL_CMD_LS (1)
#endif

#ifndef PSHELL_CMD_CAT
#define PSHELL_CMD_CAT (1)
#endif

#ifndef PSHELL_CMD_GREP
#define PSHELL_CMD_GREP (1)
#endif

#ifndef PSHELL_CMD_ECHO
#define PSHELL_CMD_ECHO (1)
#endif

#ifndef PSHELL_CMD_CD
#define PSHELL_CMD_CD (1)
#endif

#ifndef PSHELL_CMD_PWD
#define PSHELL_CMD_PWD (1)
#endif

#ifndef PSHELL_CMD_RM
#define PSHELL_CMD_RM (1)
#endif

#ifndef PSHELL_CMD_MKDIR
#define PSHELL_CMD_MKDIR (1)
#endif

#ifndef PSHELL_CMD_TOUCH
#define PSHELL_CMD_TOUCH (1)
#endif

/* System commands, sandbox.c and main.c */
#ifndef PSHELL_CMD_HELP
#define PSHELL_CMD_HELP (1)
#endif

#ifndef PSHELL_CMD_CLEAR
#define PSHELL_CMD_CLEAR (1)
#endif

#ifndef PSHELL_CMD_STATS
#define PSHELL_CMD_STATS (1)
#endif

#ifndef PSHELL_CMD_LOGO
#define PSHELL_CMD_LOGO (1)
#endif

/* Apps, sandbox.c */
#ifndef PSHELL_APP_COUNT
#define PSHELL_APP_COUNT (1)
#endif

#ifndef PSHELL_APP_2048
#define PSHELL_APP_2048 (1)
#endif

#ifndef PSHELL_APP_SOKOBAN
#define PSHELL_APP_SOKOBAN (1)
#endif

#ifndef PSHELL_APP_GLOBE
#define PSHELL_APP_GLOBE (1)
#endif

#ifndef PSHELL_APP_MENU
#define PSHELL_APP_MENU (1)
#endif

#ifndef PSHELL_APP_GPIO
#define PSHELL_APP_GPIO (1)
#endif

#ifndef PSHELL_APP_RAYCAST
#define PSHELL_APP_RAYCAST (1)
#endif
