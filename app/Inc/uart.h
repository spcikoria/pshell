/**
 * @file    uart.h
 * @brief   Shell transport on USART3, the ST-LINK virtual COM port of the Nucleo-H503RB.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Initialize USART3 (8N1) and its interrupt
 * @note Call it after the shell task is created, with the interrupts masked, the shell task creates the buffers
 * @param[in] BaudRate Baud rate
 */
void UART_Init(uint32_t BaudRate);
