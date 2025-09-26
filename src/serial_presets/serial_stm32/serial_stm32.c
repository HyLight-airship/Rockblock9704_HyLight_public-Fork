#include "serial.h"
#include "serial_stm32.h"
#include <string.h>

#if defined(USE_STM32_HAL)

// --- Ring buffer Variables ---
#define RX_BUFFER_SIZE      512

typedef struct
{
    uint8_t buffer[RX_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuffer;


static RingBuffer rx_ringbuffer = { {0}, 0, 0 };

static uint8_t rx_byte;

// --- Rockblock serial variables ---
extern serialContext context;
extern enum serialState serialState;

// --- STM32 HAL Specific Variables ---
static UART_HandleTypeDef *uart_handle = NULL;

// --- Prototypes ---
static bool openPortStm32(void);
static bool closePortStm32(void);
static int readStm32(char *bytes, const uint16_t length);
static int writeStm32(const char *data, const uint16_t length);
static int peekStm32(void);

void ringbuffer_put(RingBuffer *rb, uint8_t data)
{
    uint16_t next = (rb->head + 1) % RX_BUFFER_SIZE;
    if (next != rb->tail) {   // buffer not full
        rb->buffer[rb->head] = data;
        rb->head = next;
    }
    else
    {
    	printf("ERROR!!! Ring buffer overflow\r\n");
    }
}

int ringbuffer_get(RingBuffer *rb, uint8_t *data)
{
    if (rb->head == rb->tail)
    {
    	return 0;  // empty
    }

    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RX_BUFFER_SIZE;
    return 1;
}

void ringbuffer_clear(RingBuffer *rb)
{
    rb->head = 0;
    rb->tail = 0;
    memset(rb->buffer, 0, RX_BUFFER_SIZE);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == uart_handle->Instance)
    {
    	ringbuffer_put(&rx_ringbuffer, rx_byte);
        // Re-enable interrupt to receive next byte
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}


bool setContextStm32(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return false;
    }
    uart_handle = huart;

    strncpy(context.serialPort, "STM32_UART1", SERIAL_PORT_LENGTH);
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
        ringbuffer_clear(&rx_ringbuffer);

        // Reception
        if (HAL_UART_Receive_IT(uart_handle, &rx_byte, 1) == HAL_OK)
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
        HAL_UART_Abort(uart_handle);
        serialState = CLOSED;
        return true;
    }
    return false;
}

static int writeStm32(const char *data, const uint16_t length)
{
    if (serialState == OPEN)
    {
    	// Transmission will be done with timeout instead of interrupt to ensure correct transmission before trying to read
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
    while (bytes_read < length)
    {
    	uint8_t byte;
    	if (ringbuffer_get(&rx_ringbuffer, &byte))
    	{
    		bytes[bytes_read++] = byte;
    	}
    	else
    	{
    		break;
    	}
    }
    return bytes_read;
}

static int peekStm32(void)
{
    if (serialState != OPEN)
    {
        return -1;
    }

    return (rx_ringbuffer.head - rx_ringbuffer.tail + RX_BUFFER_SIZE) % RX_BUFFER_SIZE;
}

#endif // defined(USE_STM32_HAL)
