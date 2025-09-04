#include "imt_queue.h"

// =============================================================================
// STATIC VARIABLES AND BUFFERS
// =============================================================================

/**
 * @brief Static queue instances for Mobile-Originated (MO) and Mobile-Terminated (MT) messages
 * 
 * These queues manage the storage and retrieval of Iridium messaging protocol
 * messages in a circular buffer implementation.
 */
static imt_queue_t imtMo;
static imt_queue_t imtMt;

/**
 * @brief Static buffers for each queue
 * 
 * Each entry in the queue has IMT_PAYLOAD_SIZE bytes allocated for storing
 * message payload data. These buffers are statically allocated to avoid
 * dynamic memory allocation issues in embedded systems.
 */
static uint8_t imtMoBuffer[IMT_QUEUE_SIZE][IMT_PAYLOAD_SIZE];
static uint8_t imtMtBuffer[IMT_QUEUE_SIZE][IMT_PAYLOAD_SIZE];

/**
 * @brief Lock flag for MT queue protection
 * 
 * When set to true, prevents overwriting of the oldest entry when the
 * MT queue is full. This is useful for preserving important messages
 * during high-traffic periods.
 */
static volatile bool mtLock = false;

// =============================================================================
// QUEUE ADDITION FUNCTIONS
// =============================================================================

/**
 * @brief Add a message to the Mobile-Originated (MO) queue
 * 
 * Adds a new message to the MO queue with the specified topic and payload data.
 * The message is stored at the current tail position and the tail pointer is
 * advanced. If the queue is full, the message will be rejected.
 * 
 * @param topic Topic identifier for the message
 * @param data Pointer to the payload buffer containing message data
 * @param length Length of the payload data in bytes
 * @return true if message was successfully queued, false if queue is full or invalid data
 */
bool imtQueueMoAdd(uint16_t topic, const char * data, const size_t length)
{
    bool queued = false;
    uint16_t tempTail = imtMo.tail;
    if(data != NULL && length > 0)
    {
        if(imtMo.count < imtMo.maxLength) // Check if queue has space
        {
            memcpy(imtMoBuffer[tempTail], data, length); // Copy payload into buffer
            imtMo.messages[tempTail].topic = topic;
            imtMo.messages[tempTail].length = length;
            queued = true;

            imtMo.tail = (tempTail + 1) % imtMo.maxLength; // Update tail pointer
            imtMo.count++; // Increase message count
        }
    }
    return queued;
}

/**
 * @brief Add a message to the Mobile-Terminated (MT) queue
 * 
 * Adds a new message to the MT queue with the specified topic, ID, and length.
 * If the queue is full and not locked, the oldest entry will be removed to
 * make space for the new message. The message is stored at the current tail
 * position and the tail pointer is advanced.
 * 
 * @param topic Topic identifier for the message
 * @param id Unique message identifier
 * @param length Length of the message payload in bytes
 * @return true if message was successfully queued, false if invalid parameters
 */
bool imtQueueMtAdd(const uint16_t topic, const uint16_t id, const size_t length)
{
    bool queued = false;
    uint16_t tempTail = imtMt.tail;
    if(id >= 0 && length > 0)
    {
        // If queue is full and not locked, remove oldest entry
        if(imtMt.count >= imtMt.maxLength && !mtLock)
        {
            imtQueueMtRemove();
        }

        if(imtMt.count < imtMt.maxLength)
        {
            imtMt.messages[tempTail].id = id;
            imtMt.messages[tempTail].topic = topic;
            imtMt.messages[tempTail].length = length;
            queued = true;

            imtMt.tail = (tempTail + 1) % imtMt.maxLength; // Update tail pointer
            imtMt.count++; // Increase message count
        }
    }
    return queued;
}

// =============================================================================
// QUEUE CONTROL FUNCTIONS
// =============================================================================

/**
 * @brief Lock or unlock the MT queue to prevent message overwriting
 * 
 * When the MT queue is locked (lock = true), the oldest entry will not be
 * overwritten when the queue is full. This is useful for preserving important
 * messages during high-traffic periods. When unlocked (lock = false), normal
 * overwriting behavior is restored.
 * 
 * @param lock true to lock the queue, false to unlock
 */
void imtQueueMtLock(bool lock)
{
    if(lock)
    {
        mtLock = true;
    }
    else
    {
        mtLock = false;
    }
}

// =============================================================================
// QUEUE RETRIEVAL FUNCTIONS
// =============================================================================

/**
 * @brief Get the first (oldest) MO message without removing it
 * 
 * Returns a pointer to the oldest message in the MO queue without modifying
 * the queue structure. This allows for message inspection before processing
 * or removal.
 * 
 * @return Pointer to the first MO message, or NULL if queue is empty
 */
imt_t * imtQueueMoGetFirst(void)
{
    imt_t * mo = NULL;

    if(imtMo.count > 0)
    {
        mo = &imtMo.messages[imtMo.head];
    }

    return mo;
}

/**
 * @brief Get the first (oldest) MT message without removing it
 * 
 * Returns a pointer to the oldest message in the MT queue without modifying
 * the queue structure. This allows for message inspection before processing
 * or removal.
 * 
 * @return Pointer to the first MT message, or NULL if queue is empty
 */
imt_t * imtQueueMtGetFirst(void)
{
    imt_t * mt = NULL;

    if(imtMt.count > 0)
    {
        mt = &imtMt.messages[imtMt.head];
    }

    return mt;
}

/**
 * @brief Get the last (most recently added) MT message
 * 
 * Returns a pointer to the most recently added message in the MT queue
 * without modifying the queue structure. This is useful for accessing
 * the latest received message.
 * 
 * @return Pointer to the last MT message, or NULL if queue is empty
 */
imt_t * imtQueueMtGetLast(void)
{
    imt_t * mt = NULL;

    if(imtMt.count > 0)
    {
        mt = &imtMt.messages[(imtMt.tail == 0) ? (imtMt.maxLength - 1) : (imtMt.tail - 1)];
    }

    return mt;
}

// =============================================================================
// QUEUE REMOVAL FUNCTIONS
// =============================================================================

/**
 * @brief Remove the first MO message from the queue
 * 
 * Removes the oldest message from the MO queue and advances the head pointer.
 * The message buffer and metadata are cleared to prepare for future use.
 * This function is typically called after successful message processing.
 * 
 * @return true if a message was removed, false if queue is empty
 */
bool imtQueueMoRemove(void)
{
    bool removed = false;
    imt_t * mo = imtQueueMoGetFirst();
    if(mo != NULL)
    {
        uint16_t tempHead = imtMo.head;
        memset(mo->buffer, 0, IMT_PAYLOAD_SIZE); // Clear buffer
        mo->id = 0;
        mo->topic = 0;
        mo->length = 0;
        mo->readyToProcess = false;
        mo->ready = false;

        imtMo.head = (tempHead + 1) % imtMo.maxLength; // Update head pointer
        imtMo.count--; // Decrease message count
        removed = true;
    }
    return removed;
}

/**
 * @brief Remove the first MT message from the queue
 * 
 * Removes the oldest message from the MT queue and advances the head pointer.
 * The message buffer and metadata are cleared to prepare for future use.
 * This function is typically called after successful message processing.
 * 
 * @return true if a message was removed, false if queue is empty
 */
bool imtQueueMtRemove(void)
{
    bool removed = false;
    imt_t * mt = imtQueueMtGetFirst();
    if(mt != NULL)
    {
        uint16_t tempHead = imtMt.head;
        memset(mt->buffer, 0, IMT_PAYLOAD_SIZE); // Clear buffer
        mt->id = 0;
        mt->topic = 0;
        mt->length = 0;
        mt->readyToProcess = false;
        mt->ready = false;

        imtMt.head = (tempHead + 1) % imtMt.maxLength; // Update head pointer
        imtMt.count--; // Decrease message count
        removed = true;
    }
    return removed;
}

// =============================================================================
// QUEUE INITIALIZATION FUNCTIONS
// =============================================================================

/**
 * @brief Initialize both MO and MT queues
 * 
 * Performs complete initialization of both message queues by:
 * - Clearing all message buffers and metadata
 * - Resetting queue state variables (head, tail, count)
 * - Setting maximum queue lengths
 * - Preparing queues for first use
 * 
 * This function should be called once at system startup before any
 * queue operations are performed.
 */
void imtQueueInit(void)
{
    for (uint16_t i = 0; i < IMT_QUEUE_SIZE; i++)
    {
        // Initialize MO queue entry
        imtMo.messages[i].buffer = imtMoBuffer[i];
        memset(imtMo.messages[i].buffer, 0, IMT_PAYLOAD_SIZE);
        imtMo.messages[i].id = 0;
        imtMo.messages[i].length = 0;
        imtMo.messages[i].ready = false;
        imtMo.messages[i].readyToProcess = false;
        imtMo.messages[i].topic = 0;

        // Initialize MT queue entry
        imtMt.messages[i].buffer = imtMtBuffer[i];
        memset(imtMt.messages[i].buffer, 0, IMT_PAYLOAD_SIZE);
        imtMt.messages[i].id = 0;
        imtMt.messages[i].length = 0;
        imtMt.messages[i].ready = false;
        imtMt.messages[i].readyToProcess = false;
        imtMt.messages[i].topic = 0;
    }

    // Reset queue state (MO)
    imtMo.head = 0;
    imtMo.tail = 0;
    imtMo.count = 0;
    imtMo.maxLength = IMT_QUEUE_SIZE;

    // Reset queue state (MT)
    imtMt.head = 0;
    imtMt.tail = 0;
    imtMt.count = 0;
    imtMt.maxLength = IMT_QUEUE_SIZE;
}
