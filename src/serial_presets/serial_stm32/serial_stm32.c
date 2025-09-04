#include "serial.h"
#include "serial_stm32.h"
#include <string.h>

#if defined(USE_STM32_HAL)

// --- DMA and Buffer Configuration ---
#define DMA_RX_BUFFER_SIZE      64      // Small buffer for DMA hardware to write into.
#define APP_RX_BUFFER_SIZE      512     // Larger circular buffer for the application to read from.

static uint8_t dma_rx_buffer[DMA_RX_BUFFER_SIZE];
static uint8_t app_rx_buffer[APP_RX_BUFFER_SIZE];

static volatile uint16_t app_rx_head = 0;
static volatile uint16_t app_rx_tail = 0;
static size_t old_dma_pos = 0;

// --- STM32 HAL Specific Variables ---
static UART_HandleTypeDef *uart_handle = NULL;
extern serialContext context;
extern enum serialState serialState;

// --- Forward Declarations ---
static bool openPortStm32(void);
static bool closePortStm32(void);
static int readStm32(char *bytes, const uint16_t length);
static int writeStm32(const char *data, const uint16_t length);
static int peekStm32(void);

/**
 * @brief  UART DMA Receive Event Callback.
 * @note   This function is called by the HAL when the DMA buffer is half full,
 * completely full, or when an idle line is detected.
 * @param  huart: UART handle
 * @param  Size: Number of data bytes received since the last event.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == uart_handle)
    {
        size_t new_dma_pos = DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx);

        // If the DMA counter has wrapped around
        if (new_dma_pos < old_dma_pos)
        {
            // Process the first part (from old_pos to the end of the buffer)
            for (size_t i = old_dma_pos; i < DMA_RX_BUFFER_SIZE; i++)
            {
                app_rx_buffer[app_rx_head] = dma_rx_buffer[i];
                app_rx_head = (app_rx_head + 1) % APP_RX_BUFFER_SIZE;
            }
        }

        // Process the new data (from old_pos to new_pos)
        for (size_t i = old_dma_pos; i < new_dma_pos; i++)
        {
            app_rx_buffer[app_rx_head] = dma_rx_buffer[i];
            app_rx_head = (app_rx_head + 1) % APP_RX_BUFFER_SIZE;
        }

        old_dma_pos = new_dma_pos;
        // If the DMA is at the end of the buffer, reset its position
        if (old_dma_pos == DMA_RX_BUFFER_SIZE)
        {
            old_dma_pos = 0;
        }
    }
}


bool setContextStm32(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return false;
    }
    uart_handle = huart;

    strncpy(context.serialPort, "STM32_UART_DMA", SERIAL_PORT_LENGTH);
    context.serialBaud = huart->Init.BaudRate;
    context.serialInit = openPortStm32;
    context.serialDeInit = closePortStm32;
    context.serialRead = readStm32;
    context.serialWrite = writeStm32;
    context.serialPeek = peekStm32;

    return true;
}

static bool openPortStm32(void)
{
    if (uart_handle != NULL && serialState != OPEN)
    {
        // ***** START OF ADDED CODE *****
        // Abort any ongoing transfers to reset the UART state to Ready.
        // This makes the initialization more robust.
        if (HAL_UART_Abort(uart_handle) != HAL_OK)
        {
            return false;
        }
        // ***** END OF ADDED CODE *****

        // Reset buffer state
        app_rx_head = 0;
        app_rx_tail = 0;
        old_dma_pos = 0;

        // Start DMA reception with Idle Line detection
        if (HAL_UARTEx_ReceiveToIdle_DMA(uart_handle, dma_rx_buffer, DMA_RX_BUFFER_SIZE) == HAL_OK)
        {
            serialState = OPEN;
            return true;
        }
    }
    return false;
}

static bool closePortStm32(void)
{
    if (uart_handle != NULL && serialState != CLOSED)
    {
        HAL_UART_DMAStop(uart_handle);
        serialState = CLOSED;
        return true;
    }
    return false;
}

static int writeStm32(const char *data, const uint16_t length)
{
    if (serialState == OPEN)
    {
        // Use a long timeout as the modem can sometimes take a moment
        if (HAL_UART_Transmit(uart_handle, (uint8_t*)data, length, 500) == HAL_OK)
        {
            return length;
        }
    }
    return -1;
}

static int readStm32(char *bytes, const uint16_t length)
{
    if (serialState != OPEN)
    {
        return -1;
    }

    uint16_t bytes_read = 0;
    while (bytes_read < length && app_rx_tail != app_rx_head)
    {
        bytes[bytes_read++] = app_rx_buffer[app_rx_tail];
        app_rx_tail = (app_rx_tail + 1) % APP_RX_BUFFER_SIZE;
    }
    return bytes_read;
}

static int peekStm32(void)
{
    if (serialState != OPEN)
    {
        return -1;
    }

    if (app_rx_head >= app_rx_tail)
    {
        return app_rx_head - app_rx_tail;
    }
    else
    {
        return APP_RX_BUFFER_SIZE - (app_rx_tail - app_rx_head);
    }
}

#endif // defined(USE_STM32_HAL)
