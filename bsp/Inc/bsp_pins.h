/**
 * @file    bsp_pins.h
 * @brief   Board support: the header pins shown and set by the "gpio" app, addressed by index.
 *
 * A fixed pin is used by the board (e.g. a UART) and is never changed. A shared pin is used by a driver
 * (e.g. the SD card SPI): the app may change it, BSP_PIN_Restore() sets it back.
 * Task context only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Mode of a pin, the app sets only input, output and analog */
typedef enum
{
    BSP_PIN_MODE_INPUT = 0,
    BSP_PIN_MODE_OUTPUT,
    BSP_PIN_MODE_ANALOG,
    /* Alternate function, set by a driver */
    BSP_PIN_MODE_ALT,
} BSP_PIN_Mode_e;

/** Result of BSP_PIN_AdcRead() without a value */
#define BSP_PIN_ADC_ERROR (-1)

/** One header pin */
typedef struct
{
    /* Header name, e.g. "D2" */
    const char *pName;
    /* MCU pin, e.g. "PA10" */
    const char *pPortName;
    /* Used by the board, never changed */
    bool Fixed;
    /* Used by a driver, restored by BSP_PIN_Restore() */
    bool Shared;
    /* Has an ADC input */
    bool Analog;
} BSP_PIN_Info_t;

/**
 * @brief Number of header pins
 * @return Number of pins
 */
uint8_t BSP_PIN_Count(void);

/**
 * @brief Description of a pin
 * @param[in] Index 0..BSP_PIN_Count() - 1
 * @return The pin
 */
const BSP_PIN_Info_t *BSP_PIN_GetInfo(uint8_t Index);

/**
 * @brief Clocks and ADC, saves the configuration of the shared pins
 */
void BSP_PIN_Init(void);

/**
 * @brief Restore the shared pins saved by BSP_PIN_Init()
 */
void BSP_PIN_Restore(void);

/**
 * @brief Current mode of a pin
 * @param[in] Index Index of the pin
 * @return The mode
 */
BSP_PIN_Mode_e BSP_PIN_GetMode(uint8_t Index);

/**
 * @brief Set the mode of a pin, no pull, an output starts low and push-pull
 * @param[in] Index Index of the pin, not a fixed one
 * @param[in] Mode BSP_PIN_MODE_INPUT, BSP_PIN_MODE_OUTPUT or BSP_PIN_MODE_ANALOG
 */
void BSP_PIN_SetMode(uint8_t Index, BSP_PIN_Mode_e Mode);

/**
 * @brief Level of a pin: the output register of an output, the input level otherwise
 * @param[in] Index Index of the pin
 * @return true if high
 */
bool BSP_PIN_Read(uint8_t Index);

/**
 * @brief Toggle an output
 * @param[in] Index Index of the pin
 */
void BSP_PIN_Toggle(uint8_t Index);

/**
 * @brief One ADC conversion of an analog pin
 * @param[in] Index Index of the pin
 * @param[out] pMillivolts Voltage of the result
 * @return Raw result, or BSP_PIN_ADC_ERROR (no ADC input, ADC not ready or timeout)
 */
int32_t BSP_PIN_AdcRead(uint8_t Index, int32_t *pMillivolts);
