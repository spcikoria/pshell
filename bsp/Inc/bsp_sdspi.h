/**
 * @file    bsp_sdspi.h
 * @brief   Board support: SPI bus and chip select of the SD card, the card protocol is in sdcard.c.
 *
 * SPI master, mode 0 (CPOL 0, CPHA 0), 8 bit frames, MSB first, chip select by software.
 * Blocking, task context only, the caller serializes the calls.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** SPI clock: slow for the card initialization (100..400 kHz), fast after it (at most 25 MHz) */
typedef enum
{
    BSP_SD_SPEED_SLOW,
    BSP_SD_SPEED_FAST,
} BSP_SD_Speed_e;

/**
 * @brief Clocks, pins and SPI, the chip select is high (not selected), the speed is slow
 */
void BSP_SD_Init(void);

/**
 * @brief Set the SPI clock
 * @param[in] Speed BSP_SD_SPEED_SLOW or BSP_SD_SPEED_FAST
 */
void BSP_SD_SetSpeed(BSP_SD_Speed_e Speed);

/**
 * @brief Drive the chip select
 * @param[in] Select true: low (selected), false: high
 */
void BSP_SD_Select(bool Select);

/**
 * @brief Send and receive one byte
 * @param[in] Byte Sent byte
 * @return Received byte
 */
uint8_t BSP_SD_Exchange(uint8_t Byte);
