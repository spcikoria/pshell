#pragma once

#include <stdint.h>

/** Runs the game loop, returns on q or Ctrl+C. Uses the shell for all I/O, call it from a shell app only. */
void G2048_Run(uint32_t Seed);
