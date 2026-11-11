/**
 * @file    bsp_pins.c
 * @brief   Nucleo-H503RB: Arduino pins D0..D15 for the "gpio" app, ADC1 for the analog pins.
 *
 * D0/D1 are fixed. D10..D13 are shared with the SD card (SPI1), their configuration is saved by BSP_PIN_Init()
 * and restored by BSP_PIN_Restore().
 */
#include "bsp_pins.h"
#include "FreeRTOS.h"
#include "stm32h5xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"
#include "task.h"

#define BSP_PIN_COUNT        (16U)
#define BSP_PIN_NO_ADC       (0xFFU)

/** ADC: 12 bit, the longest sample time (640.5 cycles), SMPR1 holds channels 0..9 */
#define BSP_PIN_ADC_SMP        (7U)
#define BSP_PIN_ADC_SMP_MASK   (7U)
#define BSP_PIN_ADC_SMP_BITS   (3U)
#define BSP_PIN_ADC_SMPR1_CH   (10U)
#define BSP_PIN_ADC_DATA_MASK  (0xFFFU)
#define BSP_PIN_ADC_MAX        (4095)
#define BSP_PIN_ADC_TIMEOUT_MS (10U)
#define BSP_PIN_VREF_MV        (3300)

/** ADC regulator start-up is 20 us, two ticks are at least one full tick */
#define BSP_PIN_ADC_VREG_TICKS (2U)

/** Hardware of a pin */
typedef struct
{
    GPIO_TypeDef *pPort;
    uint32_t Pin;
    /* ADC1 input, BSP_PIN_NO_ADC if none */
    uint8_t AdcChannel;
} BSP_PIN_Hw_t;

/** Configuration of a shared pin at the start */
typedef struct
{
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Output;
} BSP_PIN_Saved_t;

static const BSP_PIN_Info_t BSP_PIN_Info[BSP_PIN_COUNT] = {
    {.pName = "D0", .pPortName = "PB15", .Fixed = true},
    {.pName = "D1", .pPortName = "PB14", .Fixed = true},
    {.pName = "D2", .pPortName = "PA10"},
    {.pName = "D3", .pPortName = "PB3"},
    {.pName = "D4", .pPortName = "PB5"},
    {.pName = "D5", .pPortName = "PB4"},
    {.pName = "D6", .pPortName = "PB10"},
    {.pName = "D7", .pPortName = "PA8"},
    {.pName = "D8", .pPortName = "PC7"},
    {.pName = "D9", .pPortName = "PC6"},
    {.pName = "D10", .pPortName = "PC9", .Shared = true},
    {.pName = "D11", .pPortName = "PA7", .Shared = true, .Analog = true},
    {.pName = "D12", .pPortName = "PA6", .Shared = true, .Analog = true},
    {.pName = "D13", .pPortName = "PA5", .Shared = true, .Analog = true},
    {.pName = "D14", .pPortName = "PB7"},
    {.pName = "D15", .pPortName = "PB6"},
};

static const BSP_PIN_Hw_t BSP_PIN_Hw[BSP_PIN_COUNT] = {
    {GPIOB, LL_GPIO_PIN_15, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_14, BSP_PIN_NO_ADC},
    {GPIOA, LL_GPIO_PIN_10, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_3, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_5, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_4, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_10, BSP_PIN_NO_ADC},
    {GPIOA, LL_GPIO_PIN_8, BSP_PIN_NO_ADC},
    {GPIOC, LL_GPIO_PIN_7, BSP_PIN_NO_ADC},
    {GPIOC, LL_GPIO_PIN_6, BSP_PIN_NO_ADC},
    {GPIOC, LL_GPIO_PIN_9, BSP_PIN_NO_ADC},
    {GPIOA, LL_GPIO_PIN_7, 7U},
    {GPIOA, LL_GPIO_PIN_6, 3U},
    {GPIOA, LL_GPIO_PIN_5, 19U},
    {GPIOB, LL_GPIO_PIN_7, BSP_PIN_NO_ADC},
    {GPIOB, LL_GPIO_PIN_6, BSP_PIN_NO_ADC},
};

static BSP_PIN_Saved_t BSP_PIN_Saved[BSP_PIN_COUNT];
static bool BSP_PIN_AdcReady;

/**
 * @brief Check if the ADC timeout passed
 * @param[in] Start Tick count at the start
 * @return true if the time is over
 */
static bool BSP_PIN_TimeIsOver(TickType_t Start)
{
    return (TickType_t)(xTaskGetTickCount() - Start) >= pdMS_TO_TICKS(BSP_PIN_ADC_TIMEOUT_MS);
}

/**
 * @brief Wait until the masked bits of a register have a value
 * @param[in] pReg Register
 * @param[in] Mask Bits to check
 * @param[in] Expected Value of the masked bits
 * @return true if reached, false on timeout
 */
static bool BSP_PIN_WaitBits(volatile uint32_t *pReg, uint32_t Mask, uint32_t Expected)
{
    TickType_t Start = xTaskGetTickCount();
    bool Reached = (*pReg & Mask) == Expected;

    while (!Reached && !BSP_PIN_TimeIsOver(Start))
    {
        Reached = (*pReg & Mask) == Expected;
    }
    return Reached;
}

/**
 * @brief Start ADC1 once: HSE kernel clock, regulator, single ended calibration, enable
 */
static void BSP_PIN_AdcInit(void)
{
    TickType_t Start;
    bool Ready;

    if (!BSP_PIN_AdcReady)
    {
        /* Asynchronous clock from the HSE (24 MHz), not divided */
        LL_RCC_SetADCDACClockSource(LL_RCC_ADCDAC_CLKSOURCE_HSE);
        LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_ADC);
        MODIFY_REG(ADC12_COMMON->CCR, ADC_CCR_CKMODE | ADC_CCR_PRESC, 0U);

        /* Leave the deep power down, then start the regulator */
        ADC1->CR = 0U;
        ADC1->CR = ADC_CR_ADVREGEN;
        vTaskDelay(BSP_PIN_ADC_VREG_TICKS);

        ADC1->CR = ADC_CR_ADVREGEN | ADC_CR_ADCAL;
        Ready = BSP_PIN_WaitBits(&ADC1->CR, ADC_CR_ADCAL, 0U);

        /* ADEN is ignored for a few ADC cycles after the calibration, set it until the ADC is ready */
        ADC1->ISR = ADC_ISR_ADRDY;
        Start = xTaskGetTickCount();
        while (Ready && ((ADC1->ISR & ADC_ISR_ADRDY) == 0U))
        {
            if ((ADC1->CR & ADC_CR_ADEN) == 0U)
            {
                ADC1->CR = ADC_CR_ADVREGEN | ADC_CR_ADEN;
            }
            Ready = !BSP_PIN_TimeIsOver(Start);
        }

        /* 12 bit, single conversion, software trigger, a new result overwrites the old one */
        ADC1->CFGR |= ADC_CFGR_OVRMOD;
        BSP_PIN_AdcReady = Ready;
    }
}

/*****************************************************************************/
uint8_t BSP_PIN_Count(void)
{
    return BSP_PIN_COUNT;
}

/*****************************************************************************/
const BSP_PIN_Info_t *BSP_PIN_GetInfo(uint8_t Index)
{
    return &BSP_PIN_Info[Index];
}

/*****************************************************************************/
void BSP_PIN_Init(void)
{
    const BSP_PIN_Hw_t *pHw;
    uint8_t Index;

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA | LL_AHB2_GRP1_PERIPH_GPIOB | LL_AHB2_GRP1_PERIPH_GPIOC);

    for (Index = 0U; Index < BSP_PIN_COUNT; Index++)
    {
        pHw = &BSP_PIN_Hw[Index];
        if (BSP_PIN_Info[Index].Shared)
        {
            BSP_PIN_Saved[Index].Mode = LL_GPIO_GetPinMode(pHw->pPort, pHw->Pin);
            BSP_PIN_Saved[Index].Pull = LL_GPIO_GetPinPull(pHw->pPort, pHw->Pin);
            BSP_PIN_Saved[Index].Output = LL_GPIO_IsOutputPinSet(pHw->pPort, pHw->Pin);
        }
    }

    BSP_PIN_AdcInit();
}

/*****************************************************************************/
void BSP_PIN_Restore(void)
{
    const BSP_PIN_Hw_t *pHw;
    uint8_t Index;

    /* The level before the mode, so the SD card CS stays high */
    for (Index = 0U; Index < BSP_PIN_COUNT; Index++)
    {
        pHw = &BSP_PIN_Hw[Index];
        if (BSP_PIN_Info[Index].Shared)
        {
            if (BSP_PIN_Saved[Index].Output != 0U)
            {
                LL_GPIO_SetOutputPin(pHw->pPort, pHw->Pin);
            }
            else
            {
                LL_GPIO_ResetOutputPin(pHw->pPort, pHw->Pin);
            }
            LL_GPIO_SetPinPull(pHw->pPort, pHw->Pin, BSP_PIN_Saved[Index].Pull);
            LL_GPIO_SetPinMode(pHw->pPort, pHw->Pin, BSP_PIN_Saved[Index].Mode);
        }
    }
}

/*****************************************************************************/
BSP_PIN_Mode_e BSP_PIN_GetMode(uint8_t Index)
{
    const BSP_PIN_Hw_t *pHw = &BSP_PIN_Hw[Index];
    BSP_PIN_Mode_e Mode;

    switch (LL_GPIO_GetPinMode(pHw->pPort, pHw->Pin))
    {
    case LL_GPIO_MODE_INPUT:
        Mode = BSP_PIN_MODE_INPUT;
        break;
    case LL_GPIO_MODE_OUTPUT:
        Mode = BSP_PIN_MODE_OUTPUT;
        break;
    case LL_GPIO_MODE_ANALOG:
        Mode = BSP_PIN_MODE_ANALOG;
        break;
    default:
        Mode = BSP_PIN_MODE_ALT;
        break;
    }
    return Mode;
}

/*****************************************************************************/
void BSP_PIN_SetMode(uint8_t Index, BSP_PIN_Mode_e Mode)
{
    const BSP_PIN_Hw_t *pHw = &BSP_PIN_Hw[Index];

    if (!BSP_PIN_Info[Index].Fixed)
    {
        LL_GPIO_SetPinPull(pHw->pPort, pHw->Pin, LL_GPIO_PULL_NO);

        switch (Mode)
        {
        case BSP_PIN_MODE_INPUT:
            LL_GPIO_SetPinMode(pHw->pPort, pHw->Pin, LL_GPIO_MODE_INPUT);
            break;
        case BSP_PIN_MODE_OUTPUT:
            LL_GPIO_ResetOutputPin(pHw->pPort, pHw->Pin);
            LL_GPIO_SetPinOutputType(pHw->pPort, pHw->Pin, LL_GPIO_OUTPUT_PUSHPULL);
            LL_GPIO_SetPinMode(pHw->pPort, pHw->Pin, LL_GPIO_MODE_OUTPUT);
            break;
        default:
            LL_GPIO_SetPinMode(pHw->pPort, pHw->Pin, LL_GPIO_MODE_ANALOG);
            break;
        }
    }
}

/*****************************************************************************/
bool BSP_PIN_Read(uint8_t Index)
{
    const BSP_PIN_Hw_t *pHw = &BSP_PIN_Hw[Index];
    bool High;

    if (LL_GPIO_GetPinMode(pHw->pPort, pHw->Pin) == LL_GPIO_MODE_OUTPUT)
    {
        High = (LL_GPIO_IsOutputPinSet(pHw->pPort, pHw->Pin) != 0U);
    }
    else
    {
        High = (LL_GPIO_IsInputPinSet(pHw->pPort, pHw->Pin) != 0U);
    }
    return High;
}

/*****************************************************************************/
void BSP_PIN_Toggle(uint8_t Index)
{
    LL_GPIO_TogglePin(BSP_PIN_Hw[Index].pPort, BSP_PIN_Hw[Index].Pin);
}

/*****************************************************************************/
int32_t BSP_PIN_AdcRead(uint8_t Index, int32_t *pMillivolts)
{
    uint8_t Channel = BSP_PIN_Hw[Index].AdcChannel;
    int32_t Value = BSP_PIN_ADC_ERROR;
    uint32_t Shift;

    if (BSP_PIN_AdcReady && (Channel != BSP_PIN_NO_ADC))
    {
        if (Channel < BSP_PIN_ADC_SMPR1_CH)
        {
            Shift = (uint32_t)Channel * BSP_PIN_ADC_SMP_BITS;
            MODIFY_REG(ADC1->SMPR1, BSP_PIN_ADC_SMP_MASK << Shift, BSP_PIN_ADC_SMP << Shift);
        }
        else
        {
            Shift = (uint32_t)(Channel - BSP_PIN_ADC_SMPR1_CH) * BSP_PIN_ADC_SMP_BITS;
            MODIFY_REG(ADC1->SMPR2, BSP_PIN_ADC_SMP_MASK << Shift, BSP_PIN_ADC_SMP << Shift);
        }

        ADC1->SQR1 = (uint32_t)Channel << ADC_SQR1_SQ1_Pos;
        ADC1->ISR = ADC_ISR_EOC;
        ADC1->CR |= ADC_CR_ADSTART;

        if (BSP_PIN_WaitBits(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC))
        {
            Value = (int32_t)(ADC1->DR & BSP_PIN_ADC_DATA_MASK);
            *pMillivolts = (Value * BSP_PIN_VREF_MV) / BSP_PIN_ADC_MAX;
        }
    }
    return Value;
}
