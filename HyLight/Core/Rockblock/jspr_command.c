#include "jspr_command.h"
#include "serial.h"
#include <stdlib.h>
#include <string.h>

// =============================================================================
// EXTERNAL VARIABLES AND CONSTANTS
// =============================================================================

/**
 * @brief External serial context containing function pointers for serial communication
 * 
 * This context provides the interface for writing to the serial port
 * through platform-specific implementations.
 */
extern serialContext context;

/**
 * @brief Global message reference counter for message originate commands
 * 
 * Used to provide unique request references (1-100) for message originate
 * commands. Increments with each use and wraps around after reaching 100.
 */
extern int messageReference;

/**
 * @brief Static buffer for formatting JSPR command strings
 * 
 * Used to format command strings before sending them to the modem.
 * This buffer is reused for all commands to avoid stack allocation.
 */
static uint8_t jsprCommandBuffer [COMMAND_MAX_LEN];

// =============================================================================
// COMMAND LENGTH CONSTANTS
// =============================================================================

/**
 * @brief Predefined lengths of JSPR command strings
 * 
 * These constants define the exact length of each command string and are
 * used to verify that the correct number of bytes were sent.
 */
#define JSPR_GET_API_LEN 18U
#define JSPR_GET_SIM_CONFIG_LEN 17U
#define JSPR_GET_OPERATIONAL_STATE_LEN 24U
#define JSPR_GET_SIGNAL_LEN 26U
#define JSPR_GET_MESSAGE_PROVISIONING_LEN 27U
#define JSPR_GET_HW_INFO_LEN 14U
#define JSPR_PUT_FIRMWARE_LEN 17U
#define JSPR_BOOT_SOURCE_STR_LEN 9U
#define JSPR_GET_FIRMWARE_LEN 16U
#define JSPR_GET_SIM_STATUS_LEN 17U

// =============================================================================
// API VERSION COMMANDS
// =============================================================================

/**
 * @brief Send command to retrieve the current API version
 * 
 * Sends a GET request to the modem to retrieve the current API version
 * information. The response will contain version details in JSON format.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetApiVersion(void)
{
    bool rVal = false;
    const char getApiVersionStr[JSPR_GET_API_LEN] = "GET apiVersion {}\r";
    if (context.serialWrite != NULL)
    {
        if(sendJspr(getApiVersionStr, JSPR_GET_API_LEN) == JSPR_GET_API_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Send command to set the API version
 * 
 * Sends a PUT request to the modem to set the active API version.
 * The version is specified using major, minor, and patch numbers.
 * 
 * @param apiVersion Pointer to structure containing version information
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutApiVersion(const jsprDottedVersion_t * apiVersion)
{
    bool rVal = false;
    int rc = 0;
    // Format command string into buffer
    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer),
            "PUT apiVersion {\"active_version\": {\"major\": %d, \"minor\": %d, \"patch\": %d}}\r",
            apiVersion->major, apiVersion->minor, apiVersion->patch);

    if (rc > 0)
    {
        const size_t putApiVersionStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putApiVersionStrLen) == putApiVersionStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

// =============================================================================
// SIM INTERFACE COMMANDS
// =============================================================================

/**
 * @brief Helper wrapper to select SIM interface type
 * 
 * Converts the enumerated SIM interface type to the corresponding string
 * and calls jsprPutSimInterface() with the correct parameter. This
 * provides a type-safe way to configure the SIM interface.
 * 
 * @param iface Enumerated SIM interface type (none, local, remote, internal)
 * @return true if command was sent successfully, false otherwise
 */
bool putSimInterface(const availableSimInterfaces_t iface)
{
    bool rVal = false;

    switch (iface)
    {
        case SIM_NONE:
            rVal =  jsprPutSimInterface("none");
        break;

        case SIM_LOCAL:
            rVal =  jsprPutSimInterface("local");
        break;

        case SIM_REMOTE:
            rVal =  jsprPutSimInterface("remote");
        break;

        case SIM_INTERNAL:
        // Fall through
        default:
            rVal =  jsprPutSimInterface("internal");
        break;
    }

    return rVal;
}

/**
 * @brief Send command to retrieve current SIM configuration
 * 
 * Sends a GET request to the modem to retrieve the current SIM interface
 * configuration. The response will indicate which SIM interface is active.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetSimInterface(void)
{
    bool rVal = false;
    const char getSimInterfaceStr[JSPR_GET_SIM_CONFIG_LEN] = "GET simConfig {}\r";
    if (context.serialWrite != NULL)
    {
        if(sendJspr(getSimInterfaceStr, JSPR_GET_SIM_CONFIG_LEN) == JSPR_GET_SIM_CONFIG_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Send command to set SIM interface type
 * 
 * Sends a PUT request to the modem to configure the SIM interface type.
 * The interface parameter specifies which SIM interface to use.
 * 
 * @param iface String specifying the SIM interface type ("none", "local", "remote", "internal")
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutSimInterface(const char * iface)
{
    bool rVal = false;
    int rc = 0;

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer),
            "PUT simConfig {\"interface\": \"%s\"}\r", iface);

    if (rc > 0)
    {
        const size_t putSimInterfaceStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putSimInterfaceStrLen) == putSimInterfaceStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

// =============================================================================
// OPERATIONAL STATE COMMANDS
// =============================================================================

/**
 * @brief Helper wrapper to set operational state
 * 
 * Converts the enumerated operational state to the corresponding string
 * and calls jsprPutOperationalState() with the correct parameter. This
 * provides a type-safe way to configure the modem's operational state.
 * 
 * @param state Enumerated operational state (inactive, cal_test, hw_self_test, rf_scan, loopback, fault, active)
 * @return true if command was sent successfully, false otherwise
 */
bool putOperationalState(availableOperationalStates_t state)
{
    bool rVal = false;

    switch (state)
    {
        case INACTIVE:
            rVal = jsprPutOperationalState("inactive");
        break;

        case CAL_TEST:
            rVal = jsprPutOperationalState("cal_test");
        break;

        case HW_SELF_TEST:
            rVal = jsprPutOperationalState("hw_self_test");
        break;

        case RF_SCAN:
            rVal = jsprPutOperationalState("rf_scan");
        break;

        case LOOPBACK:
            rVal = jsprPutOperationalState("loopback");
        break;

        case FAULT:
            rVal = jsprPutOperationalState("fault");
        break;

        case ACTIVE:
        // fall through
        default:
            rVal = jsprPutOperationalState("active");
        break;
    }

    return rVal;
}

/**
 * @brief Send command to retrieve current operational state
 * 
 * Sends a GET request to the modem to retrieve the current operational
 * state. The response will indicate the modem's current operational mode.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetOperationalState(void)
{
    bool rVal = false;
    const char getOperationalStateStr[JSPR_GET_OPERATIONAL_STATE_LEN] = "GET operationalState {}\r";

    if (context.serialWrite != NULL)
    {
        if(sendJspr(getOperationalStateStr, JSPR_GET_OPERATIONAL_STATE_LEN) == JSPR_GET_OPERATIONAL_STATE_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Send command to set operational state
 * 
 * Sends a PUT request to the modem to change its operational state.
 * The state parameter specifies the desired operational mode.
 * 
 * @param state String specifying the operational state
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutOperationalState(const char * state)
{
    bool rVal = false;
    int rc = 0;

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer),
            "PUT operationalState {\"state\": \"%s\"}\r", state);

    if (rc > 0)
    {
        const size_t putOperationalStateStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putOperationalStateStrLen) == putOperationalStateStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

// =============================================================================
// MESSAGING COMMANDS
// =============================================================================

/**
 * @brief Send request to originate a new message
 * 
 * Sends a PUT request to initiate message origination with the specified
 * topic and length. Includes a rolling request_reference (1-100) that
 * increments with each use and wraps around after reaching 100.
 * 
 * @param topic Topic identifier for the message
 * @param length Length of the message payload in bytes
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutMessageOriginate(const uint16_t topic, const size_t length)
{
    bool rVal = false;
    int rc = 0;

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer),
            "PUT messageOriginate {\"topic_id\":%d, \"message_length\":%ld, \"request_reference\":%d}\r",
            topic, length, messageReference);

    if (rc > 0)
    {
        // Increment message reference, wrap around after 100
        messageReference++;
        if(messageReference > 100)
        {
            messageReference = 1;
        }
        const size_t putMessageOriginateStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putMessageOriginateStrLen) == putMessageOriginateStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

/**
 * @brief Send a segment of a larger message
 * 
 * Sends a PUT request to transmit a segment of a larger message. This
 * is used when messages exceed the maximum segment size and need to be
 * transmitted in multiple parts.
 * 
 * @param messageOriginate Pointer to message originate structure
 * @param segmentLength Length of the current segment in bytes
 * @param segmentStart Starting byte offset of the segment within the complete message
 * @param data Pointer to the segment data buffer
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutMessageOriginateSegment(jsprMessageOriginate_t * messageOriginate, const size_t segmentLength, uint32_t segmentStart, const char * data)
{
    bool rVal = false;
    int rc = 0;

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer),
            "PUT messageOriginateSegment {\"topic_id\":%d, \"message_id\":%d, \"segment_length\":%ld, \"segment_start\":%d, \"data\":\"%s\"}\r",
            messageOriginate->topic, messageOriginate->messageId, segmentLength, segmentStart, data);

    if (rc > 0)
    {
        const size_t putMessageOriginateSegmentStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putMessageOriginateSegmentStrLen) == putMessageOriginateSegmentStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

// =============================================================================
// SYSTEM INFORMATION COMMANDS
// =============================================================================

/**
 * @brief Request signal and constellation state information
 * 
 * Sends a GET request to retrieve the current signal strength and
 * constellation state information from the modem.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetSignal(void)
{
    bool rVal = false;
    const char getSignalStr[JSPR_GET_SIGNAL_LEN] = "GET constellationState {}\r";

    if (context.serialWrite != NULL)
    {
        if(sendJspr(getSignalStr, JSPR_GET_SIGNAL_LEN) == JSPR_GET_SIGNAL_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Request message provisioning information
 * 
 * Sends a GET request to retrieve the current message provisioning
 * configuration and status from the modem.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetMessageProvisioning(void)
{
    bool rVal = false;
    const char getMessageProvisioningStr[JSPR_GET_MESSAGE_PROVISIONING_LEN] = "GET messageProvisioning {}\r";
    if (context.serialWrite != NULL)
    {
        if(sendJspr(getMessageProvisioningStr, JSPR_GET_MESSAGE_PROVISIONING_LEN) == JSPR_GET_MESSAGE_PROVISIONING_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Request hardware information
 * 
 * Sends a GET request to retrieve hardware information from the modem,
 * including device identification and capabilities.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetHwInfo(void)
{
    bool rVal = false;
    const char getHwInfoStr[JSPR_GET_HW_INFO_LEN] = "GET hwInfo {}\r";
    if (context.serialWrite != NULL)
    {
        if(sendJspr(getHwInfoStr, JSPR_GET_HW_INFO_LEN) == JSPR_GET_HW_INFO_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

// =============================================================================
// FIRMWARE COMMANDS
// =============================================================================

/**
 * @brief Convert boot slot enumeration to string representation
 * 
 * Helper function that converts the enumerated boot source to the
 * corresponding string value used in JSPR commands. This function is
 * static as it is only used internally within this module.
 * 
 * @param slot Enumerated boot source (fallback, primary, unknown)
 * @param dest Destination buffer for the string
 * @param length Length of the destination buffer
 */
static void bootSlotToStr(const jsprBootSource_t slot, char * dest, const size_t length)
{
    if (length > 0)
    {
        switch(slot)
        {
            case JSPR_BOOT_SOURCE_FALLBACK:
                strncpy(dest, "fallback", length - 1);
            break;

            case JSPR_BOOT_SOURCE_UNKNOWN:
            // fall through
            case JSPR_BOOT_SOURCE_PRIMARY:
            // fall through
            default:
                strncpy(dest, "primary", length - 1);
        }
    }
}

/**
 * @brief Request firmware information for a specific boot slot
 * 
 * Sends a GET request to retrieve firmware information for the specified
 * boot slot. The response includes version details and build information.
 * 
 * @param slot Boot source slot to query (primary or fallback)
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetFirmware(const jsprBootSource_t slot)
{
    bool rVal = false;
    int rc = 0;
    char slotStr [JSPR_BOOT_SOURCE_STR_LEN];

    bootSlotToStr(slot, slotStr, JSPR_BOOT_SOURCE_STR_LEN);

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer), "GET firmware {\"slot\": \"%s\"}\r", slotStr);

    if (rc > 0)
    {
        const size_t getFirmwareStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, getFirmwareStrLen) == getFirmwareStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

/**
 * @brief Command to update firmware for a specific boot slot
 * 
 * Sends a PUT request to initiate firmware update for the specified
 * boot slot. This command triggers the firmware update process.
 * 
 * @param slot Boot source slot to update (primary or fallback)
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutFirmware(const jsprBootSource_t slot)
{
    bool rVal = false;
    int rc = 0;
    char slotStr [JSPR_BOOT_SOURCE_STR_LEN];

    bootSlotToStr(slot, slotStr, JSPR_BOOT_SOURCE_STR_LEN);

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer), "PUT firmware {\"slot\": \"%s\"}\r", slotStr);

    if (rc > 0)
    {
        const size_t putFirmwareStrLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putFirmwareStrLen) == putFirmwareStrLen)
            {
                rVal = true;
            }
        }
    }

    return rVal;
}

// =============================================================================
// SIM STATUS AND SERVICE CONFIGURATION COMMANDS
// =============================================================================

/**
 * @brief Request SIM status information
 * 
 * Sends a GET request to retrieve the current SIM card status and
 * related information from the modem.
 * 
 * @return true if command was sent successfully, false otherwise
 */
bool jsprGetSimStatus(void)
{
    bool rVal = false;
    const char getSimStatusStr[JSPR_GET_SIM_STATUS_LEN] = "GET simStatus {}\r";
    if (context.serialWrite != NULL)
    {
        if(sendJspr(getSimStatusStr, JSPR_GET_SIM_STATUS_LEN) == JSPR_GET_SIM_STATUS_LEN)
        {
            rVal = true;
        }
    }
    return rVal;
}

/**
 * @brief Configure service with optional resynchronization
 * 
 * Sends a PUT request to configure the service settings. The resync
 * parameter determines whether to perform a service resynchronization
 * as part of the configuration process.
 * 
 * @param resync true to perform service resynchronization, false otherwise
 * @return true if command was sent successfully, false otherwise
 */
bool jsprPutServiceConfig(const bool resync)
{
    bool rVal = false;
    int rc = 0;

    rc = snprintf(jsprCommandBuffer, sizeof(jsprCommandBuffer), "PUT serviceConfig {\"resync\": %s}\r", resync ? "true" : "false");

    if (rc > 0)
    {
        const size_t putServiceConfigLen = (const size_t)rc;
        if (context.serialWrite != NULL)
        {
            if(sendJspr(jsprCommandBuffer, putServiceConfigLen) == putServiceConfigLen)
            {
                rVal = true;
            }
        }
    }
    return rVal;
}
