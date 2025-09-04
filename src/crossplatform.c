#include "crossplatform.h"   // Local header for cross-platform compatibility

// =============================================================================
// WINDOWS PLATFORM IMPLEMENTATIONS
// =============================================================================

#if defined(_WIN32)
#include <windows.h>
#include <string.h>

/**
 * @brief Implementation of usleep() for Windows platform
 * 
 * Windows does not provide usleep() natively. This function converts
 * microseconds to milliseconds and calls the Windows Sleep() function.
 * 
 * @param microseconds Time to sleep in microseconds
 * @return 0 on success (always returns 0)
 */
int usleep(unsigned int microseconds)
{
    Sleep(microseconds / 1000); // Convert microseconds to milliseconds
    return 0;
}

/**
 * @brief Implementation of stpncpy() for Windows platform
 * 
 * Windows does not provide stpncpy() natively. This function copies
 * up to len characters from src to dst and returns a pointer to the
 * end of the copied string.
 * 
 * @param dst Destination buffer
 * @param src Source string
 * @param len Maximum number of characters to copy
 * @return Pointer to the end of the copied string in dst
 */
char * stpncpy (char * dst, const char * src, size_t len)
{
    size_t n = strlen (src);
    if (n > len)
    {
        n = len;
    }

    return strncpy (dst, src, len) + n;
}

/**
 * @brief Get system uptime in milliseconds (Windows implementation)
 * 
 * Uses Windows GetTickCount64() to return the number of milliseconds
 * elapsed since system startup.
 * 
 * @return Number of milliseconds since system start
 */
unsigned long millis(void)
{
    return GetTickCount64();
}

/**
 * @brief Block execution for specified milliseconds (Windows implementation)
 * 
 * Uses Windows Sleep() function to pause execution for the specified
 * duration.
 * 
 * @param ms Number of milliseconds to delay
 */
void delay(uint32_t ms)
{
    Sleep(ms);
}

// =============================================================================
// LINUX / macOS PLATFORM IMPLEMENTATIONS
// =============================================================================

#elif defined(__linux__) || defined(__APPLE__)

#include <time.h>
#include <unistd.h>

/**
 * @brief Get system uptime in milliseconds (Linux/macOS implementation)
 * 
 * Uses CLOCK_MONOTONIC to get a monotonic clock value and converts
 * it to milliseconds. This provides a reliable time reference that
 * is not affected by system clock changes.
 * 
 * @return Number of milliseconds since an arbitrary reference point
 */
unsigned long millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); // Get current monotonic time
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000; // Convert to milliseconds
}

/**
 * @brief Block execution for specified milliseconds (Linux/macOS implementation)
 * 
 * Uses usleep() to pause execution. Note that usleep() expects
 * microseconds, so we multiply the input by 1000.
 * 
 * @param ms Number of milliseconds to delay
 */
void delay(uint32_t ms)
{
    usleep(ms * 1000); // Convert milliseconds to microseconds
}

// =============================================================================
// STM32 HAL PLATFORM IMPLEMENTATIONS
// =============================================================================

#elif defined(USE_STM32_HAL)
#include "stm32g4xx_hal.h"

/**
 * @brief Get system uptime in milliseconds (STM32 HAL implementation)
 * 
 * Uses STM32 HAL's HAL_GetTick() function to return the number of
 * milliseconds since system boot. This function is provided by the
 * STM32 HAL layer.
 * 
 * @return Number of milliseconds since system boot
 */
unsigned long millis(void)
{
    return HAL_GetTick();
}

/**
 * @brief Block execution for specified milliseconds (STM32 HAL implementation)
 * 
 * Uses STM32 HAL's HAL_Delay() function to pause execution for the
 * specified duration. This function is provided by the STM32 HAL layer.
 * 
 * @param ms Number of milliseconds to delay
 */
void delay(uint32_t ms)
{
    HAL_Delay(ms);
}

#endif
