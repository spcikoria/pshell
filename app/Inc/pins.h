/**
 * @file    pins.h
 * @brief   GPIO app: shows and sets the mode and the state of the header pins of the board (bsp_pins.h).
 */
#pragma once

#include <stdint.h>

/**
 * @brief Main of the "gpio" shell app: Tab switches the tabs, the arrows select, Space sets, q or Ctrl+C quits
 * @note  Runs in the shell app task, register it with PSH_RegisterApp()
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
void PINS_Main(uint8_t Argc, char **ppArgv);
