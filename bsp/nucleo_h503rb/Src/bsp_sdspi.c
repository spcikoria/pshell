/**
 * @file    bsp_sdspi.c
 * @brief   Nucleo-H503RB: SD card on SPI1 of the Arduino header.
 *
 * Wiring:
 *     SPI1 SCK  D13 PA5 AF5
 *     SPI1 MISO D12 PA6 AF5, pull-up
 *     SPI1 MOSI D11 PA7 AF5
 *     CS        D10 PC9 GPIO output
 * The SPI kernel clock is the HSI (32 MHz after reset), so the system clock can change freely.
 */
#include "bsp_sdspi.h"
#include "stm32h5xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"

#define BSP_SD_SPI      SPI1
#define BSP_SD_SPI_PORT GPIOA
#define BSP_SD_SCK_PIN  LL_GPIO_PIN_5
#define BSP_SD_MISO_PIN LL_GPIO_PIN_6
#define BSP_SD_MOSI_PIN LL_GPIO_PIN_7
#define BSP_SD_SPI_AF   LL_GPIO_AF_5
#define BSP_SD_CS_PORT  GPIOC
#define BSP_SD_CS_PIN   LL_GPIO_PIN_9

/** SPI clock dividers (MBR field) of the 32 MHz kernel clock: /128 = 250 kHz for the init, /4 = 8 MHz after */
#define BSP_SD_MBR_SLOW (6U)
#define BSP_SD_MBR_FAST (1U)

/** 8 bit frames (DSIZE = bits - 1) */
#define BSP_SD_DSIZE_8BIT (7U)

/**
 * @brief Set a pin of the SPI port to the SPI alternate function
 * @param[in] Pin LL_GPIO_PIN_x, pin 0..7
 * @param[in] Pull LL_GPIO_PULL_x
 */
static void BSP_SD_InitSpiPin(uint32_t Pin, uint32_t Pull)
{
    LL_GPIO_SetAFPin_0_7(BSP_SD_SPI_PORT, Pin, BSP_SD_SPI_AF);
    LL_GPIO_SetPinSpeed(BSP_SD_SPI_PORT, Pin, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinPull(BSP_SD_SPI_PORT, Pin, Pull);
    LL_GPIO_SetPinMode(BSP_SD_SPI_PORT, Pin, LL_GPIO_MODE_ALTERNATE);
}

/*****************************************************************************/
void BSP_SD_Init(void)
{
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA | LL_AHB2_GRP1_PERIPH_GPIOC);

    /* Kernel clock: CLKP from the HSI, independent of the PLL */
    LL_RCC_SetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE_HSI);
    LL_RCC_SetSPIClockSource(LL_RCC_SPI1_CLKSOURCE_CLKP);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

    /* CS high before it becomes an output */
    LL_GPIO_SetOutputPin(BSP_SD_CS_PORT, BSP_SD_CS_PIN);
    LL_GPIO_SetPinSpeed(BSP_SD_CS_PORT, BSP_SD_CS_PIN, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinMode(BSP_SD_CS_PORT, BSP_SD_CS_PIN, LL_GPIO_MODE_OUTPUT);

    BSP_SD_InitSpiPin(BSP_SD_SCK_PIN, LL_GPIO_PULL_NO);
    BSP_SD_InitSpiPin(BSP_SD_MISO_PIN, LL_GPIO_PULL_UP);
    BSP_SD_InitSpiPin(BSP_SD_MOSI_PIN, LL_GPIO_PULL_NO);

    /* SSI high with software CS, otherwise mode fault. AFCNTR keeps the pins driven while SPE is off. */
    BSP_SD_SPI->CR1 = SPI_CR1_SSI;
    BSP_SD_SPI->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR;
    BSP_SD_SetSpeed(BSP_SD_SPEED_SLOW);
}

/*****************************************************************************/
void BSP_SD_SetSpeed(BSP_SD_Speed_e Speed)
{
    uint32_t Mbr = (Speed == BSP_SD_SPEED_FAST) ? BSP_SD_MBR_FAST : BSP_SD_MBR_SLOW;

    /* The SPI is disabled for the change, then started in endless transfer mode */
    BSP_SD_SPI->CR1 &= ~SPI_CR1_SPE;
    BSP_SD_SPI->CFG1 = (Mbr << SPI_CFG1_MBR_Pos) | (BSP_SD_DSIZE_8BIT << SPI_CFG1_DSIZE_Pos);
    BSP_SD_SPI->CR1 |= SPI_CR1_SPE;
    BSP_SD_SPI->CR1 |= SPI_CR1_CSTART;
}

/*****************************************************************************/
void BSP_SD_Select(bool Select)
{
    if (Select)
    {
        LL_GPIO_ResetOutputPin(BSP_SD_CS_PORT, BSP_SD_CS_PIN);
    }
    else
    {
        LL_GPIO_SetOutputPin(BSP_SD_CS_PORT, BSP_SD_CS_PIN);
    }
}

/*****************************************************************************/
uint8_t BSP_SD_Exchange(uint8_t Byte)
{
    /* 8 bit access, a 32 bit write would put 4 frames into the FIFO */
    while ((BSP_SD_SPI->SR & SPI_SR_TXP) == 0U)
    {
    }
    *(volatile uint8_t *)&BSP_SD_SPI->TXDR = Byte;

    while ((BSP_SD_SPI->SR & SPI_SR_RXP) == 0U)
    {
    }
    return *(volatile uint8_t *)&BSP_SD_SPI->RXDR;
}
