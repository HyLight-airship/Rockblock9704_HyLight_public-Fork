#ifndef SERIAL_STM32_H
#define SERIAL_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"
#include <stdbool.h>

/**
 * @brief Sets the serial communication context for an STM32 system using HAL.
 *
 * This function must be called once before rbBegin() to configure the library
 * to use the specified UART peripheral.
 *
 * @param huart A pointer to the initialized UART_HandleTypeDef for the serial port.
 * @return true if the context was set successfully, false otherwise.
 */
bool setContextStm32(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif // SERIAL_STM32_H
