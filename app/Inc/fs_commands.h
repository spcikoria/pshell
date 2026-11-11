/**
 * @file    fs_commands.h
 * @brief   Shell commands of the virtual file system: ls, cat, grep, echo, cd, pwd, rm, mkdir and touch.
 *
 * The current folder is kept here, a relative path of any command starts from it.
 * Each command can be left out of the build in pshell_config.h.
 */
#pragma once

/**
 * @brief Register the file system commands in the shell, call it before the scheduler starts
 */
void FSCMD_Init(void);
