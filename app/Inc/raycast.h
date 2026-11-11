/**
 * @file    raycast.h
 * @brief   Minimal first person raycaster, a shell app.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Main of the "raycast" shell app: walk the maze until q or Ctrl+C
 * @note  Runs in the shell app task, register it with PSH_RegisterApp()
 * @param[in] Argc Number of arguments
 * @param[in] ppArgv Optional view mode: "color" (default) or "mono"
 */
void RAY_Main(uint8_t Argc, char **ppArgv);
