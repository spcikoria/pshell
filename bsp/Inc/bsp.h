/**
 * @file    bsp.h
 * @brief   Board support: start-up and system services of the board.
 *
 * The portable code (app, platform) reaches the hardware only through bsp/Inc/bsp*.h. Every board implements
 * these headers in bsp/<board>/Src, see "Porting to another board" in README.md.
 *   bsp.h          start-up, run time stats timer, halt
 *   bsp_console.h  byte stream of the shell (UART)
 *   bsp_sdspi.h    SPI bus and chip select of the SD card
 *   bsp_pins.h     header pins of the "gpio" app
 */
#pragma once

#include <stdint.h>

/** Short name of the board and of the MCU, e.g. in the logo */
extern const char *const BSP_pBoardName;
extern const char *const BSP_pMcuName;

/**
 * @brief Start-up: interrupt priority grouping, clocks and caches, sets SystemCoreClock
 * @note  The first call of main(), before FreeRTOS is used
 */
void BSP_Init(void);

/**
 * @brief Stop the system after a fatal error: interrupts off, endless loop
 */
void BSP_Halt(void);

/**
 * @brief Start the time base of the FreeRTOS run time stats, called by vTaskStartScheduler()
 * @note  At least 10 times the tick rate, see portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() in FreeRTOSConfig.h
 */
void BSP_RunTimeInit(void);

/**
 * @brief Counter of the FreeRTOS run time stats, it may wrap
 * @return Counter value
 */
uint32_t BSP_RunTimeGet(void);
