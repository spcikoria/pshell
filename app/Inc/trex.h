/**
 * @file    trex.h
 * @brief   T-Rex runner game, a shell app.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Main of the "trex" shell app: jump over the cacti until q or Ctrl+C
 * @note  Runs in the shell app task, register it with PSH_RegisterApp()
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
void TREX_Main(uint8_t Argc, char **ppArgv);
