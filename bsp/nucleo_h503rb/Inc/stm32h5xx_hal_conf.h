/**
 * @file    stm32h5xx_hal_conf.h
 * @brief   HAL configuration of the Nucleo-H503RB, only the modules of st_hal_modules (meson.build).
 *
 * The firmware does not call the HAL, it is compiled only for completeness. A new family needs its own
 * <family>_hal_conf.h (template in STM32Cube<family>/Drivers/<family>_HAL_Driver/Inc) when its HAL is compiled.
 */
#pragma once

/* Modules, the same as st_hal_modules in meson.build */
#define HAL_MODULE_ENABLED
#define HAL_ICACHE_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

/* Oscillators: the HSE is the 24 MHz X3 crystal of the Nucleo */
#if !defined(HSE_VALUE)
#define HSE_VALUE 24000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100U
#endif
#if !defined(CSI_VALUE)
#define CSI_VALUE 4000000UL
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 64000000UL
#endif
#if !defined(HSI48_VALUE)
#define HSI48_VALUE 48000000UL
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE 32000UL
#endif
#if !defined(LSI_STARTUP_TIME)
#define LSI_STARTUP_TIME 130UL
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE 32768UL
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000UL
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE 12288000UL
#endif

/* System */
#define VDD_VALUE                      3300UL
#define TICK_INT_PRIORITY              (15UL)
#define USE_RTOS                       0U
#define PREFETCH_ENABLE                0U

/* No register callbacks */
#define USE_HAL_PCD_REGISTER_CALLBACKS 0U

/* Module headers */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32h5xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32h5xx_hal_gpio.h"
#endif
#ifdef HAL_ICACHE_MODULE_ENABLED
#include "stm32h5xx_hal_icache.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32h5xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32h5xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32h5xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32h5xx_hal_pwr.h"
#endif
#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32h5xx_hal_pcd.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32h5xx_hal_exti.h"
#endif

#define assert_param(expr) ((void)0U)
