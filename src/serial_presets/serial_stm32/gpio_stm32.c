#include "main.h"
#include "gpio.h"

#if defined(USE_STM32_HAL) && defined(RB_GPIO)

/**
 * @brief Drives a GPIO pin high (3.3V).
 * @param port The GPIO port (e.g., GPIOB).
 * @param pin The GPIO pin (e.g., GPIO_PIN_8).
 * @return Always returns true.
 */
bool gpioDriveHigh(GPIO_TypeDef* port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    return true;
}

/**
 * @brief Drives a GPIO pin low (0V).
 * @param port The GPIO port.
 * @param pin The GPIO pin.
 * @return Always returns true.
 */
bool gpioDriveLow(GPIO_TypeDef* port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    return true;
}

/**
 * @brief Reads the state of a GPIO input pin.
 * @param port The GPIO port.
 * @param pin The GPIO pin.
 * @return 1 if the pin is high, 0 if it is low.
 */
int gpioReceive(GPIO_TypeDef* port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin);
}

/**
 * @brief Waits for the Iridium Booted pin to go high, with a timeout.
 * @param port The GPIO port of the booted pin.
 * @param pin The GPIO pin of the booted pin.
 * @param timeout Timeout duration in seconds.
 * @return true if the pin went high, false if a timeout occurred.
 */
bool gpioListenIridBooted(GPIO_TypeDef* port, uint16_t pin, const int timeout)
{
    uint32_t start_tick = HAL_GetTick();
    // Wait for the pin to go high
    while(HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
    {
        // Check for timeout
        if ((HAL_GetTick() - start_tick) >= (timeout * 1000))
        {
            return false; // Timeout
        }
    }
    return true;
}

#endif // defined(USE_STM32_HAL) && defined(RB_GPIO)
