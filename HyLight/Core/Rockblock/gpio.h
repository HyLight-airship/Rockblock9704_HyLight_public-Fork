#ifndef GPIO_H
#define GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#ifdef USE_STM32_HAL
    #include "stm32g4xx_hal.h"
    // Define the pin structure for the STM32 platform
    typedef struct
    {
        GPIO_TypeDef* port;
        uint16_t pin;
    } gpioPin_t;
#else
    // Original definitions for Linux and other platforms
    #define PI_HAT_PATH "/dev/ttyS0"
    #define CHIP_NAME "/dev/gpiochip0"
    #define POWER_ENABLE_PIN 24U
    #define IRIDIUM_ENABLE_PIN 16U
    #define IRIDIUM_BOOTED_PIN 23U
    #define GPIO_CHIP_MAX_LEN 20U
    typedef struct
    {
        const char chip[GPIO_CHIP_MAX_LEN];
        uint8_t pin;
    } gpioPin_t;
#endif

// This structure is now compatible with both platforms
typedef struct
{
    gpioPin_t powerEnable;
    gpioPin_t iridiumEnable;
    gpioPin_t booted;
} rbGpioTable_t;

extern const rbGpioTable_t gpioTable;

#ifdef USE_STM32_HAL
    // New function prototypes for STM32 HAL
    bool gpioDriveHigh(GPIO_TypeDef* port, uint16_t pin);
    bool gpioDriveLow(GPIO_TypeDef* port, uint16_t pin);
    int gpioReceive(GPIO_TypeDef* port, uint16_t pin);
    bool gpioListenIridBooted(GPIO_TypeDef* port, uint16_t pin, const int timeout);
#else
    // Original function prototypes
    bool gpioToggle(const char * selectedChip, int selectedPin, int value);
    bool gpioDriveHigh(const char * selectedChip, int selectedPin);
    bool gpioDriveLow(const char * selectedChip, int selectedPin);
    int gpioReceive(const char * selectedChip, int selectedPin);
    bool gpioListenIridBooted(const char * selectedChip, int selectedPin, const int timeout);
#endif


#ifdef __cplusplus
}
#endif

#endif // GPIO_H
