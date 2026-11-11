/**
 * @file    globe.h
 * @brief   Rotating ASCII globe, a shell app.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Main of the "globe" shell app: draws the rotating Earth until q or Ctrl+C
 * @note  Runs in the shell app task, register it with PSH_RegisterApp()
 * @param[in] Argc Unused
 * @param[in] ppArgv Unused
 */
void GLOBE_Main(uint8_t Argc, char **ppArgv);
