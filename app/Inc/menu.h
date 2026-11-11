/**
 * @file    menu.h
 * @brief   TUI demo app: tabs, a titled frame and a selection list, drawn with the tui library.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Main of the "menu" shell app: Tab switches the tabs, the arrows select, q or Ctrl+C quits
 * @note  Runs in the shell app task, register it with PSH_RegisterApp()
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
void MENU_Main(uint8_t Argc, char **ppArgv);
