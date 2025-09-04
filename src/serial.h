#ifndef SERIAL_H
#define SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32)
#include <io.h>
#define access _access
#elif defined(__linux__) || defined(__APPLE__) // <-- The include was moved here
#include <unistd.h>
#endif

// This block includes the correct platform-specific header file
#if defined(__linux__) || defined(__APPLE__)
    #include "serial_linux.h"
#elif defined(_WIN32)
    #include "serial_windows.h"
#elif defined(ARDUINO)
    #include "serial_arduino.h"
#elif defined(USE_STM32_HAL)
    #include "serial_stm32.h" // This line is the new addition
#endif

#define SERIAL_PORT_LENGTH 50U

// Callback functions which will link to the serial interface
typedef bool(*serialInitFunc)();
typedef bool(*serialDeInitFunc)();
typedef int(*serialReadFunc)(char * bytes, const uint16_t length);
typedef int(*serialWriteFunc)(const char * data, const uint16_t length);
typedef int(*serialPeekFunc)(void);

typedef struct
{
    serialInitFunc           serialInit;
    serialDeInitFunc         serialDeInit;
    serialReadFunc           serialRead;
    serialWriteFunc          serialWrite;
    serialPeekFunc           serialPeek;
    char                     serialPort[SERIAL_PORT_LENGTH];
    uint32_t                 serialBaud;
} serialContext;

enum serialState
{
    CLOSED,
    OPEN,
};

#ifdef __cplusplus
}
#endif

#endif
