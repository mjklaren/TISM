/* 

  TISM_messaging.c
  ================
  Structure and tools for thread-safe messaging between tasks using circular buffers.

  Message queueing is based on using circular buffers:
  - One producer (head), one consumer (tail).
  - Buffer length determined by MAX_MESSAGES.
  - Buffer is full when head is about to over the tail ("head + 1 = tail"):
    - This means one slot will always remain empty!
    - Actual capacity is MAX_MESSAGES-1
  - Buffer is empty when head = tail.
  - New data is rejected when the buffer is full; write-function returns 'false' in such cases.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <sys/time.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  Description
  Check if a message is waiting in the circular buffer for the consumer. One or more messages are waiting when head != tail.
  Count the number of messages by calculating the delta between head and tail.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  <value> - Integer value of number of messages waiting
  0       - No message(s) waiting
*/
uint16_t TISM_CircularBufferMessagesWaiting(struct TISM_CircularBuffer *Buffer)
{
  if(Buffer->Head!=Buffer->Tail)
  {
    // Head and Tail differ; there must be messages waiting. Calculate how many.
    if(Buffer->Head>Buffer->Tail)
    {
      return(Buffer->Head-Buffer->Tail);
    }
    else
      return((MAX_MESSAGES-Buffer->Tail)+Buffer->Head);
  }
  else
    return(0);
}


/*
  Description
  Calculate the number of slots available in the buffer. As there is always one slot unused between head and tail, 
  max capacity will be MAX_MESSAGES-(number of messages waiting)-1.
  This function is somewhat redundant to TISM_CircularBufferMessagesWaiting, but used for readability of the code.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  <value> - Integer value of number of slots available.
*/
uint16_t TISM_CircularBufferSlotsAvailable(struct TISM_CircularBuffer *Buffer)
{
  return(MAX_MESSAGES-TISM_CircularBufferMessagesWaiting(Buffer)-1);
}


/*
  Description
  Read the first unread message from stack, do not delete (don't move tail)

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  *TISM_message - Pointer to message of type struct TISM_Message; the current message in the buffer.
*/
struct TISM_Message *TISM_CircularBufferRead (struct TISM_CircularBuffer *Buffer)
{
  // Is a message waiting? If not, return NULL.
  if(TISM_CircularBufferMessagesWaiting(Buffer))
  {
    return(&(Buffer->Message[Buffer->Tail]));
  }
  else
    return(NULL);
}


/*
  Description
  Remove the first unread message from stack by advancing the tail +1.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/
void TISM_CircularBufferDelete (struct TISM_CircularBuffer *Buffer)
{
  // Is the tail already at the same position as the head? Then we don't have to do anything.
  if(TISM_CircularBufferMessagesWaiting(Buffer)>0)
  {
    Buffer->Tail++;
    if(Buffer->Tail==MAX_MESSAGES)
      Buffer->Tail=0;
  }
}


/*
  Description
  Insert data into the current position of the circular buffer (head) and and advance head +1. This function allows the 
  timestamp to be specified by the user. This can be usefull (for instance) for logging.

  Parameters:
  *TISM_CircularBuffer     - Pointer to the TISM_CircularBuffer struct.
  uint8_t SenderTaskID     - ID of the sender of the message.
  uint8_t RecipientTaskID  - ID of the intended recipient of the message.
  uint8_t MessageType      - Type of message.
  uint32_t Message         - Message; could also contain a pointer to something (e.g. text buffer).
  uint32_t Specification   - Specification to the provided Message; could also contain a pointer to something (e.g. text buffer).
  uint64_t Timestamp       - Timestamp to be added to the message.

  Return value:
  false - Buffer is full.
  true  - Succes
*/
bool TISM_CircularBufferWriteWithTimestamp (struct TISM_CircularBuffer *Buffer, uint8_t SenderTaskID, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Message, uint32_t Specification, uint64_t Timestamp)
{
  // First check if a slot is available in the ringbuffer
  if(TISM_CircularBufferSlotsAvailable(Buffer)>0)
  {
    // Write a record to the current position of the head-pointer and add a timestamp.
    Buffer->Message[Buffer->Head].SenderTaskID=SenderTaskID;
    Buffer->Message[Buffer->Head].RecipientTaskID=RecipientTaskID;
    Buffer->Message[Buffer->Head].MessageType=MessageType;
    Buffer->Message[Buffer->Head].Message=Message;
    Buffer->Message[Buffer->Head].Specification=Specification;
    Buffer->Message[Buffer->Head].MessageTimestamp=Timestamp;
    
    // Advance the head-pointer +1; set to 0 if the end is reached. If the buffer is full, do not advance the head pointer.  
    Buffer->Head++;
    if(Buffer->Head==MAX_MESSAGES)
      Buffer->Head=0;
  }
  else
  {
    // No slots available; circulair buffer is full.
    return (false);
  }
  return(true);
}


/*
  Description
  Insert data into the current position of the circular buffer (head) and and advance head +1. Add a timestamp.

  Parameters:
  *TISM_CircularBuffer     - Pointer to the TISM_CircularBuffer struct.
  uint8_t SenderTaskID     - ID of the sender of the message.
  uint8_t RecipientTaskID  - ID of the intended recipient of the message.
  uint8_t MessageType      - Type of message.
  uint32_t Message         - Message; could also contain a pointer to something (e.g. text buffer).
  uint32_t Specification   - Specification to the provided Message; could also contain a pointer to something (e.g. text buffer).

  Return value:
  false - Buffer is full.
  true  - Succes
*/
bool TISM_CircularBufferWrite (struct TISM_CircularBuffer *Buffer, uint8_t SenderTaskID, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Message, uint32_t Specification)
{
  return(TISM_CircularBufferWriteWithTimestamp(Buffer, SenderTaskID, RecipientTaskID, MessageType, Message, Specification, time_us_64()));
} 


/*
  Description
  (Virtually) remove all messages by setting the tail at the same position as the head.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/
void TISM_CircularBufferClear (struct TISM_CircularBuffer *Buffer)           
{
  // 'Remove' all messages by setting the tail pointer to the same position as head. All data in the buffer is ignored.
  Buffer->Tail=Buffer->Head;
}


/*
  Description
  Initialize the circular buffer. For read safety, add default values.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/
void TISM_CircularBufferInit (struct TISM_CircularBuffer *Buffer)           
{
  // Set head and tail to 0.
  Buffer->Tail=0;
  Buffer->Head=0;

  // Give the slots in the buffer an initial value (not needed, but perhaps safer for reading operations).
  for(uint8_t counter=0;counter<MAX_MESSAGES; counter++)
  {
    Buffer->Message[counter].SenderTaskID=0;
    Buffer->Message[counter].RecipientTaskID=0;
    Buffer->Message[counter].MessageType=0;
    Buffer->Message[counter].Message=0;
    Buffer->Message[counter].Specification=0;
    Buffer->Message[counter].MessageTimestamp=0;
  }
}
