#ifdef RB_GPIO
#include "gpio.h"
#include <gpiod.h>
#include <time.h>
#include <stdio.h>

// =============================================================================
// GPIO CONFIGURATION AND TABLES
// =============================================================================

/**
 * @brief GPIO chip and pin mapping table
 * 
 * Defines the mapping between GPIO chips and pins for various functions
 * such as power control, Iridium module enable, and boot detection.
 */
const rbGpioTable_t gpioTable = 
{
    { CHIP_NAME, POWER_ENABLE_PIN},
    { CHIP_NAME, IRIDIUM_ENABLE_PIN},
    { CHIP_NAME, IRIDIUM_BOOTED_PIN}
};

// =============================================================================
// GPIO CONTROL FUNCTIONS
// =============================================================================

/**
 * @brief Toggle a GPIO pin to specified state
 * 
 * Configures the specified GPIO pin as an output and sets it to the
 * requested value (high or low). This function handles the complete
 * GPIO configuration sequence including chip opening, line settings,
 * and request management.
 * 
 * @param selectedChip GPIO chip device path (e.g., "/dev/gpiochip0")
 * @param selectedPin GPIO pin number to control
 * @param value Desired pin state (GPIOD_LINE_VALUE_ACTIVE for high, 
 *              GPIOD_LINE_VALUE_INACTIVE for low)
 * @return true if operation was successful, false otherwise
 */
bool gpioToggle(const char * selectedChip, int selectedPin, int value)
{
    bool enabled = false;
    struct gpiod_chip * chip;
    struct gpiod_line_settings * settings;
    struct gpiod_line_info * line;
    struct gpiod_line_request * request;
    struct gpiod_line_config * config;
    int pin = selectedPin;

    chip = gpiod_chip_open(selectedChip); // Open GPIO chip
    if(chip)
    {
        settings = gpiod_line_settings_new(); // Create new line settings
        if(settings)
        {
            // Configure line as output
            if(gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) == 0)
            {
                config = gpiod_line_config_new(); // Create line config
                if(config)
                {
                    // Apply settings to selected pin
                    if(gpiod_line_config_add_line_settings(config, &pin, 1, settings) == 0)
                    {
                        // Request control of the line
                        request = gpiod_chip_request_lines(chip, NULL, config);
                        if(request)
                        {
                            // Set output value
                            if(gpiod_line_request_set_value(request, pin, value) == 0)
                            {
                                enabled = true;
                            }
                        }
                        gpiod_line_request_release(request); // Release line request
                    }
                }
                gpiod_line_config_free(config); // Free config
            }
        }
    }
    gpiod_chip_close(chip); // Close chip
    return enabled;
}

/**
 * @brief Read the current state of a GPIO pin
 * 
 * Configures the specified GPIO pin as an input and reads its current
 * value. This function handles the complete GPIO configuration sequence
 * including chip opening, line settings, and request management.
 * 
 * @param selectedChip GPIO chip device path
 * @param selectedPin GPIO pin number to read
 * @return Pin value (0 for low, 1 for high) or -1 if error occurred
 */
int gpioReceive(const char * selectedChip, int selectedPin)
{
    int value = -1;
    struct gpiod_chip * chip;
    struct gpiod_line_settings * settings;
    struct gpiod_line_info * line;
    struct gpiod_line_request * request;
    struct gpiod_line_config * config;
    int pin = selectedPin;

    chip = gpiod_chip_open(selectedChip); // Open GPIO chip
    if(chip)
    {
        settings = gpiod_line_settings_new(); // Create new line settings
        if(settings)
        {
            // Configure line as input
            if(gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT) == 0)
            {
                config = gpiod_line_config_new(); // Create line config
                if(config)
                {
                    // Apply settings to selected pin
                    if(gpiod_line_config_add_line_settings(config, &pin, 1, settings) == 0)
                    {
                        // Request control of the line
                        request = gpiod_chip_request_lines(chip, NULL, config);
                        if(request)
                        {
                            // Read pin value
                            value = gpiod_line_request_get_value(request, pin);
                        }
                        gpiod_line_request_release(request); // Release line request
                    }
                }
                gpiod_line_config_free(config); // Free config
            }
        }
    }
    gpiod_chip_close(chip); // Close chip
    return value;
}

/**
 * @brief Drive a GPIO pin to logic high state
 * 
 * Convenience function that sets the specified GPIO pin to logic high
 * by calling gpioToggle() with the appropriate value.
 * 
 * @param selectedChip GPIO chip device path
 * @param selectedPin GPIO pin number to drive high
 * @return true if operation was successful, false otherwise
 */
bool gpioDriveHigh(const char * selectedChip, int selectedPin)
{
    bool enabled = false;
    if(gpioToggle(selectedChip, selectedPin, GPIOD_LINE_VALUE_ACTIVE))
    {
        enabled = true;
    }
    return enabled;
}

/**
 * @brief Drive a GPIO pin to logic low state
 * 
 * Convenience function that sets the specified GPIO pin to logic low
 * by calling gpioToggle() with the appropriate value.
 * 
 * @param selectedChip GPIO chip device path
 * @param selectedPin GPIO pin number to drive low
 * @return true if operation was successful, false otherwise
 */
bool gpioDriveLow(const char * selectedChip, int selectedPin)
{
    bool disabled = false;
    if(gpioToggle(selectedChip, selectedPin, GPIOD_LINE_VALUE_INACTIVE))
    {
        disabled = true;
    }
    return disabled;
}

// =============================================================================
// GPIO MONITORING FUNCTIONS
// =============================================================================

/**
 * @brief Wait for a GPIO pin to go high with timeout
 * 
 * Continuously monitors the specified GPIO pin until it goes high or
 * the timeout period expires. This function is specifically designed
 * for Iridium modem boot detection where the boot signal indicates
 * the modem has completed its initialization sequence.
 * 
 * @param selectedChip GPIO chip device path
 * @param selectedPin GPIO pin number to monitor
 * @param timeout Maximum time to wait in seconds before giving up
 * @return true if pin went high within timeout period, false if timeout occurred
 */
bool gpioListenIridBooted(const char * selectedChip, int selectedPin, const int timeout)
{
    bool enabled = true;
    time_t start = time(NULL);
    while(gpioReceive(selectedChip, selectedPin) <= 0) // Keep checking until pin goes high
    {
        if (difftime(time(NULL), start) >= timeout) // Check for timeout
        {
            enabled = false; // Timeout occurred, pin did not go high
            break;
        }
    }
    return enabled;
}
#endif
