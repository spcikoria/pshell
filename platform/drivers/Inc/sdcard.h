/**
 * @file    sdcard.h
 * @brief   SD card driver over SPI, 512 byte blocks: SDv1 (SDSC) and SDv2 (SDSC, SDHC, SDXC) cards, no MMC.
 *
 * Portable: the SPI bus and the chip select come from the board, see bsp_sdspi.h.
 *
 * @note Not thread-safe and blocking (busy wait), the caller serializes the calls. Task context only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Size of a block (sector) */
#define SDCARD_BLOCK_SIZE (512U)

/**
 * @brief  Initialize the SPI on the first call, then the card: needed after power-up and after a card change
 * @return 0, -ENODEV (no card or no answer) or -EIO (not supported card)
 */
int32_t SDCARD_Init(void);

/**
 * @brief  Check if the initialized card still answers, a removed or a changed card does not
 * @return true if the card answers
 */
bool SDCARD_IsAlive(void);

/**
 * @brief  Read blocks
 * @param[out] pBuffer  Buffer, Count * SDCARD_BLOCK_SIZE bytes
 * @param[in]  Block  First block
 * @param[in]  Count  Number of blocks
 * @return 0 or -EIO
 */
int32_t SDCARD_Read(uint8_t *pBuffer, uint32_t Block, uint32_t Count);

/**
 * @brief  Write blocks
 * @param[in]  pData  Data, Count * SDCARD_BLOCK_SIZE bytes
 * @param[in]  Block  First block
 * @param[in]  Count  Number of blocks
 * @return 0 or -EIO
 */
int32_t SDCARD_Write(const uint8_t *pData, uint32_t Block, uint32_t Count);

/**
 * @brief  Wait until the card finished the last write
 * @return 0 or -EIO on timeout
 */
int32_t SDCARD_Sync(void);
