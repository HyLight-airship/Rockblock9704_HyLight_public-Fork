#include "jspr.h"
#include "jspr_command.h"
#include "serial.h"
#include "third_party/cJSON/cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "crossplatform.h"
#if defined(_WIN32)
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

// =============================================================================
// GLOBAL VARIABLES AND EXTERNAL REFERENCES
// =============================================================================

/**
 * @brief Global message reference counter for JSPR messaging
 * 
 * Provides unique request references (1-100) for message originate
 * commands. Increments with each use and wraps around after reaching 100.
 */
int messageReference = 1;

/**
 * @brief Static receive buffer for JSPR communication
 * 
 * Buffer used to store incoming JSPR messages from the modem.
 * This buffer is reused for all incoming messages to avoid
 * dynamic memory allocation.
 */
static uint8_t jsprRxBuffer [RX_BUFFER_SIZE];

/**
 * @brief External serial context for communication
 * 
 * Contains function pointers for serial port operations including
 * read, write, and peek functions.
 */
extern serialContext context;

/**
 * @brief Shared JSPR response structure
 * 
 * Static response structure shared across all JSPR operations
 * to avoid stack allocation of large structures.
 */
static jsprResponse_t shared_jspr_response;

// =============================================================================
// JSPR COMMUNICATION FUNCTIONS
// =============================================================================

/**
 * @brief Send data to the modem over the serial port
 * 
 * Sends the specified buffer to the modem through the serial interface.
 * Verifies that the data was sent successfully and optionally prints
 * debug information if DEBUG is defined.
 * 
 * @param buffer Pointer to the data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes written, or -1 if error occurred
 */
int sendJspr(const char *buffer, size_t length)
{
        int bytesWritten = context.serialWrite(buffer, length);
        if(0 > bytesWritten)
        {
            return -1;
        }
#ifdef DEBUG
        char * terminator = strpbrk(buffer, "\r");
        *terminator = '\0';
        printf("SENT: %s\r\n", buffer);
#endif
        return bytesWritten;
}

/**
 * @brief Receive data from the modem over the serial port
 * 
 * Waits for incoming data from the modem with a specified timeout.
 * Parses the received JSPR message to extract the result code,
 * target, and JSON payload. Optionally prints debug information
 * if DEBUG is defined.
 * 
 * @param timeout_ms Maximum time to wait for a complete message in milliseconds
 * @return Pointer to the parsed response structure, or NULL if timeout occurred
 */
jsprResponse_t* receiveJspr(const uint32_t timeout_ms)
{
    uint16_t pos = 0;
    int bytesRead = 0;
    char resultCode[JSPR_RESULT_CODE_LENGTH + 1];
    char * targetStart = NULL;
    char * targetEnd = NULL;
    uint16_t targetLength = 0;
    char * jsonStart = NULL;
    uint32_t startTime = millis();
    jsprResponse_t* response = &shared_jspr_response; // Use the shared buffer

    if (context.serialRead == NULL)
    {
        return NULL;
    }

    clearResponse(response);
    memset(jsprRxBuffer, 0 , RX_BUFFER_SIZE);

    // Wait for a complete message or timeout
    while ((millis() - startTime) < timeout_ms)
    {
        if (context.serialPeek() > 0)
        {
            bytesRead = context.serialRead((char*)&jsprRxBuffer[pos], 1);
            if (bytesRead > 0)
            {
                // Look for the end of a message
                if (jsprRxBuffer[pos] == '\r' && pos > JSPR_MIN_RESPONSE)
                {
                    jsprRxBuffer[pos] = '\0'; // Null-terminate the string

                    // --- Start Parsing ---
                    strncpy(resultCode, (char*)jsprRxBuffer, JSPR_RESULT_CODE_LENGTH);
                    resultCode[JSPR_RESULT_CODE_LENGTH] = '\0';
                    response->code = (uint16_t)atoi(resultCode);

                    if (response->code < 200)
                    {
                        pos = 0;
                        continue;
                    }

#ifdef DEBUG
                    printf("RECEIVED: %s\r\n", jsprRxBuffer);
#endif

                    targetStart = (char*)&jsprRxBuffer[JSPR_RESULT_CODE_LENGTH + 1];
                    targetEnd = strchr(targetStart, ' ');
                    if (targetEnd == NULL)
                    {
                        pos = 0; // Malformed, reset
                        continue;
                    }

                    targetLength = targetEnd - targetStart;
                    memcpy(response->target, targetStart, targetLength);
                    response->target[targetLength] = '\0';

                    jsonStart = strchr(targetStart, '{');
                    if (jsonStart != NULL)
                    {
                        response->jsonSize = strlen(jsonStart);
                        strncpy(response->json, jsonStart, response->jsonSize);
                        response->json[response->jsonSize] = '\0';
                    }

                    return response; // Return pointer to the populated shared buffer
                }
                pos++;
                if (pos >= RX_BUFFER_SIZE) // Prevent buffer overflow
                {
                    pos = 0;
                }
            }
        }
    }

    return NULL; // Timeout occurred, return NULL
}

/**
 * @brief Clear the response buffer structure
 * 
 * Resets all fields of the response structure to their default values.
 * This function is called before parsing new responses to ensure
 * clean state for the next operation.
 * 
 * @param response Pointer to the response structure to clear
 */
void clearResponse(jsprResponse_t * response)
{
    response->code = 0;
    response->jsonSize = 0;
    memset(response->json, 0, JSPR_MAX_JSON_LENGTH);
    memset(response->target, 0, JSPR_MAX_TARGET_LENGTH);
}

// =============================================================================
// JSPR RESPONSE PARSING FUNCTIONS
// =============================================================================

/**
 * @brief Parse boot information from JSPR response string
 * 
 * Extracts boot information from a JSPR response including image type,
 * boot source, and version details. Uses cJSON library to parse the
 * JSON payload and populate the bootInfo structure.
 * 
 * @param jsprString JSPR response string containing boot information
 * @param bootInfo Pointer to structure to store parsed boot information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprBootInfo(const char * jsprString, jsprBootInfo_t * bootInfo)
{
    bool parsed = false;
    cJSON * json = NULL;
    cJSON * imageType = NULL;
    cJSON * bootSource = NULL;
    cJSON * version = NULL;
    cJSON * major =  NULL;
    cJSON * minor =  NULL;
    cJSON * patch =  NULL;
    cJSON * buildInfo =  NULL;
    char * cPtr = NULL;

    if ((jsprString != NULL) && (bootInfo != NULL))
    {
        json = cJSON_Parse(jsprString);

        if (json != NULL)
        {
            imageType = cJSON_GetObjectItem(json, "image_type");
            bootSource = cJSON_GetObjectItem(json, "boot_source");
            version = cJSON_GetObjectItem(json, "version");
        }

        if (imageType != NULL)
        {
            cPtr = stpncpy(bootInfo->imageType, imageType->valuestring, JSPR_BOOT_INFO_IMAGE_TYPE_LEN - 1);
            *cPtr = '\0';
            cPtr = NULL;
        }

        if (bootSource != NULL)
        {
            if (strcmp(bootSource->valuestring, "primary") == 0)
            {
                bootInfo->bootSource = JSPR_BOOT_SOURCE_PRIMARY;
            }
            else if (strcmp(bootSource->valuestring, "fallback") == 0)
            {
                bootInfo->bootSource = JSPR_BOOT_SOURCE_FALLBACK;
            }
            else
            {
                bootInfo->bootSource = JSPR_BOOT_SOURCE_UNKNOWN;
            }
        }

        if (version != NULL)
        {
            major = cJSON_GetObjectItem(version, "major");
            minor = cJSON_GetObjectItem(version, "minor");
            patch = cJSON_GetObjectItem(version, "patch");
            buildInfo = cJSON_GetObjectItem(version, "build_info");

            if (major != NULL)
            {
                bootInfo->versionInfo.version.major = major->valueint;
            }

            if (minor != NULL)
            {
                bootInfo->versionInfo.version.minor = minor->valueint;
            }

            if (patch != NULL)
            {
                bootInfo->versionInfo.version.patch = patch->valueint;
            }

            if (buildInfo != NULL)
            {
                cPtr = stpncpy(bootInfo->versionInfo.buildInfo, buildInfo->valuestring, JSPR_VERSION_INFO_BUILD_INFO_LEN - 1);
                *cPtr = '\0';
                cPtr = NULL;
            }
        }
        cJSON_Delete(json);
        parsed = true;
    }

    return parsed;
}

/**
 * @brief Parse API version information from JSPR response string
 * 
 * Extracts API version information from a JSPR response including
 * supported versions and active version. Parses both the supported
 * versions array and the active version object to populate the
 * apiVersion structure.
 * 
 * @param jsprString JSPR response string containing API version information
 * @param apiVersion Pointer to structure to store parsed API version information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetApiVersion(char * jsprString, jsprApiVersion_t * apiVersion)
{
    bool parsed = false;

    if ((jsprString != NULL) && (apiVersion != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * supportedVersions = cJSON_GetObjectItem(root, "supported_versions");
            if (cJSON_IsArray(supportedVersions))
            {
                int count = cJSON_GetArraySize(supportedVersions);
                int apiIndex = (count > 0) ? (count - 1) : 0;
                for (int i = 0; i < count && i < JSPR_MAX_NUM_API_VERSIONS; i++)
                {
                    cJSON *version = cJSON_GetArrayItem(supportedVersions, apiIndex);
                    if (cJSON_IsObject(version))
                    {
                        cJSON *major = cJSON_GetObjectItem(version, "major");
                        cJSON *minor = cJSON_GetObjectItem(version, "minor");
                        cJSON *patch = cJSON_GetObjectItem(version, "patch");
                        if (cJSON_IsNumber(major) && cJSON_IsNumber(minor) && cJSON_IsNumber(patch))
                        {
                            apiVersion->supportedVersions[i].major = (uint8_t)major->valueint;
                            apiVersion->supportedVersions[i].minor = (uint8_t)minor->valueint;
                            apiVersion->supportedVersions[i].patch = (uint8_t)patch->valueint;
                            apiVersion->supportedVersionCount++;
                        }
                    }
                    apiIndex--;
                }
            }
            cJSON *activeVersion = cJSON_GetObjectItem(root, "active_version");
            if (cJSON_IsObject(activeVersion))
            {
                cJSON *major = cJSON_GetObjectItem(activeVersion, "major");
                cJSON *minor = cJSON_GetObjectItem(activeVersion, "minor");
                cJSON *patch = cJSON_GetObjectItem(activeVersion, "patch");
                if (cJSON_IsNumber(major) && cJSON_IsNumber(minor) && cJSON_IsNumber(patch))
                {
                    apiVersion->activeVersion.major = (uint8_t)major->valueint;
                    apiVersion->activeVersion.minor = (uint8_t)minor->valueint;
                    apiVersion->activeVersion.patch = (uint8_t)patch->valueint;
                    apiVersion->activeVersionSet = true;
                }
            }
            else
            {
                apiVersion->activeVersionSet = false;
            }
            parsed = true;
            cJSON_Delete(root);
        }
    }

    return parsed;
}

/**
 * @brief Parse firmware information from JSPR response string
 * 
 * Extracts firmware information from a JSPR response including slot,
 * validity, version details, and hash. Uses cJSON library to parse
 * the JSON payload and populate the firmwareInfo structure.
 * 
 * @param jsprString JSPR response string containing firmware information
 * @param firmwareInfo Pointer to structure to store parsed firmware information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprFirmwareInfo(const char * jsprString, jsprFirmwareInfo_t * firmwareInfo)
{
    bool parsed = false;
    cJSON * json = NULL;
    cJSON * slot = NULL;
    cJSON * validity = NULL;
    cJSON * version = NULL;
    cJSON * hash = NULL;
    cJSON * major =  NULL;
    cJSON * minor =  NULL;
    cJSON * patch =  NULL;
    cJSON * buildInfo = NULL;
    char * cPtr = NULL;

    if ((jsprString != NULL) && (firmwareInfo != NULL))
    {
        cJSON *json = cJSON_Parse(jsprString);
        if (json != NULL)
        {
            slot = cJSON_GetObjectItem(json, "slot");
            validity = cJSON_GetObjectItem(json, "validity");
            version = cJSON_GetObjectItem(json, "version");
            hash = cJSON_GetObjectItem(json, "hash");

            if (slot != NULL)
            {
                if (strcmp(slot->valuestring, "primary") == 0)
                {
                    firmwareInfo->slot = JSPR_BOOT_SOURCE_PRIMARY;
                }
                else if (strcmp(slot->valuestring, "fallback") == 0)
                {
                    firmwareInfo->slot = JSPR_BOOT_SOURCE_FALLBACK;
                }
                else
                {
                    firmwareInfo->slot = JSPR_BOOT_SOURCE_UNKNOWN;
                }
            }

            if (validity != NULL)
            {
                firmwareInfo->validity = (validity->valueint > 0);
            }

            if (version != NULL)
            {
                major = cJSON_GetObjectItem(version, "major");
                minor = cJSON_GetObjectItem(version, "minor");
                patch = cJSON_GetObjectItem(version, "patch");
                buildInfo = cJSON_GetObjectItem(version, "build_info");

                if (major != NULL)
                {
                    firmwareInfo->versionInfo.version.major = major->valueint;
                }

                if (minor != NULL)
                {
                    firmwareInfo->versionInfo.version.minor = minor->valueint;
                }

                if (patch != NULL)
                {
                    firmwareInfo->versionInfo.version.patch = patch->valueint;
                }

                if (buildInfo != NULL)
                {
                    cPtr = stpncpy(firmwareInfo->versionInfo.buildInfo, buildInfo->valuestring, JSPR_VERSION_INFO_BUILD_INFO_LEN - 1);
                    *cPtr = '\0';
                    cPtr = NULL;
                }
            }

            if (hash != NULL)
            {
                cPtr = stpncpy(firmwareInfo->hash, hash->valuestring, JSPR_BOOT_INFO_HASH_LEN - 1);
                *cPtr = '\0';
                cPtr = NULL;
            }

            parsed = true;
            cJSON_Delete(json);
        }
    }

    return parsed;
}

/**
 * @brief Parse SIM interface configuration from JSPR response string
 * 
 * Extracts SIM interface configuration from a JSPR response including
 * the active interface type. Converts the string interface value to
 * the corresponding enumeration value.
 * 
 * @param jsprString JSPR response string containing SIM interface information
 * @param simInterface Pointer to structure to store parsed SIM interface information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetSimInterface(char * jsprString, jsprSimInterface_t * simInterface)
{
    bool parsed = false;

    if ((jsprString != NULL) && (simInterface != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * iface = cJSON_GetObjectItem(root, "interface");
            if(cJSON_IsString(iface))
            {
                if(strcmp(iface->valuestring, "none") == 0)
                {
                    simInterface->ifaceSet = true;
                    simInterface->iface = SIM_NONE;
                }
                else if (strcmp(iface->valuestring, "local") == 0)
                {
                    simInterface->ifaceSet = true;
                    simInterface->iface = SIM_LOCAL;
                }
                else if (strcmp(iface->valuestring, "remote") == 0)
                {
                    simInterface->ifaceSet = true;
                    simInterface->iface = SIM_REMOTE;
                }
                else if (strcmp(iface->valuestring, "internal") == 0)
                {
                    simInterface->ifaceSet = true;
                    simInterface->iface = SIM_INTERNAL;
                }
            }
            else
            {
                simInterface->ifaceSet = false;
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }

    return parsed;
}

/**
 * @brief Parse operational state from JSPR response string
 * 
 * Extracts operational state information from a JSPR response including
 * the reason and state. Parses the reason as an integer and the state
 * as a string.
 * 
 * @param jsprString JSPR response string containing operational state information
 * @param operationalState Pointer to structure to store parsed operational state information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetOperationalState(char * jsprString, jsprOperationalState_t * operationalState)
{
    bool parsed = false;

    if ((jsprString != NULL) && (operationalState != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * reason = cJSON_GetObjectItem(root, "reason");
            if(cJSON_IsNumber(reason))
            {
                if(reason->valueint >= NORMAL && reason->valueint <= MFRTEST_USED_INCORRECTLY)
                {
                    operationalState->reason = reason->valueint;
                }
            }
            cJSON * state = cJSON_GetObjectItem(root, "state");
            if(cJSON_IsString(state))
            {
                if(strcmp(state->valuestring, "inactive") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = INACTIVE;
                }
                else if (strcmp(state->valuestring, "active") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = ACTIVE;
                }
                else if (strcmp(state->valuestring, "cal_test") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = CAL_TEST;
                }
                else if (strcmp(state->valuestring, "hw_self_test") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = HW_SELF_TEST;
                }
                else if (strcmp(state->valuestring, "rf_scan") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = RF_SCAN;
                }
                else if (strcmp(state->valuestring, "loopback") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = LOOPBACK;
                }
                else if (strcmp(state->valuestring, "fault") == 0)
                {
                    operationalState->operationalStateSet = true;
                    operationalState->operationalState = FAULT;
                }
            }
            else
            {
                operationalState->operationalStateSet = false;
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }

    return parsed;
}

/**
 * @brief Parse message originate from JSPR response string
 * 
 * Extracts message originate information from a JSPR response including
 * topic ID, request reference, and message response. Parses the
 * message response as a string and sets the corresponding enum value.
 * 
 * @param jsprString JSPR response string containing message originate information
 * @param messageOriginate Pointer to structure to store parsed message originate information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprPutMessageOriginate(char * jsprString, jsprMessageOriginate_t  * messageOriginate)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageOriginate != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageOriginate->topic = topicId->valueint;
                }
            }
            cJSON * requestReference = cJSON_GetObjectItem(root, "request_reference");
            if(cJSON_IsNumber(requestReference))
            {
                if(requestReference->valueint >= 1 && requestReference->valueint <= 100)
                {
                    messageOriginate->requestReference = requestReference->valueint;
                }
            }
            cJSON * messageResponce = cJSON_GetObjectItem(root, "message_response");
            if(cJSON_IsString(messageResponce))
            {
                if(strcmp(messageResponce->valuestring, "message_accepted") == 0)
                {
                    messageOriginate->messageResponse = MESSAGE_ACCEPTED;
                }
                else if (strcmp(messageResponce->valuestring, "subscription_invalid") == 0)
                {
                    messageOriginate->messageResponse = SUBSCRIPTION_INVALID;
                }
                else if (strcmp(messageResponce->valuestring, "message_discarded_on_overflow") == 0)
                {
                    messageOriginate->messageResponse = MESSAGE_DISCARDED_ON_OVERFLOW;
                }
            }
            messageOriginate->messageIdSet = false;
            if(messageOriginate->messageResponse == MESSAGE_ACCEPTED)
            {
                cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
                if(cJSON_IsNumber(messageId))
                {
                    if(messageId->valueint >= 0 && messageId->valueint <= 255)
                    {
                        messageOriginate->messageId = messageId->valueint;
                        messageOriginate->messageIdSet = true;
                    }
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message originate segment from JSPR response string
 * 
 * Extracts message originate segment information from a JSPR response
 * including topic ID, segment length, segment start, and message ID.
 * 
 * @param jsprString JSPR response string containing message originate segment information
 * @param messageOriginateSegment Pointer to structure to store parsed message originate segment information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprUnsMessageOriginateSegment(char * jsprString, jsprMessageOriginateSegment_t * messageOriginateSegment)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageOriginateSegment != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageOriginateSegment->topic = topicId->valueint;
                }
            }
            cJSON * segmentLength = cJSON_GetObjectItem(root, "segment_length");
            if(cJSON_IsNumber(segmentLength))
            {
                if(segmentLength->valueint >= 1 && segmentLength->valueint <= 1446)
                {
                    messageOriginateSegment->segmentLength = segmentLength->valueint;
                }
            }
            cJSON * segmentStart = cJSON_GetObjectItem(root, "segment_start");
            if(cJSON_IsNumber(segmentStart))
            {
                if(segmentStart->valueint >= 0 && segmentStart->valueint <= 100001)
                {
                    messageOriginateSegment->segmentStart = segmentStart->valueint;
                }
            }
            cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
            if(cJSON_IsNumber(messageId))
            {
                if(messageId->valueint >= 0 && messageId->valueint <= 255)
                {
                    messageOriginateSegment->messageId = messageId->valueint;
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message terminate from JSPR response string
 * 
 * Extracts message terminate information from a JSPR response including
 * topic ID, message length max, and message ID.
 * 
 * @param jsprString JSPR response string containing message terminate information
 * @param messageTerminate Pointer to structure to store parsed message terminate information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprUnsMessageTerminate(char * jsprString, jsprMessageTerminate_t * messageTerminate)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageTerminate != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageTerminate->topic = topicId->valueint;
                }
            }
            cJSON * messageLengthMax = cJSON_GetObjectItem(root, "message_length_max");
            if(cJSON_IsNumber(messageLengthMax))
            {
                if(messageLengthMax->valueint >= 3 && messageLengthMax->valueint <= 100002)
                {
                    messageTerminate->messageLengthMax = messageLengthMax->valueint;
                }
            }
            cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
            if(cJSON_IsNumber(messageId))
            {
                if(messageId->valueint >= 0 && messageId->valueint <= 255)
                {
                    messageTerminate->messageId = messageId->valueint;
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message terminate segment from JSPR response string
 * 
 * Extracts message terminate segment information from a JSPR response
 * including topic ID, segment length, segment start, message ID, and
 * data. Parses the data as a string and stores it in the data buffer.
 * 
 * @param jsprString JSPR response string containing message terminate segment information
 * @param messageTerminateSegment Pointer to structure to store parsed message terminate segment information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprUnsMessageTerminateSegment(char * jsprString, jsprMessageTerminateSegment_t * messageTerminateSegment)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageTerminateSegment != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageTerminateSegment->topic = topicId->valueint;
                }
            }
            cJSON * segmentLength = cJSON_GetObjectItem(root, "segment_length");
            if(cJSON_IsNumber(segmentLength))
            {
                if(segmentLength->valueint >= 1 && segmentLength->valueint <= 1446)
                {
                    messageTerminateSegment->segmentLength = segmentLength->valueint;
                }
            }
            cJSON * segmentStart = cJSON_GetObjectItem(root, "segment_start");
            if(cJSON_IsNumber(segmentStart))
            {
                if(segmentStart->valueint >= 0 && segmentStart->valueint <= 100001)
                {
                    messageTerminateSegment->segmentStart = segmentStart->valueint;
                }
            }
            cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
            if(cJSON_IsNumber(messageId))
            {
                if(messageId->valueint >= 0 && messageId->valueint <= 255)
                {
                    messageTerminateSegment->messageId = messageId->valueint;
                }
            }
            cJSON * data = cJSON_GetObjectItem(root, "data");
            if(cJSON_IsString(data))
            {
                memset(messageTerminateSegment->data, 0, JSPR_MAX_SEGMENT_LENGTH);
                memcpy(messageTerminateSegment->data, data->valuestring, strlen(data->valuestring));
                messageTerminateSegment->dataLength = strlen(data->valuestring);
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse signal from JSPR response string
 * 
 * Extracts signal information from a JSPR response including
 * constellation visibility, signal level, and signal bars.
 * 
 * @param jsprString JSPR response string containing signal information
 * @param signal Pointer to structure to store parsed signal information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetSignal(char * jsprString, jsprConstellationState_t * signal)
{
    bool parsed = false;

    if ((jsprString != NULL) && (signal != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * constellationVisible = cJSON_GetObjectItem(root, "constellation_visible");
            if(cJSON_IsBool(constellationVisible))
            {
                signal->constellationVisible = cJSON_IsTrue(constellationVisible);
                if(signal->constellationVisible)
                {
                    cJSON * signalLevel = cJSON_GetObjectItem(root, "signal_level");
                    if(cJSON_IsNumber(signalLevel))
                    {
                        signal->signalLevel = signalLevel->valueint;
                    }
                }
            }
            cJSON * signalBars = cJSON_GetObjectItem(root, "signal_bars");
            if(cJSON_IsNumber(signalBars))
            {
                if(signalBars->valueint >= 0 && signalBars->valueint <= 5)
                {
                    signal->signalBars = signalBars->valueint;
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message originate status from JSPR response string
 * 
 * Extracts message originate status information from a JSPR response
 * including topic ID, message ID, and final message originate status.
 * Parses the final message originate status as a string and sets the
 * corresponding enum value.
 * 
 * @param jsprString JSPR response string containing message originate status information
 * @param messageOriginateStatus Pointer to structure to store parsed message originate status information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprUnsMessageOriginateStatus(char * jsprString, jsprMessageOriginateStatus_t * messageOriginateStatus)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageOriginateStatus != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageOriginateStatus->topic = topicId->valueint;
                }
            }
            cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
            if(cJSON_IsNumber(messageId))
            {
                if(messageId->valueint >= 0 && messageId->valueint <= 255)
                {
                    messageOriginateStatus->messageId = messageId->valueint;
                }
            }
            cJSON * finalMoStatus = cJSON_GetObjectItem(root, "final_mo_status");
            if(cJSON_IsString(finalMoStatus))
            {
                if(strcmp(finalMoStatus->valuestring, "mo_ack_received") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MO_ACK_RECEIVED_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_discarded_on_overflow") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_DISCARDED_ON_OVERFLOW_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_expired") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_EXPIRED_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_transfer_timeout") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_TRANSFER_TIMEOUT_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "segment_not_supplied") == 0)
                {
                    messageOriginateStatus->finalMoStatus = SEGMENT_NOT_SUPPLIED_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "segment_incorrect") == 0)
                {
                    messageOriginateStatus->finalMoStatus = SEGMENT_INCORRECT_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "network_error") == 0)
                {
                    messageOriginateStatus->finalMoStatus = NETWORK_ERROR_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_cancelled_pre_transit") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_CANCELLED_PRE_TRANSIT_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_cancelled_in_transit") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_CANCELLED_IN_TRANSIT_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "subscription_invalid") == 0)
                {
                    messageOriginateStatus->finalMoStatus = SUBSCRIPTION_INVALID_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "protocol_error") == 0)
                {
                    messageOriginateStatus->finalMoStatus = PROTOCOL_ERROR_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_dropped_local_crc_error") == 0)
                {
                    messageOriginateStatus->finalMoStatus = MESSAGE_DROPPED_LOCAL_CRC_ERROR_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "crc_error_in_transfer") == 0)
                {
                    messageOriginateStatus->finalMoStatus = CRC_ERROR_IN_TRANSFER_MOS;
                }
                else if (strcmp(finalMoStatus->valuestring, "user_supplied_crc_error") == 0)
                {
                    messageOriginateStatus->finalMoStatus = USER_SUPPLIED_CRC_ERROR_MOS;
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message terminate status from JSPR response string
 * 
 * Extracts message terminate status information from a JSPR response
 * including topic ID, message ID, and final message terminate status.
 * Parses the final message terminate status as a string and sets the
 * corresponding enum value.
 * 
 * @param jsprString JSPR response string containing message terminate status information
 * @param messageTerminateStatus Pointer to structure to store parsed message terminate status information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprUnsMessageTerminateStatus(char * jsprString, jsprMessageTerminateStatus_t * messageTerminateStatus)
{
    bool parsed = false;

    if ((jsprString != NULL) && (messageTerminateStatus != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * topicId = cJSON_GetObjectItem(root, "topic_id");
            if(cJSON_IsNumber(topicId))
            {
                if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                {
                    messageTerminateStatus->topic = topicId->valueint;
                }
            }
            cJSON * messageId = cJSON_GetObjectItem(root, "message_id");
            if(cJSON_IsNumber(messageId))
            {
                if(messageId->valueint >= 0 && messageId->valueint <= 255)
                {
                    messageTerminateStatus->messageId = messageId->valueint;
                }
            }
            cJSON * finalMoStatus = cJSON_GetObjectItem(root, "final_mt_status");
            if(cJSON_IsString(finalMoStatus))
            {
                if(strcmp(finalMoStatus->valuestring, "complete") == 0)
                {
                    messageTerminateStatus->finalMtStatus = COMPLETE;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_timed_out") == 0)
                {
                    messageTerminateStatus->finalMtStatus = MESSAGE_TIMED_OUT;
                }
                else if (strcmp(finalMoStatus->valuestring, "message_cancelled") == 0)
                {
                    messageTerminateStatus->finalMtStatus = MESSAGE_CANCELLED;
                }
                else if (strcmp(finalMoStatus->valuestring, "crc_error_in_transfer") == 0)
                {
                    messageTerminateStatus->finalMtStatus = CRC_ERROR_IN_TRANSFER;
                }
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse message provisioning from JSPR response string
 * 
 * Extracts message provisioning information from a JSPR response including
 * the list of topics and their configurations. Parses the provisioning
 * array and populates the messageProvisioning structure.
 * 
 * @param jsprString JSPR response string containing message provisioning information
 * @param messageProvisioning Pointer to structure to store parsed message provisioning information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetMessageProvisioning(char * jsprString, jsprMessageProvisioning_t * messageProvisioning)
{
        bool parsed = false;

    if ((jsprString != NULL) && (messageProvisioning != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * provisioning = cJSON_GetObjectItem(root, "provisioning");
            if(cJSON_IsArray(provisioning))
            {
                int count = cJSON_GetArraySize(provisioning);
                messageProvisioning->topicCount = count;
                for (int i = 0; i < count && i < JSPR_MAX_TOPICS; i++)
                {
                    cJSON *topic = cJSON_GetArrayItem(provisioning, i);
                    if(cJSON_IsObject(topic))
                    {
                        cJSON * topicId = cJSON_GetObjectItem(topic, "topic_id");
                        if(cJSON_IsNumber(topicId))
                        {
                            if(topicId->valueint >= 64 && topicId->valueint <= 65535)
                            {
                                messageProvisioning->provisioning[i].topicId = topicId->valueint;
                            }
                        }
                        cJSON * topicName = cJSON_GetObjectItem(topic, "topic_name");
                        if(cJSON_IsString(topicName))
                        {
                            if(strlen(topicName->valuestring) <= JSPR_TOPIC_NAME_MAX_LENGTH)
                            {
                                memset(messageProvisioning->provisioning[i].topicName, 0, JSPR_TOPIC_NAME_MAX_LENGTH);
                                memcpy(messageProvisioning->provisioning[i].topicName, topicName->valuestring, strlen(topicName->valuestring));
                            }
                        }
                        cJSON * priority = cJSON_GetObjectItem(topic, "priority");
                        if(cJSON_IsString(priority))
                        {
                            if(strcmp(priority->valuestring, "Safety-1") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = SAFETY_1;
                            }
                            else if(strcmp(priority->valuestring, "Safety-2") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = SAFETY_2;
                            }
                            else if(strcmp(priority->valuestring, "Safety-3") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = SAFETY_3;
                            }
                            else if(strcmp(priority->valuestring, "High") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = HIGH_PRIORITY;
                            }
                            else if(strcmp(priority->valuestring, "Medium") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = MEDIUM_PRIORITY;
                            }
                            else if(strcmp(priority->valuestring, "Low") == 0)
                            {
                                messageProvisioning->provisioning[i].priority = LOW_PRIORITY;
                            }
                        }
                    }
                }
            }
        messageProvisioning->provisioningSet = true;
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse hardware information from JSPR response string
 * 
 * Extracts hardware information from a JSPR response including
 * hardware version, serial number, and IMEI.
 * 
 * @param jsprString JSPR response string containing hardware information
 * @param hwInfo Pointer to structure to store parsed hardware information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetHwInfo(char * jsprString, jsprHwInfo_t * hwInfo)
{
    bool parsed = false;

    if ((jsprString != NULL) && (hwInfo != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * hwVersion = cJSON_GetObjectItem(root, "hw_version");
            if(cJSON_IsString(hwVersion))
            {
                memset(hwInfo->hwVersion, 0, JSPR_HW_VERSION_MAX_LENGTH);
                memcpy(hwInfo->hwVersion, hwVersion->valuestring, JSPR_HW_VERSION_MAX_LENGTH - 1);
            }
            cJSON * serialNumber = cJSON_GetObjectItem(root, "serial_number");
            if(cJSON_IsString(serialNumber))
            {
                memset(hwInfo->serialNumber, 0, JSPR_SERIAL_NUMBER_MAX_LENGTH);
                memcpy(hwInfo->serialNumber, serialNumber->valuestring, JSPR_SERIAL_NUMBER_MAX_LENGTH - 1);
            }
            cJSON * imei = cJSON_GetObjectItem(root, "imei");
            if(cJSON_IsString(imei))
            {
                memset(hwInfo->imei, 0, JSPR_IMEI_MAX_LENGTH);
                memcpy(hwInfo->imei, imei->valuestring, JSPR_IMEI_MAX_LENGTH - 1);
            }
            cJSON * boardTemp = cJSON_GetObjectItem(root, "board_temp");
            if(cJSON_IsNumber(boardTemp))
            {
                hwInfo->boardTemp = boardTemp->valueint;
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}

/**
 * @brief Parse SIM status from JSPR response string
 * 
 * Extracts SIM status information from a JSPR response including
 * card presence, SIM connection, and ICCID.
 * 
 * @param jsprString JSPR response string containing SIM status information
 * @param simStatus Pointer to structure to store parsed SIM status information
 * @return true if parsing was successful, false otherwise
 */
bool parseJsprGetSimStatus(char * jsprString, jsprSimStatus_t * simStatus)
{
    bool parsed = false;

    if ((jsprString != NULL) && (simStatus != NULL))
    {
        cJSON * root = cJSON_Parse(jsprString);
        if (root != NULL)
        {
            cJSON * cardPresent = cJSON_GetObjectItem(root, "card_present");
            if(cJSON_IsBool(cardPresent))
            {
                simStatus->cardPresent = cJSON_IsTrue(cardPresent);
            }
            cJSON * simConnected = cJSON_GetObjectItem(root, "sim_connected");
            if(cJSON_IsBool(simConnected))
            {
                simStatus->simConnected = cJSON_IsTrue(simConnected);
            }
            cJSON * iccid = cJSON_GetObjectItem(root, "iccid");
            if(cJSON_IsString(iccid))
            {
                memset(simStatus->iccid, 0, JSPR_ICCID_MAX_LENGTH);
                memcpy(simStatus->iccid, iccid->valuestring, JSPR_ICCID_MAX_LENGTH - 1);
            }
        parsed = true;
        cJSON_Delete(root);
        }
    }
    return parsed;
}
