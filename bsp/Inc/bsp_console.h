/**
 * @file    bsp_console.h
 * @brief   Board support: the byte stream of the shell, usually a UART.
 *
 * The implementation passes the received bytes to PSH_RxDataFromISR(), takes the bytes to send with
 * PSH_TxByteFromISR() and overrides PSH_TxStart() (pshell.h), which is called when new output is waiting.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Initialize the console, 8N1, and its interrupt
 * @note  Call it after the shell task is created, with the interrupts masked: the shell task creates the buffers
 * @param[in] BaudRate Baud rate
 */
void BSP_CON_Init(uint32_t BaudRate);
