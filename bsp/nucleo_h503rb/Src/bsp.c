/**
 * @file    bsp.c
 * @brief   Nucleo-H503RB: clocks, cache, run time stats timer.
 */
#include "bsp.h"
#include "stm32h5xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_icache.h"
#include "stm32h5xx_ll_pwr.h"
#include "stm32h5xx_ll_rcc.h"
#include "stm32h5xx_ll_system.h"

/** System clock set by BSP_SystemClockConfig() */
#define BSP_SYSCLK_HZ (250000000U)

/** Priority grouping: all 4 priority bits are preemption bits, no subpriority, as FreeRTOS needs */
#define BSP_NVIC_PRIORITY_GROUPING (3U)

/** Frequency of the FreeRTOS run time stats counter, 10 times the tick rate */
#define BSP_RUN_TIME_HZ (10000U)

const char *const BSP_pBoardName = "Nucleo-H503";
const char *const BSP_pMcuName = "STM32H503RB";

/**
 * @brief System clock configuration with LL, the generic HAL_RCC_OscConfig()/HAL_RCC_ClockConfig() are ~2 KB
 *
 * SYSCLK 250 MHz from PLL1 (X3 crystal of the Nucleo, HSE 24 MHz / 12 * 250 / 2), buses not divided.
 * Runs once after reset: HSI is the system clock and PLL1 is off, so the PLL can be configured.
 */
static void BSP_SystemClockConfig(void)
{
    /* Voltage scale 0 is needed for 250 MHz, the PWR registers need no clock enable on the H5 */
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE0);
    while (LL_PWR_IsActiveFlag_VOS() == 0U)
    {
    }

    /* HSE crystal */
    LL_RCC_HSE_Enable();
    while (LL_RCC_HSE_IsReady() == 0U)
    {
    }

    /* PLL1: 24 MHz / 12 = 2 MHz input, * 250 = 500 MHz VCO, / 2 = 250 MHz on the P output (SYSCLK) */
    LL_RCC_PLL1_ConfigDomain_SYS(LL_RCC_PLL1SOURCE_HSE, 12U, 250U, 2U);
    LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_2_4);
    LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
    LL_RCC_PLL1P_Enable();
    LL_RCC_PLL1_Enable();
    while (LL_RCC_PLL1_IsReady() == 0U)
    {
    }

    /* Flash wait states and programming delay before the clock gets faster */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5)
    {
    }
    /* Programming delay for 168..250 MHz, LL has no function for it */
    MODIFY_REG(FLASH->ACR, FLASH_ACR_WRHIGHFREQ, FLASH_ACR_WRHIGHFREQ_1);

    /* Buses not divided (the reset value), then switch SYSCLK to PLL1 */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1)
    {
    }

    /* FreeRTOS (SysTick), the UART baud rate and the run time stats timer are computed from it */
    SystemCoreClock = BSP_SYSCLK_HZ;
}

/*****************************************************************************/
void BSP_Init(void)
{
    /*
     * No HAL_Init(): of what it does, only the priority grouping matters, set to the same value as HAL did (with 4
     * priority bits the reset value 0 means the same). The flash prefetch stays off as before, SystemCoreClock is
     * set by the clock configuration, and SysTick belongs to FreeRTOS, the port sets it up when the scheduler starts.
     */
    NVIC_SetPriorityGrouping(BSP_NVIC_PRIORITY_GROUPING);
    BSP_SystemClockConfig();
    LL_ICACHE_Enable();
}

/*****************************************************************************/
void BSP_Halt(void)
{
    __disable_irq();
    while (1)
    {
    }
}

/*****************************************************************************/
void BSP_RunTimeInit(void)
{
    /* TIM2 is a 32 bit timer, its clock is SYSCLK as APB1 is not divided. Enabling the clock reads the enable
       register back, so the clock is on before the first timer register write. */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    TIM2->PSC = (SystemCoreClock / BSP_RUN_TIME_HZ) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    /* Load the prescaler now, not at the first overflow */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;
}

/*****************************************************************************/
uint32_t BSP_RunTimeGet(void)
{
    return TIM2->CNT;
}
