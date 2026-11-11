/**
 * @file    bsp_console.c
 * @brief   Nucleo-H503RB: shell console on USART3 (PA3 RX, PA4 TX), the ST-LINK virtual COM port.
 */
#include "bsp_console.h"
#include "FreeRTOS.h"
#include "pshell.h"
#include "stm32h5xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

/** The interrupt uses the FreeRTOS API, so it must not be above the FreeRTOS syscall priority */
#define BSP_CON_IRQ_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1U)

/**
 * @brief Set a GPIOA pin to USART3 (AF13) with pull-up, push-pull and low speed are the reset values
 * @note  The LL pin functions take one pin only. The AF is set before the mode, so the pin never drives another
 *        function.
 * @param[in] Pin LL_GPIO_PIN_x, pin 0..7
 */
static void BSP_CON_InitPin(uint32_t Pin)
{
    LL_GPIO_SetAFPin_0_7(GPIOA, Pin, LL_GPIO_AF_13);
    LL_GPIO_SetPinPull(GPIOA, Pin, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinMode(GPIOA, Pin, LL_GPIO_MODE_ALTERNATE);
}

/*****************************************************************************/
void BSP_CON_Init(uint32_t BaudRate)
{
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);

    /* PA3 RX, PA4 TX, connected to the ST-LINK */
    BSP_CON_InitPin(LL_GPIO_PIN_3);
    BSP_CON_InitPin(LL_GPIO_PIN_4);

    /* The kernel clock is PCLK1 after reset, equal to SystemCoreClock as APB1 is not divided.
       The other reset values mean 8N1 and no FIFO. */
    USART3->BRR = (SystemCoreClock + (BaudRate / 2U)) / BaudRate;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;

    NVIC_SetPriority(USART3_IRQn, BSP_CON_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART3_IRQn);
}

/*****************************************************************************/
void PSH_TxStart(void)
{
    /* Not atomic, but the interrupt only clears this bit: the worst case is one extra interrupt */
    USART3->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

/**
 * @brief USART3 interrupt: received byte to the shell, next byte to send from the shell
 */
void USART3_IRQHandler(void)
{
    uint32_t Status = USART3->ISR;
    uint8_t Byte;

    /* An error flag that is not cleared would fire the interrupt again and again */
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;

    if ((Status & USART_ISR_RXNE_RXFNE) != 0U)
    {
        Byte = (uint8_t)USART3->RDR;
        PSH_RxDataFromISR(&Byte, 1U);
    }

    if (((USART3->CR1 & USART_CR1_TXEIE_TXFNFIE) != 0U) && ((Status & USART_ISR_TXE_TXFNF) != 0U))
    {
        if (PSH_TxByteFromISR(&Byte, 1U) != 0U)
        {
            USART3->TDR = Byte;
        }
        else
        {
            USART3->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        }
    }
}
