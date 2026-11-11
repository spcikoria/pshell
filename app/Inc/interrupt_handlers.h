/**
 * @file    interrupt_handlers.h
 * @brief   Interrupt handler prototypes.
 *
 * SVC_Handler, PendSV_Handler and SysTick_Handler are provided by the FreeRTOS port,
 * the peripheral handlers (e.g. the console UART) by bsp/<board>/Src.
 */
#pragma once

/**
 * @brief Non maskable interrupt
 */
void NMI_Handler(void);

/**
 * @brief Hard fault interrupt
 */
void HardFault_Handler(void);

/**
 * @brief Memory management fault
 */
void MemManage_Handler(void);

/**
 * @brief Pre-fetch fault, memory access fault
 */
void BusFault_Handler(void);

/**
 * @brief Undefined instruction or illegal state
 */
void UsageFault_Handler(void);

/**
 * @brief Debug monitor
 */
void DebugMon_Handler(void);
