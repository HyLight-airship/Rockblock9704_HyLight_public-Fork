#include "rockblock_9704.h"
#include "jspr_command.h"
#include "serial.h"
#include "imt_queue.h"

#include "third_party/cJSON/cJSON.h"
#include "third_party/base64/base64.h"
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include "crossplatform.h"

#if defined(_WIN32)
#include <io.h>
#define access _access
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

// =============================================================================
// CONSTANTS AND DEFINITIONS
// =============================================================================

/**
 * @brief Minimum allowed topic ID for IMT messaging
 * 
 * Defines the lower bound for valid topic identifiers in the
 * Iridium messaging protocol.
 */
#define IMT_MIN_TOPIC_ID 64U

/**
 * @brief Maximum allowed topic ID for IMT messaging
 * 
 * Defines the upper bound for valid topic identifiers in the
 * Iridium messaging protocol.
 */
#define IMT_MAX_TOPIC_ID 65535U

/**
 * @brief Length of firmware version string
 * 
 * Maximum length for storing firmware version information
 * including major, minor, and patch numbers.
 */
#define FIRMWARE_VERSION_STRING_LEN 13U

#ifndef SERIAL_CONTEXT_SETUP_FUNC
    #error A serial context function is needed
#endif

// =============================================================================
// EXTERNAL VARIABLES AND REFERENCES
// =============================================================================

/**
 * @brief External message reference counter
 * 
 * Global counter used to provide unique request references
 * for message originate commands.
 */
extern int messageReference;

/**
 * @brief External serial communication context
 * 
 * Contains function pointers for serial port operations
 * including initialization, read, write, and deinitialization.
 */
extern serialContext context;

/**
 * @brief External serial state enumeration
 * 
 * Tracks the current state of the serial communication
 * (OPEN, CLOSED, etc.).
 */
extern enum serialState serialState;

// =============================================================================
// STATIC VARIABLES AND BUFFERS
// =============================================================================

/**
 * @brief Static buffer for base64 encoding operations
 * 
 * Temporary buffer used during base64 encoding and decoding
 * operations to avoid stack allocation of large buffers.
 */
static uint8_t base64Buffer [BASE64_TEMP_BUFFER];

/**
 * @brief Static buffer for CRC calculations
 * 
 * Buffer used to store data during CRC16 calculations
 * for message integrity verification.
 */
static uint8_t crcBuffer [IMT_CRC_SIZE];

/**
 * @brief Static buffer for firmware version string
 * 
 * Buffer used to store the firmware version information
 * retrieved from the modem.
 */
static char firmwareVersion [FIRMWARE_VERSION_STRING_LEN];

// =============================================================================
// GLOBAL MODEM STATE VARIABLES
// =============================================================================

/**
 * @brief Hardware information structure
 * 
 * Stores hardware information retrieved from the modem including
 * version, serial number, and IMEI.
 */
jsprHwInfo_t hwInfo;

/**
 * @brief SIM status information structure
 * 
 * Stores SIM card status information including card presence,
 * connection status, and ICCID.
 */
jsprSimStatus_t simStatus;

/**
 * @brief Firmware information structure
 * 
 * Stores firmware information including version details,
 * validity, and hash values.
 */
jsprFirmwareInfo_t firmwareInfo;

/**
 * @brief Message provisioning information structure
 * 
 * Stores message provisioning configuration including
 * topic lists and their configurations.
 */
jsprMessageProvisioning_t messageProvisioningInfo;

/**
 * @brief Asynchronous message length
 * 
 * Stores the length of messages received asynchronously
 * from the modem.
 */
uint32_t messageLengthAsync = 0;

/**
 * @brief Count of queued Mobile-Originated messages
 * 
 * Tracks the number of outgoing messages currently
 * queued for transmission.
 */
uint16_t moQueuedMessages = 0;

/**
 * @brief Count of queued Mobile-Terminated messages
 * 
 * Tracks the number of incoming messages currently
 * queued for processing.
 */
uint16_t mtQueuedMessages = 0;

/**
 * @brief Receive lock flag
 * 
 * When set, prevents new messages from being received
 * to avoid interrupting ongoing operations.
 */
bool Receivelock = false;

/**
 * @brief Mobile-Originated message dropped flag
 * 
 * Indicates that an outgoing message was dropped
 * due to queue overflow or other conditions.
 */
bool moDropped = false;

/**
 * @brief Mobile-Originated message sent flag
 * 
 * Indicates that an outgoing message was successfully
 * transmitted to the modem.
 */
bool moSent = false;

/**
 * @brief Mobile-Terminated message dropped flag
 * 
 * Indicates that an incoming message was dropped
 * due to queue overflow or other conditions.
 */
bool mtDropped = false;

/**
 * @brief Mobile-Terminated message received flag
 * 
 * Indicates that an incoming message was successfully
 * received from the modem.
 */
bool mtReceived = false;

/**
 * @brief Static pointer to callback functions
 * 
 * Points to the registered callback structure containing
 * function pointers for various RockBLOCK library events.
 */
static const rbCallbacks_t *rbCallbacks = NULL;

// =============================================================================
// CALLBACK REGISTRATION FUNCTIONS
// =============================================================================

/**
 * @brief Register callback functions for RockBLOCK library events
 * 
 * Stores a pointer to the callback structure containing function
 * pointers for various events such as message provisioning,
 * message reception, and status updates.
 * 
 * @param callbacks Pointer to the callback structure to register
 */
void rbRegisterCallbacks(const rbCallbacks_t *callbacks)
{
    if (callbacks)
    {
        rbCallbacks = callbacks;
    }
}

// =============================================================================
// GPIO-BASED MODEM CONTROL FUNCTIONS
// =============================================================================

/**
 * @brief Initialize the RockBLOCK modem using GPIO control
 * 
 * Performs the complete power-on sequence for the modem:
 * 1. Drive power enable pin LOW to power on
 * 2. Drive iridium enable pin HIGH to activate modem
 * 3. Wait for boot signal from modem
 * 4. Handle boot messages
 * 5. Initialize serial communication
 * 
 * This function is only available when RB_GPIO is defined.
 * 
 * @param port Serial port name (ignored on STM32)
 * @param gpioInfo Pointer to GPIO configuration structure
 * @param timeout Maximum time to wait for boot signal in seconds
 * @return true if the modem successfully initialized, false otherwise
 */
#ifdef RB_GPIO
bool rbBeginGpio(char * port, const rbGpioTable_t * gpioInfo, const int timeout)
{
    bool enabled = false;
#if defined(USE_STM32_HAL)
    // This is the corrected STM32 implementation.
    // It now includes the power-on sequence before waiting for the boot signal.
    if (gpioDriveLow(gpioInfo->powerEnable.port, gpioInfo->powerEnable.pin))
    {
        if (gpioDriveHigh(gpioInfo->iridiumEnable.port, gpioInfo->iridiumEnable.pin))
        {
            if (gpioListenIridBooted(gpioInfo->booted.port, gpioInfo->booted.pin, timeout))
            {
                // --- Boot message handling logic ---
                uint32_t startTime = HAL_GetTick();
                bool gotBootInfo = false;
                bool gotOperationalState = false;

                if(context.serialInit())
                {
                    serialState = OPEN;
                    jsprResponse_t* response_ptr;

                    while ((HAL_GetTick() - startTime < 5000) && !(gotBootInfo && gotOperationalState))
                    {
                        response_ptr = receiveJspr(100);
                        if (response_ptr != NULL)
                        {
                            if (response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE)
                            {
                                if (strcmp(response_ptr->target, "bootInfo") == 0)
                                {
                                    gotBootInfo = true;
                                    printf("Modem sent bootInfo.\r\n");
                                }
                                else if (strcmp(response_ptr->target, "operationalState") == 0)
                                {
                                    gotOperationalState = true;
                                    printf("Modem sent operationalState.\r\n");
                                }
                            }
                        }
                    }

                    if(context.serialDeInit()) {
                        serialState = CLOSED;
                    }
                }

                if (rbBegin(port))
                {
                    enabled = true;
                }
            }
        }
    }
#else
    // Original implementation for Linux/other platforms
    if (gpioDriveLow(gpioInfo->powerEnable.chip, gpioInfo->powerEnable.pin))
    {
        if (gpioDriveHigh(gpioInfo->iridiumEnable.chip, gpioInfo->iridiumEnable.pin))
        {
            if (gpioListenIridBooted(gpioInfo->booted.chip, gpioInfo->booted.pin, timeout))
            {
                sleep(1);
                if (rbBegin(port))
                {
                    enabled = true;
                }
            }
        }
    }
#endif
    return enabled;
}

/**
 * @brief De-initialize the RockBLOCK modem using GPIO control
 * 
 * Performs the complete power-off sequence for the modem:
 * 1. Drive iridium enable pin LOW to deactivate modem
 * 2. Drive power enable pin HIGH to power off
 * 
 * This function is only available when RB_GPIO is defined.
 * 
 * @param gpioInfo Pointer to GPIO configuration structure
 * @return true if the modem was successfully deinitialized, false otherwise
 */
bool rbEndGpio(const rbGpioTable_t * gpioInfo)
{
    bool disabled = false;
#if defined(USE_STM32_HAL)
    // STM32 implementation using .port and .pin
    if (gpioDriveHigh(gpioInfo->powerEnable.port, gpioInfo->powerEnable.pin))
    {
        if (gpioDriveLow(gpioInfo->iridiumEnable.port, gpioInfo->iridiumEnable.pin))
        {
            if (rbEnd())
            {
                disabled = true;
            }
        }
    }
#else
    // Original implementation for Linux/other platforms
    if (gpioDriveHigh(gpioInfo->powerEnable.chip, gpioInfo->powerEnable.pin))
    {
        if (gpioDriveLow(gpioInfo->iridiumEnable.chip, gpioInfo->iridiumEnable.pin))
        {
            if (rbEnd())
            {
                disabled = true;
            }
        }
    }
#endif
    return disabled;
}
#endif // RB_GPIO

// =============================================================================
// CRC16 CALCULATION FUNCTIONS
// =============================================================================

/**
 * @brief CRC16 lookup table for polynomial 0x1021
 * 
 * Pre-computed CRC16 values for all possible byte values using
 * the polynomial 0x1021 (CRC-16-CCITT). This table is used for
 * fast CRC calculation during message integrity verification.
 */
static const uint16_t CRC16Table[256] =
{
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
  0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
  0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
  0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
  0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
  0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
  0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
  0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
  0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
  0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
  0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
  0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
  0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
  0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
  0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
  0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
  0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
  0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

// =============================================================================
// MODEM CONFIGURATION FUNCTIONS
// =============================================================================

/**
 * @brief Set the API version for the modem
 * 
 * Sends the API version command to the modem and waits for the response.
 * If no active version is set, it sets the first supported version.
 * This function is called during modem initialization.
 * 
 * @return true if the API version is set successfully, false otherwise
 */
static bool setApi(void)
{
    bool set = false;
    jsprResponse_t* response_ptr;
    uint32_t startTime = millis();

    jsprGetApiVersion();

    while ((millis() - startTime) < 5000)
    {
        response_ptr = receiveJspr(1000);
        if (response_ptr != NULL)
        {
            if (strcmp(response_ptr->target, "apiVersion") == 0 && response_ptr->code == JSPR_RC_NO_ERROR)
            {
                jsprApiVersion_t apiVersion;
                memset(&apiVersion, 0, sizeof(apiVersion));
                parseJsprGetApiVersion(response_ptr->json, &apiVersion);

                if(!apiVersion.activeVersionSet)
                {
                    jsprPutApiVersion(&apiVersion.supportedVersions[0]);
                    response_ptr = receiveJspr(2000);
                    if (response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
                    {
                        set = true;
                    }
                }
                else
                {
                    set = true;
                }
                break;
            }
        }
    }
    return set;
}

/**
 * @brief Set the SIM interface for the modem
 * 
 * Sends the SIM interface command to the modem and waits for the response.
 * If the interface is not set to internal, it configures it to use
 * the internal SIM interface. This function is called during modem initialization.
 * 
 * @return true if the SIM interface is set successfully, false otherwise
 */
static bool setSim(void)
{
    bool set = false;
    jsprResponse_t* response_ptr;
    if(jsprGetSimInterface())
    {
        response_ptr = receiveJspr(2000);
        if (response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
        {
            jsprSimInterface_t simInterface;
            parseJsprGetSimInterface(response_ptr->json, &simInterface);

            if(!simInterface.ifaceSet || simInterface.iface != SIM_INTERNAL)
            {
                putSimInterface(SIM_INTERNAL);
                response_ptr = receiveJspr(2000);
                if (response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR &&
                    (strncmp(response_ptr->target, "simConfig", JSPR_MAX_TARGET_LENGTH) == 0))
                {
                    set = true;
                }
            }
            else if (simInterface.iface == SIM_INTERNAL)
            {
                set = true;
            }
        }
    }
    receiveJspr(200);
    return set;
}

/**
 * @brief Set the operational state for the modem
 * 
 * Sends the operational state command to the modem and waits for the response.
 * If the state is not active, it sets the modem to active state.
 * This function is called during modem initialization.
 * 
 * @return true if the operational state is set successfully, false otherwise
 */
static bool setState(void)
{
    bool set = false;
    jsprResponse_t* response_ptr;
    if(jsprGetOperationalState())
    {
        response_ptr = receiveJspr(2000);
        if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
        {
            jsprOperationalState_t state;
            parseJsprGetOperationalState(response_ptr->json, &state);
            if(state.operationalStateSet)
            {
                if(state.operationalState == ACTIVE)
                {
                    set = true;
                }
                else
                {
                    putOperationalState(ACTIVE);
                    response_ptr = receiveJspr(2000);
                    if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
                    {
                        set = true;
                    }
                }
            }
        }
    }
    return set;
}

// =============================================================================
// MODEM INITIALIZATION FUNCTIONS
// =============================================================================

/**
 * @brief Initialize the RockBLOCK modem
 * 
 * This function sets up the serial communication context, configures the API,
 * SIM, and operational state, and initializes internal message queues.
 * 
 * - On STM32 (USE_STM32_HAL defined), the serial port name is ignored and the
 *   HAL-based serial context is used.
 * - On other platforms, the given port and default baud rate are used to
 *   configure the serial context.
 *
 * If provisioning information is available, the registered callback
 * (rbCallbacks->messageProvisioning) will be invoked.
 * 
 * @param port Serial port name (ignored on STM32)
 * @return true if initialization was successful, false otherwise
 */
#if defined(USE_STM32_HAL)
bool rbBegin(const char* port)
{
    (void)port;
    bool began = false;
    if(context.serialInit != NULL)
    {
        if(context.serialInit())
        {
            serialState = OPEN;
            if(setApi())
            {
                if(setSim())
                {
                    if(setState())
                    {
                        imtQueueInit();
                        if (messageProvisioningInfo.provisioningSet)
                        {
                            if (rbCallbacks && rbCallbacks->messageProvisioning)
                            {
                                rbCallbacks->messageProvisioning(&messageProvisioningInfo);
                            }
                        }
                        began = true;
                    }
                }
            }
        }
    }
    return began;
}
#else
bool rbBegin(const char* port)
{
    bool began = false;
    if(SERIAL_CONTEXT_SETUP_FUNC(port, RB9704_BAUD))
    {
        if(context.serialInit != NULL)
        {
            if(context.serialInit())
            {
                serialState = OPEN;
                if(setApi())
                {
                    if(setSim())
                    {
                        if(setState())
                        {
                            imtQueueInit();
                            began = true;
                        }
                    }
                }
            }
        }
    }
    return began;
}
#endif

// =============================================================================
// BASE64 ENCODING AND DECODING FUNCTIONS
// =============================================================================

/**
 * @brief Encode data using base64 encoding
 * 
 * Encodes the data from srcBuffer to destBuffer.
 * 
 * @param srcBuffer Pointer to the source data buffer.
 * @param srcLength Length of the source data in bytes.
 * @param destBuffer Pointer to the destination buffer for encoded data.
 * @param destLength Maximum length of the destination buffer.
 * @return Number of bytes encoded, or (size_t)-1 on error.
 */
static size_t encodeData(const char * srcBuffer, const size_t srcLength, char * destBuffer, const size_t destLength)
{
    size_t encodedBytes = (size_t)-1;
    if(srcBuffer != NULL && srcLength > 0 && destBuffer != NULL && destLength > 0)
    {
        int err = mbedtls_base64_encode((unsigned char*)destBuffer, destLength, &encodedBytes, (const unsigned char*)srcBuffer, srcLength);
        if (0 != err)
        {
            encodedBytes = (size_t)-1;
        }
    }
    return encodedBytes;
}

/**
 * @brief Decode data using base64 encoding
 * 
 * Decodes the data from srcBuffer to destBuffer.
 * 
 * @param srcBuffer Pointer to the source data buffer (Base64 encoded).
 * @param srcLength Length of the source data in bytes (including padding).
 * @param destBuffer Pointer to the destination buffer for decoded data.
 * @param destLength Maximum length of the destination buffer.
 * @return Number of bytes decoded, or (size_t)-1 on error.
 */
static size_t decodeData(const char * srcBuffer, const size_t srcLength, char * destBuffer, const size_t destLength)
{
    size_t decodedBytes = (size_t)-1;
    if(srcBuffer != NULL && srcLength > 0 && destBuffer != NULL && destLength > 0)
    {
        int err = mbedtls_base64_decode((unsigned char*)destBuffer, destLength, &decodedBytes, (const unsigned char*)srcBuffer, srcLength);
        if (0 != err)
        {
            decodedBytes = (size_t)-1;
        }
    }
    return decodedBytes;
}

// =============================================================================
// CRC16 CALCULATION FUNCTIONS
// =============================================================================

/**
 * @brief Append CRC to the data
 * 
 * Appends the CRC to the data.
 * 
 * @param buffer Pointer to the buffer to which CRC will be appended.
 * @param length Current length of the buffer.
 * @return true if CRC was appended successfully, false otherwise.
 */
static bool appendCrc(uint8_t * buffer, size_t length)
{
    bool appended = false;
    uint16_t crc = calculateCrc(buffer, length, 0);
    crcBuffer[0] = (crc >> 8) & 0xFFU;
    crcBuffer[1] = crc & 0xFFU;
    memcpy(buffer + length, crcBuffer, IMT_CRC_SIZE);
    appended = true;
    return appended;
}

// =============================================================================
// MESSAGE SENDING FUNCTIONS
// =============================================================================

/**
 * @brief Send a message to the modem using the Cloudloop topic
 * 
 * Steps performed:
 * 1. Check if the topic is provisioned (allowed to send messages).
 * 2. Add the message to the MO queue.
 * 3. Attempt to send queued messages within the given timeout.
 * 
 * @param topic Cloudloop topic to send the message to.
 * @param data Pointer to the message data.
 * @param length Length of the message data.
 * @param timeout Maximum time to wait for the message to be sent in seconds.
 * @return true if the message was sent successfully, false otherwise.
 */
bool rbSendMessageCloudloop(cloudloopTopics_t topic, const char * data, const size_t length, const int timeout)
{
    bool sent = false;
    if(checkProvisioning(topic))
    {
        if(imtQueueMoAdd(topic, data, length))
        {
            sent = sendMoFromQueue(timeout);
        }
    }
    return sent;
}

/**
 * @brief Send a message to the modem using any topic
 * 
 * Steps performed:
 * 1. Check if the topic is provisioned (allowed to send messages).
 * 2. Add the message to the MO queue.
 * 3. Attempt to send queued messages within the given timeout.
 * 
 * @param topic Topic ID to send the message to.
 * @param data Pointer to the message data.
 * @param length Length of the message data.
 * @param timeout Maximum time to wait for the message to be sent in seconds.
 * @return true if the message was sent successfully, false otherwise.
 */
bool rbSendMessageAny(uint16_t topic, const char * data, const size_t length, const int timeout)
{
    bool sent = false;
    if(checkProvisioning(topic))
    {
        if(imtQueueMoAdd(topic, data, length))
        {
            sent = sendMoFromQueue(timeout);
        }
    }
    return sent;
}

/**
 * @brief Send a message from the MO queue
 * 
 * Steps performed:
 * 1. Check if the message is valid.
 * 2. Append the CRC to the message.
 * 3. Send the message to the modem.
 * 4. Wait for the response from the modem.
 * 
 * @param timeout Maximum time to wait for the message to be sent in seconds.
 * @return true if the message is sent successfully, false otherwise.
 */
static bool sendMoFromQueue(const int timeout)
{
    bool sent = false;
    bool started = false;
    unsigned long start = millis();
    jsprResponse_t* response_ptr;
    imt_t * imtMo = imtQueueMoGetFirst();

    if(imtMo != NULL)
    {
        if(appendCrc(imtMo->buffer, imtMo->length))
        {
            if(imtMo->buffer != NULL && imtMo->length > 0 && imtMo->topic >= IMT_MIN_TOPIC_ID
            && imtMo->topic <= IMT_MAX_TOPIC_ID)
            {
                if(jsprPutMessageOriginate(imtMo->topic, imtMo->length + IMT_CRC_SIZE))
                {
                    response_ptr = receiveJspr(2000);
                    if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
                    {
                        jsprMessageOriginate_t messageOriginate;
                        parseJsprPutMessageOriginate(response_ptr->json, &messageOriginate);
                        imtMo->id = messageOriginate.messageId;
                        started = true;
                        while (true)
                        {
                            rbPoll();
                            if(moDropped)
                            {
                                sent = false;
                                moDropped = false;
                                break;
                            }
                            else if (moSent)
                            {
                                sent = true;
                                moSent = false;
                                break;
                            }
                            else if ((millis() - start) >= (timeout * 1000UL))
                            {
                                sent = false;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if(!started)
        {
            imtQueueMoRemove();
        }
    }
    return sent;
}

// =============================================================================
// MESSAGE RECEIVING FUNCTIONS
// =============================================================================

/**
 * @brief Receive a message from the modem
 * 
 * Steps performed:
 * 1. Check if the modem is listening for messages.
 * 2. Get the first message from the MO queue.
 * 3. Check if the message is valid.
 * 4. Copy the message to the buffer.
 * 
 * @param buffer Pointer to a buffer where the message will be stored.
 * @return Length of the message (excluding CRC).
 */
size_t rbReceiveMessage(char ** buffer)
{
    size_t length = 0;

    if(listenForMt())
    {
        imt_t * imtMt = imtQueueMtGetFirst();
        if(buffer != NULL && imtMt != NULL)
        {
            if(imtMt->buffer != NULL && imtMt->length > 0 && imtMt->topic >= IMT_MIN_TOPIC_ID &&
                imtMt->topic <= IMT_MAX_TOPIC_ID)
            {
                length = (imtMt->length - IMT_CRC_SIZE);
                imtMt->buffer[length] = '\0';
                *buffer = (char*)imtMt->buffer;
                imtMt->readyToProcess = false;
            }
        }
    }
    return length;
}

/**
 * @brief Receive a message from the modem with the topic
 * 
 * Steps performed:
 * 1. Check if the modem is listening for messages.
 * 2. Get the first message from the MO queue.
 * 3. Check if the message is valid.
 * 4. Copy the message to the buffer.
 * 
 * @param buffer Pointer to a buffer where the message will be stored.
 * @param topic Pointer to a variable where the topic ID will be stored.
 * @return Length of the message (excluding CRC).
 */
size_t rbReceiveMessageWithTopic(char ** buffer, uint16_t * topic)
{
    size_t length = 0;

    if(listenForMt())
    {
        imt_t * imtMt = imtQueueMtGetFirst();
        if(buffer != NULL && imtMt != NULL)
        {
            if(imtMt->buffer != NULL && imtMt->length > 0 && imtMt->topic >= IMT_MIN_TOPIC_ID &&
                imtMt->topic <= IMT_MAX_TOPIC_ID)
            {
                length = (imtMt->length - IMT_CRC_SIZE);
                imtMt->buffer[length] = '\0';
                *buffer = (char*)imtMt->buffer;
                *topic = imtMt->topic;
                imtMt->readyToProcess = false;
            }
        }
    }
    return length;
}

/**
 * @brief Listen for a Mobile-Terminated (MT) message from the modem.
 * 
 * Steps performed:
 * 1. Poll the modem to update message status.
 * 2. Get the first message from the MT queue.
 * 3. Check if the message is ready to process.
 * 4. Wait until the message is either received or dropped.
 * 
 * @return true if an MT message was successfully received, false if dropped or none.
 */
static bool listenForMt(void)
{
    bool received = false;

    rbPoll();
    imt_t * imtMt = imtQueueMtGetFirst();
    if(imtMt != NULL)
    {
        if(imtMt->readyToProcess)
        {
            while(true)
            {
                rbPoll();
                if(mtDropped)
                {
                    received = false;
                    mtDropped = false;
                    break;
                }
                else if(mtReceived)
                {
                    received = true;
                    mtReceived = false;
                    break;
                }
            }
        }
    }
    return received;
}

// =============================================================================
// ASYNCHRONOUS MESSAGE SENDING FUNCTIONS
// =============================================================================

/**
 * @brief Start sending the first Mobile-Originated (MO) message from the queue asynchronously.
 *
 * This function attempts to send the first message in the MO queue without blocking
 * for the entire transfer. It performs the following steps:
 *
 * 1. Retrieve the first message from the MO queue.
 * 2. Append CRC to the message buffer.
 * 3. Validate the message length, buffer, and topic ID.
 * 4. Send a "message originate" request to the modem once.
 * 5. Listen for up to 3 seconds for the correct response message (ignore others).
 * 6. If the correct response is received:
 *    - Parse the response to get the assigned message ID.
 *    - Mark the sending as started.
 * 7. If sending did not start successfully, remove the message from the queue.
 *
 * @return true if the message sending process was successfully started, false otherwise.
 */
static bool sendMoFromQueueAsync(void)
{
    bool started = false;
    jsprResponse_t* response_ptr;
    imt_t * imtMo = imtQueueMoGetFirst();

    if(imtMo != NULL)
    {
        if(appendCrc(imtMo->buffer, imtMo->length))
        {
            if(imtMo->buffer != NULL && imtMo->length > 0 && imtMo->topic >= IMT_MIN_TOPIC_ID
            && imtMo->topic <= IMT_MAX_TOPIC_ID)
            {
                // Send the request to originate a message ONCE.
                if(jsprPutMessageOriginate(imtMo->topic, imtMo->length + IMT_CRC_SIZE))
                {
                    uint32_t startTime = millis();
                    // Now, listen for up to 3 seconds for the correct response, ignoring others.
                    while ((millis() - startTime) < 3000)
                    {
                        response_ptr = receiveJspr(500);
                        if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR &&
                           strcmp(response_ptr->target, "messageOriginate") == 0)
                        {
                            // This is the correct response. Parse it and set the ID.
                            jsprMessageOriginate_t messageOriginate;
                            parseJsprPutMessageOriginate(response_ptr->json, &messageOriginate);
                            imtMo->id = messageOriginate.messageId;
                            started = true;
                            break; // Success, exit the loop.
                        }
                        // If we received another message, ignore it and continue listening.
                    }
                }
            }
        }

        if(!started)
        {
            imtQueueMoRemove();
        }
    }
    return started;
}

/**
 * @brief Queue a Mobile-Originated (MO) message for asynchronous sending.
 *
 * This function performs the following steps:
 * 1. Checks if the given topic is provisioned (allowed to send messages).
 * 2. Validates that the data pointer is not NULL and the length is within allowed limits.
 * 3. Adds the message to the internal MO queue.
 * 4. If no other messages are queued, starts sending the message asynchronously
 *    using `sendMoFromQueueAsync()`.
 * 5. Increments the count of queued messages (`moQueuedMessages`).
 *
 * @param topic Topic ID to send the message to.
 * @param data Pointer to the message data.
 * @param length Length of the message data.
 * @return true if the message was successfully queued and sending started, false otherwise.
 */
bool rbSendMessageAsync(uint16_t topic, const char * data, const size_t length)
{
    bool queuedToSend = false;
    if(checkProvisioning(topic))
    {
        if(data != NULL && length > 0 && length <= IMT_PAYLOAD_SIZE - IMT_CRC_SIZE)
        {
            if(imtQueueMoAdd(topic, data, length))
            {
                if (moQueuedMessages == 0)
                {
                    queuedToSend = sendMoFromQueueAsync();
                }
                else
                {
                    queuedToSend = true;
                }
                moQueuedMessages += 1;
            }
        }
    }
    return queuedToSend;
}

// =============================================================================
// ASYNCHRONOUS MESSAGE RECEIVING FUNCTIONS
// =============================================================================

/**
 * @brief Retrieve the first ready Mobile-Terminated (MT) message from the queue asynchronously.
 *
 * This function performs the following steps:
 * 1. Gets the first MT message from the queue.
 * 2. Checks if the message is marked as ready.
 * 3. Validates the message buffer, length, and topic ID.
 * 4. If valid, sets the output pointer to the message buffer (excluding CRC).
 * 5. Null-terminates the message for string-safe usage.
 * 6. Marks the message as no longer ready to process.
 *
 * @param buffer Pointer to a buffer where the message will be stored.
 * @return The length of the message (excluding CRC) if a message is ready, 0 otherwise.
 */

size_t rbReceiveMessageAsync(char ** buffer)
{
    size_t length = 0;
    imt_t * imtMt = imtQueueMtGetFirst();

    if(imtMt != NULL)
    {
        if(imtMt->ready)
        {
            if(buffer != NULL)
            {
                if(imtMt->buffer != NULL && imtMt->length > 0 && imtMt->topic >= IMT_MIN_TOPIC_ID &&
                    imtMt->topic <= IMT_MAX_TOPIC_ID && imtMt->ready)
                {
                    length = (imtMt->length - IMT_CRC_SIZE);
                    imtMt->buffer[length] = '\0';
                    *buffer = (char*)imtMt->buffer;
                    imtMt->readyToProcess = false;
                }
            }
        }
    }
    return length;
}

/**
 * @brief Lock the receive queue to prevent new messages from being added.
 * 
 * This function is used to prevent race conditions when receiving messages
 * asynchronously. It locks the receive queue, preventing new messages from
 * being added to the `imtQueueMtAdd` function.
 */
void rbReceiveLockAsync(void)
{
    imtQueueMtLock(true);
}

/**
 * @brief Unlock the receive queue to allow new messages to be added.
 * 
 * This function is used to re-enable the receive queue after it has been
 * locked by `rbReceiveLockAsync`.
 */
void rbReceiveUnlockAsync(void)
{
    imtQueueMtLock(false);
}

/**
 * @brief Acknowledge the receipt of a message from the receive queue.
 * 
 * This function removes the first message from the MT queue and
 * acknowledges its receipt to the modem.
 * 
 * @return true if the message was successfully acknowledged, false otherwise.
 */
bool rbAcknowledgeReceiveHeadAsync(void)
{
    bool acknowledged = false;
    if(imtQueueMtRemove())
    {
        acknowledged = true;
    }
    return acknowledged;
}

// =============================================================================
// ASYNCHRONOUS MESSAGE PROCESSING
// =============================================================================

/**
 * @brief Check if there are any messages in the MO queue that need to be sent.
 * 
 * If there are messages in the MO queue, it attempts to send the first one
 * asynchronously using `sendMoFromQueueAsync()`.
 * 
 * @return true if a message was successfully sent, false otherwise.
 */
static bool checkMoQueue(void)
{
    bool success = false;
    if(moQueuedMessages > 0)
    {
        if(sendMoFromQueueAsync())
        {
            success = true;
        }
    }
    return success;
}

/**
 * @brief Poll the modem and handle incoming and outgoing messages asynchronously.
 *
 * This function is the main polling loop for the messaging system. It performs
 * the following tasks:
 *
 * 1. Calls `receiveJspr()` to check for new JSPR messages from the modem.
 * 2. Processes Mobile-Originated (MO) messages:
 *    - Handles `messageOriginateSegment` unsolicited messages by encoding the
 *      segment in Base64 and sending it back via `jsprPutMessageOriginateSegment`.
 *    - Handles errors in `messageOriginateSegment` by notifying callbacks or
 *      marking the message as dropped.
 *    - Handles `messageOriginateStatus` unsolicited messages to confirm delivery
 *      or detect failure, updating flags or calling callbacks as needed.
 *    - Updates the MO queue and queued message count accordingly.
 * 3. Processes Mobile-Terminated (MT) messages:
 *    - Handles `messageTerminate` messages by adding them to the MT queue and
 *      marking them ready to process.
 *    - Handles `messageTerminateSegment` messages by decoding Base64 segments
 *      into the MT buffer and tracking the total message length.
 *    - Handles `messageTerminateStatus` messages to mark the MT message as complete
 *      or failed, calling the appropriate callbacks or setting flags.
 * 4. Handles unsolicited `constellationState` messages by parsing signal data
 *    and invoking the corresponding callback.
 *
 * The function ensures proper asynchronous message handling, queue management,
 * and integration with user-provided callbacks.
 */
void rbPoll(void)
{
    jsprResponse_t* response_ptr;
    size_t decodedBytes;

    response_ptr = receiveJspr(100);
    if(response_ptr != NULL)
    {
        imt_t * imtMo = imtQueueMoGetFirst();
        //MO JSPR
        if(imtMo != NULL)
        {
            if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageOriginateSegment") == 0)
            {
                jsprMessageOriginateSegment_t messageOriginateSegment;
                parseJsprUnsMessageOriginateSegment(response_ptr->json, &messageOriginateSegment);
                if(messageOriginateSegment.messageId == imtMo->id &&
                messageOriginateSegment.topic == imtMo->topic)
                {
                    size_t encodedBytes = encodeData((char*)imtMo->buffer + messageOriginateSegment.segmentStart,
                    messageOriginateSegment.segmentLength, (char*)base64Buffer, BASE64_TEMP_BUFFER);
                    if(0 < encodedBytes)
                    {
                        jsprMessageOriginate_t messageOriginate;
                        messageOriginate.messageId = imtMo->id;
                        messageOriginate.topic = imtMo->topic;
                        jsprPutMessageOriginateSegment(&messageOriginate, messageOriginateSegment.segmentLength,
                        messageOriginateSegment.segmentStart, (char*)base64Buffer);
                    }
                }
            }
            if(response_ptr->code != JSPR_RC_NO_ERROR && response_ptr->code != JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageOriginateSegment") == 0)
            {
                if(rbCallbacks && rbCallbacks->moMessageComplete)
                {
                    rbCallbacks->moMessageComplete(imtMo->id, RB_MSG_STATUS_FAIL);
                }
                else
                {
                    moDropped = true;
                }
                imtQueueMoRemove();
                moQueuedMessages -= 1;
                checkMoQueue();
            }
            if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageOriginateStatus") == 0)
            {
                jsprMessageOriginateStatus_t messageOriginateStatus;
                if(parseJsprUnsMessageOriginateStatus(response_ptr->json, &messageOriginateStatus))
                {
                    if(imtMo->id == messageOriginateStatus.messageId)
                    {
                        if(messageOriginateStatus.finalMoStatus == MO_ACK_RECEIVED_MOS)
                        {
                            if(rbCallbacks && rbCallbacks->moMessageComplete)
                            {
                                rbCallbacks->moMessageComplete(imtMo->id, RB_MSG_STATUS_OK);
                            }
                            else
                            {
                                moSent = true;
                            }
                        }
                        else
                        {
                            if(rbCallbacks && rbCallbacks->moMessageComplete)
                            {
                                rbCallbacks->moMessageComplete(imtMo->id, RB_MSG_STATUS_FAIL);
                            }
                            else
                            {
                                moDropped = true;
                            }
                        }
                    }
                }
                imtQueueMoRemove();
                moQueuedMessages -= 1;
                checkMoQueue();
            }
        }
        //MT JSPR
        if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageTerminate") == 0)
        {
            jsprMessageTerminate_t messageTerminate;
            parseJsprUnsMessageTerminate(response_ptr->json, &messageTerminate);
            if (imtQueueMtAdd(messageTerminate.topic, messageTerminate.messageId, messageTerminate.messageLengthMax))
            {
                imt_t * imtMt = imtQueueMtGetLast();
                if(imtMt != NULL)
                {
                    imtMt->readyToProcess = true;
                }
            }
            else
            {
                if(rbCallbacks && rbCallbacks->mtMessageComplete)
                {
                    rbCallbacks->mtMessageComplete(messageTerminate.messageId, RB_MSG_STATUS_FAIL);
                }
            }
        }
        if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageTerminateSegment") == 0)
        {
            imt_t * imtMt = imtQueueMtGetLast();
            if(imtMt != NULL)
            {
                if(imtMt->readyToProcess)
                {
                    jsprMessageTerminateSegment_t messageTerminateSegment;
                    parseJsprUnsMessageTerminateSegment(response_ptr->json, &messageTerminateSegment);
                    if(imtMt->id == messageTerminateSegment.messageId)
                    {
                        decodedBytes = decodeData(messageTerminateSegment.data, messageTerminateSegment.dataLength,
                        (char*)imtMt->buffer + messageTerminateSegment.segmentStart, messageTerminateSegment.segmentLength);
                        messageLengthAsync += messageTerminateSegment.segmentLength;
                        if(decodedBytes == (size_t)-1)
                        {
                            if(rbCallbacks && rbCallbacks->mtMessageComplete)
                            {
                                rbCallbacks->mtMessageComplete(imtMt->id, RB_MSG_STATUS_FAIL);
                            }
                            else
                            {
                                mtDropped = true;
                            }
                            imtQueueMtRemove();
                        }
                    }
                }
            }
        }
        if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "messageTerminateStatus") == 0)
        {
            imt_t * imtMt = imtQueueMtGetLast();
            if(imtMt != NULL)
            {
                if(imtMt->readyToProcess)
                {
                    jsprMessageTerminateStatus_t messageTerminateStatus;
                    if(parseJsprUnsMessageTerminateStatus(response_ptr->json, &messageTerminateStatus))
                    {
                        if(imtMt->id == messageTerminateStatus.messageId)
                        {
                            if(messageTerminateStatus.finalMtStatus == COMPLETE)
                            {
                                imtMt->length = messageLengthAsync;
                                messageLengthAsync = 0;
                                imtMt->ready = true;
                                if(rbCallbacks && rbCallbacks->mtMessageComplete)
                                {
                                    rbCallbacks->mtMessageComplete(imtMt->id, RB_MSG_STATUS_OK);
                                }
                                else
                                {
                                    mtReceived = true;
                                }
                            }
                            else
                            {
                                if(rbCallbacks && rbCallbacks->mtMessageComplete)
                                {
                                    rbCallbacks->mtMessageComplete(imtMt->id, RB_MSG_STATUS_FAIL);
                                }
                                else
                                {
                                    mtDropped = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        if(response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE && strcmp(response_ptr->target, "constellationState") == 0)
        {
            jsprConstellationState_t constellationState;
            if(parseJsprGetSignal(response_ptr->json, &constellationState))
            {
                if(rbCallbacks && rbCallbacks->constellationState)
                {
                    rbCallbacks->constellationState(&constellationState);
                }
            }
        }
    }
}

// =============================================================================
// SIGNAL STRENGTH FUNCTIONS
// =============================================================================

/**
 * @brief Retrieve the current signal strength from the modem.
 *
 * This function performs the following steps:
 * 1. Sends a request to the modem to get the current signal (constellation) state.
 * 2. Waits up to 2000 ms for a response using `receiveJspr()`.
 * 3. Checks if the response is valid and corresponds to "constellationState".
 * 4. Parses the JSON response to extract the signal information.
 * 5. Returns the signal strength as an integer representing the number of bars (0–5).
 *
 * @return int8_t Signal strength in bars (0–5). Returns -1 if the signal cannot
 *                be determined or if an error occurs.
 */

int8_t rbGetSignal(void)
{
    int8_t signal = -1;
    jsprResponse_t* response_ptr;
    jsprGetSignal();

    response_ptr = receiveJspr(2000);
    if (response_ptr != NULL)
    {
        if(response_ptr->code == JSPR_RC_NO_ERROR && strcmp(response_ptr->target, "constellationState") == 0)
        {
            jsprConstellationState_t conState;
            if(parseJsprGetSignal(response_ptr->json, &conState))
            {
                if(conState.signalBars >= 0 && conState.signalBars <= 5)
                {
                    signal = conState.signalBars;
                }
            }
        }
    }
    return signal;
}

// =============================================================================
// HARDWARE INFORMATION FUNCTIONS
// =============================================================================

/**
 * @brief Retrieve hardware information from the modem.
 *
 * This function performs the following steps:
 * 1. Sends a request to the modem to get hardware information via `jsprGetHwInfo()`.
 * 2. Waits up to 2000 ms for a response using `receiveJspr()`.
 * 3. Checks if the response is valid and corresponds to "hwInfo".
 * 4. Parses the JSON response into the provided `jsprHwInfo_t` structure.
 * 5. Returns true if the hardware information was successfully retrieved and parsed,
 *    false otherwise.
 *
 * @param hwInfo Pointer to a structure to populate with hardware information.
 * @return true if hardware info was successfully retrieved and parsed, false otherwise.
 */

static bool getHwInfo(jsprHwInfo_t * hwInfo)
{
    bool populated = false;
    jsprResponse_t* response_ptr;
    jsprGetHwInfo();
    response_ptr = receiveJspr(2000);
    if(response_ptr != NULL)
    {
        if(response_ptr->code == JSPR_RC_NO_ERROR && strcmp(response_ptr->target, "hwInfo") == 0)
        {
            if(parseJsprGetHwInfo(response_ptr->json, hwInfo))
            {
                populated = true;
            }
        }
    }
    return populated;
}

/**
 * @brief Retrieve the IMEI of the modem.
 *
 * This function fetches the modem's hardware information using `getHwInfo()`
 * and returns a pointer to the IMEI string stored in the `hwInfo` structure.
 *
 * @return char* Pointer to the modem's IMEI string, or NULL if it cannot be retrieved.
 */

char * rbGetImei(void)
{
    char * imei = NULL;
    if(getHwInfo(&hwInfo))
    {
        imei = hwInfo.imei;
    }
    return imei;
}

/**
 * @brief Retrieve the hardware version of the modem.
 *
 * This function fetches the modem's hardware information using `getHwInfo()`
 * and returns a pointer to the hardware version string stored in the `hwInfo` structure.
 *
 * @return char* Pointer to the modem's hardware version string, or NULL if it cannot be retrieved.
 */
char * rbGetHwVersion(void)
{
    char * hwVersion = NULL;
    if(getHwInfo(&hwInfo))
    {
        hwVersion = hwInfo.hwVersion;
    }
    return hwVersion;
}

/**
 * @brief Retrieve the serial number of the modem.
 *
 * This function fetches the modem's hardware information using `getHwInfo()`
 * and returns a pointer to the serial number string stored in the `hwInfo` structure.
 *
 * @return char* Pointer to the modem's serial number string, or NULL if it cannot be retrieved.
 */
char * rbGetSerialNumber(void)
{
    char * serialNumber = NULL;
    if(getHwInfo(&hwInfo))
    {
        serialNumber = hwInfo.serialNumber;
    }
    return serialNumber;
}

/**
 * @brief Retrieve the current board temperature of the modem.
 *
 * This function fetches the modem's hardware information using `getHwInfo()`
 * and returns the board temperature in degrees Celsius.
 *
 * @return int8_t Board temperature if available; returns -100 if the temperature
 *                cannot be retrieved.
 */
int8_t rbGetBoardTemp(void)
{
    int8_t boardTemp = -100;
    if(getHwInfo(&hwInfo))
    {
        boardTemp = hwInfo.boardTemp;
    }
    return boardTemp;
}

// =============================================================================
// SIM CARD STATUS FUNCTIONS
// =============================================================================

/**
 * @brief Retrieve the SIM card status from the modem.
 *
 * This function performs the following steps:
 * 1. Sends a request to the modem to get SIM status via `jsprGetSimStatus()`.
 * 2. Waits up to 2000 ms for a response using `receiveJspr()`.
 * 3. Checks if the response is valid and corresponds to "simStatus".
 * 4. Parses the JSON response into the provided `jsprSimStatus_t` structure.
 * 5. Returns true if the SIM status was successfully retrieved and parsed,
 *    false otherwise.
 *
 * @param simStatus Pointer to a structure to populate with SIM status information.
 * @return true if SIM status was successfully retrieved and parsed, false otherwise.
 */
static bool getSimStatus(jsprSimStatus_t * simStatus)
{
    bool populated = false;
    jsprResponse_t* response_ptr;
    jsprGetSimStatus();
    response_ptr = receiveJspr(2000);
    if(response_ptr != NULL)
    {
        if(response_ptr->code == JSPR_RC_NO_ERROR && strcmp(response_ptr->target, "simStatus") == 0)
        {
            if(parseJsprGetSimStatus(response_ptr->json, simStatus))
            {
                populated = true;
            }
        }
    }
    return populated;
}

/**
 * @brief Check if a SIM card is present in the modem.
 *
 * This function retrieves the SIM status using `getSimStatus()` and returns
 * whether a SIM card is currently present.
 *
 * @return true if a SIM card is present, false otherwise.
 */
bool rbGetCardPresent(void)
{
    bool cardPresent = false;
    if(getSimStatus(&simStatus))
    {
        cardPresent = simStatus.cardPresent;
    }
    return cardPresent;
}

/**
 * @brief Check if the SIM card is connected to the network.
 *
 * This function retrieves the SIM status using `getSimStatus()` and returns
 * whether the SIM is currently connected.
 *
 * @return true if the SIM is connected, false otherwise.
 */
bool rbGetSimConnected(void)
{
    bool simConnected = false;
    if(getSimStatus(&simStatus))
    {
        simConnected = simStatus.simConnected;
    }
    return simConnected;
}

/**
 * @brief Retrieve the ICCID (Integrated Circuit Card Identifier) of the SIM card.
 *
 * This function fetches the SIM status using `getSimStatus()` and returns a pointer
 * to the ICCID string stored in the `simStatus` structure.
 *
 * @return char* Pointer to the ICCID string, or NULL if it cannot be retrieved.
 */
char * rbGetIccid(void)
{
    char * iccid = NULL;
    if(getSimStatus(&simStatus))
    {
        iccid = simStatus.iccid;
    }
    return iccid;
}

// =============================================================================
// FIRMWARE INFORMATION FUNCTIONS
// =============================================================================

/**
 * @brief Retrieve firmware information from the modem.
 *
 * This function requests the firmware info for the primary boot source using `jsprGetFirmware()`,
 * waits for a response via `receiveJspr()`, and parses it into the provided `fwInfo` structure.
 *
 * @param fwInfo Pointer to a structure to populate with firmware information.
 * @return true if the firmware information was successfully retrieved and parsed, false otherwise.
 */
static bool getFirmwareInfo(jsprFirmwareInfo_t * fwInfo)
{
    bool populated = false;
    jsprResponse_t* response_ptr;
    jsprGetFirmware(JSPR_BOOT_SOURCE_PRIMARY);
    response_ptr = receiveJspr(2000);
    if(response_ptr != NULL)
    {
        if(response_ptr->code == JSPR_RC_NO_ERROR && strcmp(response_ptr->target, "firmware") == 0)
        {
            if(parseJsprFirmwareInfo(response_ptr->json, fwInfo))
            {
                populated = true;
            }
        }
    }
    return populated;
}

/**
 * @brief Retrieve the firmware version string of the modem.
 *
 * This function calls `getFirmwareInfo()` to populate the `firmwareInfo` structure,
 * then formats the version as a string in the form "v<major>.<minor>.<patch>".
 *
 * @return char* Pointer to a static buffer containing the firmware version string.
 *                Returns an empty string if the firmware information could not be retrieved.
 */
char * rbGetFirmwareVersion(void)
{
    if(getFirmwareInfo(&firmwareInfo))
    {
        snprintf(firmwareVersion, FIRMWARE_VERSION_STRING_LEN,"v%u.%u.%u",
            firmwareInfo.versionInfo.version.major,
            firmwareInfo.versionInfo.version.minor,
            firmwareInfo.versionInfo.version.patch);
    }
    else
    {
        firmwareVersion[0] = '\0';
    }

    return firmwareVersion;
}

// =============================================================================
// SERVICE CONFIGURATION FUNCTIONS
// =============================================================================

/**
 * @brief Resynchronize the modem's service configuration.
 *
 * This function ensures that the modem's operational state and service configuration
 * are correctly aligned. The process involves:
 * 1. Checking the current operational state (ACTIVE/INACTIVE).
 * 2. If the modem was ACTIVE, temporarily setting it to INACTIVE.
 * 3. Updating the service configuration using `jsprPutServiceConfig(true)`.
 * 4. Restoring the operational state to ACTIVE if necessary.
 *
 * @return true if the service configuration was successfully resynchronized, false otherwise.
 */
bool rbResyncServiceConfig(void)
{
    bool rVal = false;
    bool isInactive = false;
    bool wasActive = false;
    jsprResponse_t* response_ptr;
    jsprOperationalState_t state;
    uint32_t startTime = millis();

    if(jsprGetOperationalState())
    {
        while ((millis() - startTime) < 2000)
        {
            response_ptr = receiveJspr(500);
            if (response_ptr != NULL)
            {
                if (strcmp(response_ptr->target, "operationalState") == 0)
                {
                    parseJsprGetOperationalState(response_ptr->json, &state);
                    if (state.operationalState == INACTIVE) isInactive = true;
                    if (state.operationalState == ACTIVE) wasActive = true;
                    break;
                }
            }
        }
    }

    if (wasActive == true && isInactive == false)
    {
        putOperationalState(INACTIVE);
        startTime = millis();
        while ((millis() - startTime) < 2000)
        {
             response_ptr = receiveJspr(500);
             if (response_ptr != NULL)
             {
                 if (strcmp(response_ptr->target, "operationalState") == 0)
                 {
                     parseJsprGetOperationalState(response_ptr->json, &state);
                     if (state.operationalState == INACTIVE)
                     {
                         isInactive = true;
                         break;
                     }
                 }
             }
        }
    }

    if (isInactive == true)
    {
        if (jsprPutServiceConfig(true) == true)
        {
            startTime = millis();
            while ((millis() - startTime) < 2000)
            {
                response_ptr = receiveJspr(500);
                if (response_ptr != NULL)
                {
                    if (strcmp(response_ptr->target, "serviceConfig") == 0 && response_ptr->code == JSPR_RC_NO_ERROR)
                    {
                        if (wasActive != true) rVal = true;
                        break;
                    }
                }
            }
        }
    }

    if (rVal == false && wasActive == true)
    {
        putOperationalState(ACTIVE);
        startTime = millis();
        while ((millis() - startTime) < 2000)
        {
            response_ptr = receiveJspr(500);
            if (response_ptr != NULL)
            {
                if (strcmp(response_ptr->target, "operationalState") == 0)
                {
                    parseJsprGetOperationalState(response_ptr->json, &state);
                    if (state.operationalState == ACTIVE)
                    {
                        rVal = true;
                        break;
                    }
                }
            }
        }
    }
    return rVal;
}

// =============================================================================
// CRC16 CALCULATION FUNCTIONS
// =============================================================================

/**
 * @brief Calculate a 16-bit CRC for a given buffer using a precomputed table.
 *
 * This function computes the CRC16 of the input buffer using the provided
 * initial CRC value. The algorithm iterates over each byte of the buffer,
 * updating the CRC based on the current byte and a lookup table (CRC16Table).
 *
 * @param buffer Pointer to the input buffer.
 * @param bufferLength Length of the input buffer.
 * @param initialCRC Initial CRC value (usually 0).
 * @return uint16_t The computed 16-bit CRC value.
 */
static uint16_t calculateCrc(const uint8_t * buffer, const size_t bufferLength, const uint16_t initialCRC)
{
    uint16_t crc = (uint16_t)initialCRC;
    uint8_t data = 0;
    size_t tableIndex = 0;
    if (buffer != 0)
    {
        for (size_t i = 0; i < bufferLength; i++)
        {
            data = buffer[i];
            tableIndex = (((crc >> 8) ^ data) & 0xFF);
            crc = (((crc << 8) ^ CRC16Table[tableIndex]) & 0xFFFF);
        }
    }
    return (crc);
}

// =============================================================================
// SERIAL INTERFACE CONTROL FUNCTIONS
// =============================================================================

/**
 * @brief Deinitialize the serial interface and close the connection.
 *
 * Calls the serial deinitialization function from the context. If successful,
 * marks the serial state as CLOSED.
 *
 * @return true if the serial interface was successfully deinitialized, false otherwise.
 */
bool rbEnd(void)
{
    bool deinitialised = false;
    if(context.serialDeInit())
    {
        deinitialised = true;
        serialState = CLOSED;
    }
    return deinitialised;
}

// =============================================================================
// PROVISIONING CHECK FUNCTIONS
// =============================================================================

/**
 * @brief Check if a topic is provisioned (allowed to send messages).
 * 
 * This function checks if the given topic is configured in the modem's
 * message provisioning list. If not, it attempts to retrieve the provisioning
 * list from the modem and check again.
 * 
 * @param topic Topic ID to check.
 * @return true if the topic is provisioned, false otherwise.
 */
static bool checkProvisioning(uint16_t topic)
{
    bool provisioned = false;
    int count = 0;

    if (messageProvisioningInfo.provisioningSet)
    {
        count = messageProvisioningInfo.topicCount;
        for (int i = 0; i < count; i++)
        {
            if(messageProvisioningInfo.provisioning[i].topicId == topic)
            {
                return true;
            }
        }
    }

    if (jsprGetMessageProvisioning())
    {
        jsprResponse_t* response_ptr;
        uint32_t startTime = millis();

        while ((millis() - startTime) < 5000)
        {
            response_ptr = receiveJspr(500);
            if (response_ptr != NULL)
            {
                if (JSPR_RC_NO_ERROR == response_ptr->code && strcmp(response_ptr->target, "messageProvisioning") == 0)
                {
                    jsprMessageProvisioning_t messageProvisioning;
                    if (parseJsprGetMessageProvisioning(response_ptr->json, &messageProvisioning))
                    {
                        messageProvisioningInfo = messageProvisioning;
                        count = messageProvisioning.topicCount;
                        if (count > 0)
                        {
                            for (int i = 0; i < count; i++)
                            {
                                if (messageProvisioning.provisioning[i].topicId == topic)
                                {
                                    provisioned = true;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    return provisioned;
}

// =============================================================================
// KERMIT FIRMWARE UPDATE FUNCTIONS
// =============================================================================

#if defined(KERMIT)
#include "kermit_io.h"

struct k_data kermitData;
struct k_response kermitResponse;
int kermitStatus = 0;
unsigned char i_buf[IBUFLEN+8];

/**
 * @brief Update the modem firmware using the Kermit protocol.
 *
 * This function performs a firmware update for the device by:
 * 1. Checking the file size of the provided firmware file.
 * 2. Ensuring the modem is in an INACTIVE operational state.
 * 3. Switching the modem to Kermit boot mode if necessary.
 * 4. Initializing the Kermit transfer structures and callbacks.
 * 5. Sending the firmware file using the Kermit protocol and monitoring progress.
 * 6. Reporting progress via the provided callback function.
 *
 * @param firmwareFile Path to the firmware file to be updated.
 * @param progress Callback function to report update progress.
 * @param context Context pointer to pass to the progress callback.
 * @return true if the firmware update was successfully completed, false otherwise.
 */
bool rbUpdateFirmware (const char * firmwareFile, updateProgressCallback progress, void * context)
{
    const char * firmwareFileList[2] = {firmwareFile, NULL};
    unsigned char *inputBufferPtr = (unsigned char *)0;
    short receiveSlot = 0;
    int kermitRxLength = 0;
    void * contextPtr = context;

    jsprResponse_t* response_ptr;
    jsprOperationalState_t state;
    jsprFirmwareInfo_t firmware;
    jsprBootInfo_t bootInfo;

    bool kermitDone = false;
    bool isInactive = false;
    bool isInKermitMode = false;
    bool firmwareUpdated = false;

    memset(&kermitData, 0, sizeof(kermitData));
    memset(&kermitResponse, 0, sizeof(kermitResponse));

    const long filesize = kermit_io_filesize(firmwareFile);
    if (filesize <= 0)
    {
        return firmwareUpdated;
    }

    if (jsprGetOperationalState())
    {
        response_ptr = receiveJspr(1000);
        if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
        {
            parseJsprGetOperationalState(response_ptr->json, &state);
            if (state.operationalState != INACTIVE)
            {
                putOperationalState(INACTIVE);
                response_ptr = receiveJspr(1000);
                if (response_ptr != NULL && response_ptr->code == JSPR_RC_UNSOLICITED_MESSAGE)
                {
                    parseJsprGetOperationalState(response_ptr->json, &state);
                    isInactive = state.operationalState == INACTIVE;
                }
            }

            if (state.operationalStateSet == true)
            {
                isInactive = state.operationalState == INACTIVE;
            }
        }
    }

    if (isInactive == true)
    {
        if (jsprPutFirmware(JSPR_BOOT_SOURCE_PRIMARY))
        {
            response_ptr = receiveJspr(2000);
            if(response_ptr != NULL && response_ptr->code == JSPR_RC_NO_ERROR)
            {
                isInKermitMode = parseJsprFirmwareInfo(response_ptr->json, &firmware);
            }
        }
    }

    if (isInKermitMode == true)
    {;
        kermit_io_init_string();
        delay(1000);
        kermitData.xfermode = 0;
        kermitData.remote = 0;
        kermitData.binary = 1;
        kermitData.parity = PAR_NONE;
        kermitData.bct = 1;
        kermitData.ikeep = OFF;
        kermitData.filelist = (unsigned char **)&firmwareFileList;
        kermitData.cancel = 0;
        kermitData.zinbuf = i_buf;
        kermitData.zinlen = IBUFLEN;
        kermitData.zincnt = 0;
        kermitData.obuf = (unsigned char *)0;
        kermitData.obuflen = 0;
        kermitData.obufpos = 0;
        kermitData.rxd    = kermit_io_readpkt;
        kermitData.txd    = kermit_io_tx_data;
        kermitData.ixd    = kermit_io_inchk;
        kermitData.openf  = kermit_io_openfile;
        kermitData.finfo  = 0;
        kermitData.readf  = kermit_io_readfile;
        kermitData.writef = 0;
        kermitData.closef = kermit_io_closefile;
        kermitData.dbf    = 0;
        kermitStatus = kermit(K_INIT, &kermitData, 0, 0, 0, &kermitResponse);
        if (kermitStatus == SUCCESS)
        {
            kermitResponse.filesize = filesize;

            kermitStatus = kermit(K_SEND, &kermitData, 0, 0, 0, &kermitResponse);
            if (kermitStatus == SUCCESS)
            {
                while (kermitStatus != X_RC_DONE)
                {
                    inputBufferPtr = (unsigned char *)0;
                    receiveSlot = -1;
                    kermitRxLength = 0;

                    if (kermitData.ixd(&kermitData) > 0)
                    {
                        inputBufferPtr = getrslot(&kermitData, &receiveSlot);
                        kermitRxLength = kermitData.rxd(&kermitData, inputBufferPtr, P_PKTLEN);

                        if (kermitRxLength < 1)
                        {
                            freesslot(&kermitData, receiveSlot);
                            break;
                        }
                    }

                    kermitStatus = kermit(K_RUN, &kermitData, receiveSlot, kermitRxLength, 0, &kermitResponse);
                    switch (kermitStatus)
                    {
                        case X_RC_OK:
                            if ((kermitResponse.status == S_DATA) && (progress != NULL))
                            {
                                progress(contextPtr, kermitResponse.sofar, kermitResponse.filesize);
                            }
                        break;
                        case X_RC_DONE:
                            kermitDone = true;
                        break;
                        case X_RC_ERROR:
                        break;
                    }
                }
            }
        }
    }

    if (kermitDone == true)
    {
        firmwareUpdated = true;
    }
    return firmwareUpdated;
}
#endif
