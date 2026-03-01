/*

  TISM_UartMX.c
  =============
  Library for exchange of TISM-messages via the UART, either P2P using a serial cable, using devices like the
  DT-06, HC-12 or RS485 adapters. Supports multi-host communication. 

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <string.h>
#include "TISM.h"


#ifndef TISM_DISABLE_UARTMX          // In case UartMX is disabled in TISM.h, to prevent compilation errors.


// Local data for the UartMX task
static uint8_t IRQ, Head, Tail, TISM_NetworkManagerTaskID;
static char UartBuffer[UARTMX_BUFFER];        // Circular buffer.
static uint8_t MessageTypeToTaskMapping[255]; // Mapping of MessageType to TaskID for subscribed message types.

// Packet parsing state machine
typedef enum {RX_IDLE, RX_COLLECTING, RX_VALIDATING} RxState_t;
static RxState_t RxState;
static uint8_t RxPacketBuffer[UARTMX_MAX_PACKET_SIZE];          // Maximum packet size
static uint8_t RxPacketIndex;
static uint64_t RxPacketStartTime;

// Transmit retry mechanism - sliding window
typedef struct 
{
  uint8_t Active;                                       // 0=empty slot, 1=waiting for ACK
  uint8_t RetryCount;                                   // Number of retries (max 3)
  uint64_t SendTime;                                    // Timestamp of (last) transmission
  uint8_t SequenceNumber;                               // Sequence number for this packet
  uint8_t PacketBuffer[UARTMX_MAX_PACKET_SIZE];         // Copy of transmitted packet
  uint8_t PacketLength;                                 // Length of packet
  uint8_t RecipientHostID;                              // For tracking ACK source
  uint8_t RecipientTaskID;                              // For tracking ACK source
} TxRetrySlot;
static TxRetrySlot TxRetryBuffer[UARTMX_RETRY_BUFFER];  // Sliding window of UARTMX_RETRY_BUFFER packets
static uint8_t TxRetryBufferEntries, TxSequenceNumber;  // 8-bit sequence number counter

// Duplicate packet detection; keep a small history of recently accepted packets
typedef struct
{
    uint8_t SenderHostID;
    uint8_t SequenceNumber;
} RxSequenceEntry;
static RxSequenceEntry RxSequenceHistory[UARTMX_RETRY_BUFFER];
static uint8_t RxSequenceHead = 0;


/*
  UART interrupt handler; read data from the UART and write to the circular buffer for later processing. No error checking.
*/
void TISM_UartMXHandler()
{
  uint8_t counter=0;
  while(uart_is_readable(UARTMX_UARTID) && counter<UARTMX_MAX_INCOMPLETE_BYTES)
  {
    if(((Head>Tail?Head-Tail:UARTMX_BUFFER-Tail+Head)>0))
    {
      UartBuffer[Head]=uart_getc(UARTMX_UARTID);
      Head++;
      if(Head==UARTMX_BUFFER)
        Head=0;
      counter++;
    }
    else
    {
      // Buffer full. Don't attempt to store data, but generate a warning.
      fprintf(STDERR, "TISM_UartMX: incoming data buffer full.\\n");
      fflush(STDERR);
    }
  }
}


/*
  Check if any incoming packets are waiting for processing; return the number of bytes waiting.
*/
uint8_t TISM_UartMXPacketsWaiting()
{
  if(Head!=Tail)
    return(Head>Tail?Head-Tail:UARTMX_BUFFER-Tail+Head);
  else
    return(0);
}


/*
  Use messaging instead of changing mappings directly for thread safety.
*/
bool TISM_UartMXSubscribe(TISM_Task ThisTask, uint8_t MessageType)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_UartMXTaskID,TISM_MX_SUBSCRIBE,MessageType,0));
}


/*
  Use messaging instead of changing mappings directly for thread safety.
*/
bool TISM_UartMXUnsubscribe(TISM_Task ThisTask, uint8_t MessageType)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_UartMXTaskID,TISM_MX_UNSUBSCRIBE,MessageType,0));
}


/*
  Description:
  Calculate CRC-16-CCITT checksum using bit-by-bit-fast algorithm.
  
  Parameters:
  const uint8_t *Data - Pointer to data buffer.
  size_t Length       - Number of bytes to process.

  Return value:
  uint16_t            - Calculated CRC value.
*/
static uint16_t UartMXCalculateCRC16(const uint8_t *Data, size_t Length) 
{
  uint16_t CRC=UARTMX_CRC_INIT_VALUE;
  uint8_t BitCounter;
  size_t ByteCounter;
  for(ByteCounter=0; ByteCounter<Length; ByteCounter++) 
  {
    CRC^=(uint16_t)Data[ByteCounter]<<8;
    for(BitCounter=0; BitCounter<8; BitCounter++) 
    {
      if(CRC&0x8000) 
        CRC=(CRC<<1)^UARTMX_CRC_POLINOMIAL;
      else 
        CRC<<=1;
    }
  }
  return(CRC);
}


/*
  Read one byte from circular buffer.
  Return: true if byte read successfully, false if buffer empty.
*/
static bool TISM_UartMXReadByte(uint8_t *Byte)
{
  if(Tail!=Head)
  {
    *Byte=(uint8_t)UartBuffer[Tail];
    Tail++;
    if(Tail==UARTMX_BUFFER)
      Tail=0;
    return true;
  }
  return false;
}


/*
  Peek at byte at 'offset' positions from current Tail, without modifying buffer.
  Return: true if byte available, false otherwise.
*/
static bool TISM_UartMXPeekByte(uint16_t Offset, uint8_t *Byte)
{
  uint16_t Available=(Head>=Tail)?(Head-Tail):(UARTMX_BUFFER-Tail+Head);
  if(Offset>=Available)
    return false;
  uint16_t Position=(Tail+Offset);
  if(Position>=UARTMX_BUFFER)
    Position-=UARTMX_BUFFER;
  *Byte=(uint8_t)UartBuffer[Position];
  return true;
}


/*
  Drop 'count' bytes from the circular buffer.
*/
static void TISM_UartMXDropBytes(uint16_t Count)
{
  for(uint16_t i=0;i<Count;i++)
  {
    if(Tail==Head)
      break;
    Tail++;
    if(Tail==UARTMX_BUFFER)
      Tail=0;
  }
}


/*
  Search for START_MARKER in the circular buffer.
  Return: offset of START_MARKER, or 0xFFFF if not found.
*/
static uint16_t TISM_UartMXFindStartMarker()
{
  uint16_t available=(Head>=Tail)?(Head-Tail):(UARTMX_BUFFER-Tail+Head);
  uint8_t byte;
  
  // Search up to UARTMX_MAX_PACKET_SIZE bytes (max packet size) or available bytes
  uint16_t searchLimit=(available<UARTMX_MAX_PACKET_SIZE)?available:UARTMX_MAX_PACKET_SIZE;
  for(uint16_t Counter=0; Counter<searchLimit; Counter++)
  {
    if(TISM_UartMXPeekByte(Counter, &byte) && byte==UARTMX_START_MARKER)
      return Counter;
  }
  return 0xFFFF;
}


/*
  Validate message type against corresponding packet size and message size field.
  Return: true if combination is valid according to protocol.
*/
static bool TISM_UartMXValidateMessage(uint8_t MessageType, uint8_t MessageSize, uint8_t PacketSize, uint8_t SenderTaskID, uint8_t RecipientHostID, uint8_t RecipientTaskID, uint8_t Payload0LSB)
{
  // Validate specific TISM message types according to protocol specification
  switch(MessageType)
  {
    case TISM_ACK: 
    case TISM_NAK:                    return (MessageSize==0x01 && PacketSize==UARTMX_ACKNAK_PACKET_SIZE && SenderTaskID==0x00 && RecipientHostID!=UARTMX_BROADCAST && RecipientTaskID==0x00);
    case TISM_PING:                   
    case TISM_ECHO:                   return (MessageSize==0x04 && PacketSize==UARTMX_PINGECHO_PACKET_SIZE && SenderTaskID!=UARTMX_BROADCAST && RecipientHostID!=0x00 && RecipientHostID!=UARTMX_BROADCAST && RecipientTaskID!=UARTMX_BROADCAST);
    case TISM_INTRODUCE:              return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && SenderTaskID!=UARTMX_BROADCAST && RecipientHostID==UARTMX_BROADCAST && RecipientTaskID==0x00);
    case TISM_DISCOVER:               return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && SenderTaskID==0x00 && RecipientHostID!=0x00 && RecipientTaskID==0x00);
    case TISM_RESOLVE_TASKNAME:       return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && SenderTaskID==0x00 && RecipientHostID!=0x00 && RecipientTaskID==0x00);
    case TISM_RESOLVE_TASKNAME_REPLY: return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && RecipientHostID==0xFF && RecipientTaskID==0x00);
    case TISM_RESOLVE_TASKID:         return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && SenderTaskID==0x00 && RecipientHostID!=0x00 && (RecipientHostID==UARTMX_BROADCAST && (uint8_t)Payload0LSB==UARTMX_BROADCAST?false:true) && RecipientTaskID==0x00);
    case TISM_RESOLVE_TASKID_REPLY:   return (MessageSize==0x08 && PacketSize==UARTMX_HOSTDISC_PACKET_SIZE && SenderTaskID!=UARTMX_BROADCAST && RecipientHostID==UARTMX_BROADCAST && RecipientTaskID==0x00);
    case TISM_TEST:
    default:                          // For test and user-defined message types: accept if MESSAGE_SIZE is valid 
                                      // and packet size matches expected size (UARTMX_MIN_PACKET_SIZE + MessageSize)
                                      return (PacketSize==(UARTMX_MIN_PACKET_SIZE+MessageSize) && SenderTaskID!=UARTMX_BROADCAST);
  }
}


/*
  Send packet via UART.
  Return: true if send successful.
*/
static bool TISM_UartMXSendPacket(const uint8_t *Packet, uint8_t Length)
{
  if(Length>UARTMX_MAX_PACKET_SIZE)
    return false;
  for(uint8_t Counter=0; Counter<Length; Counter++)
  {
    while(!uart_is_writable(UARTMX_UARTID)) {}
    uart_putc_raw(UARTMX_UARTID, Packet[Counter]);
  }
  
  // Wait until the FIFO buffer is empty
  //uart_tx_wait_blocking(UARTMX_UARTID);
  return true;
}


/*
  Build a UartMX packet from a TISM_Message.
  Return: packet length in bytes.
*/
static uint8_t TISM_UartMXBuildPacket(TISM_Message *Message, uint8_t *Packet, uint8_t SequenceNumber)
{
  uint8_t Index=0;
  uint8_t MessageSize;
  
  // Determine message size (payload) based on which fields are populated
  // Standard/pre-defined TISM message types have fixed MessageSize values.
  switch(Message->MessageType)
  {
    case TISM_ACK: 
    case TISM_NAK:                    MessageSize=1;
                                      break;
    case TISM_PING:                   
    case TISM_ECHO:                   MessageSize=4;
                                      break;
    case TISM_INTRODUCE:
    case TISM_DISCOVER: 
    case TISM_RESOLVE_TASKNAME: 
    case TISM_RESOLVE_TASKNAME_REPLY: 
    case TISM_RESOLVE_TASKID:         
    case TISM_RESOLVE_TASKID_REPLY:  MessageSize=8;
                                     break;
    default:                         // Other message types can have different payload sizes
                                     if(Message->MessageTimestamp!=0)
                                     {
                                        MessageSize=UARTMX_MAX_PAYLOAD_SIZE;  // Full payload + Timestamp
                                     }
                                     else if(Message->Payload1!=0)
                                     {
                                       MessageSize=8;   // Both payloads
                                     }
                                     else if(Message->Payload0!=0)
                                     {
                                       MessageSize=4;   // Only Payload0
                                     }
                                     else
                                     {
                                       MessageSize=0;   // No payload
                                     }
                                     break; 
  }
  
  // START_MARKER
  Packet[Index++]=UARTMX_START_MARKER;
  
  // First header byte: VERSION_ID (high nibble) + NETWORK_ID (low nibble)
  Packet[Index++]=(UARTMX_VERSION<<4) | (System.NetworkID & 0x0F);
  
  // Second header byte: sequence number
  Packet[Index++]=SequenceNumber;
  
  // MESSAGE_SIZE
  Packet[Index++]=MessageSize;
  
  // TISM Message fields
  Packet[Index++]=Message->SenderHostID;
  Packet[Index++]=Message->SenderTaskID;
  Packet[Index++]=Message->RecipientHostID;
  Packet[Index++]=Message->RecipientTaskID;
  Packet[Index++]=Message->MessageType;
  
  // Payload based on message type
  if(Message->MessageType==TISM_ACK || Message->MessageType==TISM_NAK)
  {
    // ACK/NAK: 1 byte payload (sender's sequence number)
    Packet[Index++]=(uint8_t)(Message->Payload0 & 0xFF);
  }
  else
  {
    // Regular message: optional payloads
    if(MessageSize>=4)                                  // Payload0, 4 bytes
    {
      Packet[Index++]=(Message->Payload0 >> 24) & 0xFF;
      Packet[Index++]=(Message->Payload0 >> 16) & 0xFF;
      Packet[Index++]=(Message->Payload0 >> 8) & 0xFF;
      Packet[Index++]=Message->Payload0 & 0xFF;
    }
    if(MessageSize>=8)                                  // Payload1, additional4 bytes
    {
      Packet[Index++]=(Message->Payload1 >> 24) & 0xFF;
      Packet[Index++]=(Message->Payload1 >> 16) & 0xFF;
      Packet[Index++]=(Message->Payload1 >> 8) & 0xFF;
      Packet[Index++]=Message->Payload1 & 0xFF;
    }
    if(MessageSize==16)                                 // Timestamp, additional 8 bytes
    {
      Packet[Index++]=(Message->MessageTimestamp >> 56) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 48) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 40) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 32) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 24) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 16) & 0xFF;
      Packet[Index++]=(Message->MessageTimestamp >> 8) & 0xFF;
      Packet[Index++]=Message->MessageTimestamp & 0xFF;
    }
  }
  
  // Calculate CRC over all bytes from index 1 (after START_MARKER) up to current position
  uint16_t CRC=UartMXCalculateCRC16(&Packet[1], Index-1);
  Packet[Index++]=(CRC >> 8) & 0xFF;
  Packet[Index++]=CRC & 0xFF;
  
  // END_MARKER
  Packet[Index++] = UARTMX_END_MARKER;
  return Index;
}


/*
  Add packet to retry buffer for ACK/NAK handling.
*/
static void TISM_UartMXAddToRetryBuffer(TISM_Task ThisTask, const uint8_t *Packet, uint8_t Length, uint8_t SequenceNumber, uint8_t RecipientHostID, uint8_t RecipientTaskID)
{
  // Find an empty slot
  for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
  {
    if(TxRetryBuffer[Counter].Active==0)
    {
      TxRetryBuffer[Counter].Active=1;
      TxRetryBuffer[Counter].RetryCount=0;
      TxRetryBuffer[Counter].SendTime=time_us_64();
      TxRetryBuffer[Counter].SequenceNumber=SequenceNumber;
      TxRetryBuffer[Counter].RecipientHostID=RecipientHostID;
      TxRetryBuffer[Counter].RecipientTaskID=RecipientTaskID;
      TxRetryBuffer[Counter].PacketLength=Length;
      for(uint8_t Counter2=0; Counter2<Length && Counter2<UARTMX_MAX_PACKET_SIZE; Counter2++)
        TxRetryBuffer[Counter].PacketBuffer[Counter2]=Packet[Counter2];
      TxRetryBufferEntries++;
      return;
    }
  }
  
  // No empty slot found
  TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Transmit retry buffer full, dropping packet.");
}


/*
  Process received ACK/NAK and remove from retry buffer.
*/
static void TISM_UartMXProcessAckNak(uint8_t SequenceNumber, uint8_t IsAck)
{
  // For now, only ACK packets are processed and sent packages are removed from the retry buffer.
  // Packets are retried until ACK is received or maximum retries reached.
  if(IsAck)
  {
    for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
    {
      if(TxRetryBuffer[Counter].Active && TxRetryBuffer[Counter].SequenceNumber==SequenceNumber)
      {
        TxRetryBuffer[Counter].Active=0;  // Remove from buffer
        return;
      }
    }
  }
}


/*
  Process retry timeouts for all active slots in retry buffer.
  Returns number of packets in the retry buffer.
*/
static uint8_t TISM_UartMXProcessRetries(TISM_Task ThisTask)
{
  uint64_t CurrentTime=time_us_64();
  TxRetryBufferEntries=0;  // Recalculate below
  for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
  {
    if(TxRetryBuffer[Counter].Active)
    {
      TxRetryBufferEntries++;

      // Check timeout
      if((CurrentTime-TxRetryBuffer[Counter].SendTime)>UARTMX_TX_TIMEOUT_US)
      {
        if(TxRetryBuffer[Counter].RetryCount<UARTMX_TX_RETRIES)
        {
          // Retry transmission
          TxRetryBuffer[Counter].RetryCount++;
          TxRetryBuffer[Counter].SendTime=CurrentTime;
          TISM_UartMXSendPacket(TxRetryBuffer[Counter].PacketBuffer, TxRetryBuffer[Counter].PacketLength);

          // Dump full packet (when debugging)
          if(ThisTask.TaskDebug) 
          {
            char Buffer[UARTMX_MAX_PACKET_SIZE*3+1];
            TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Retry outbound raw data (hex):");
            Buffer[0]='\0';                    
            for(uint8_t Counter2=0; Counter2<TxRetryBuffer[Counter].PacketLength; Counter2++) 
              snprintf(Buffer+(Counter2*3), 4, " %02X", TxRetryBuffer[Counter].PacketBuffer[Counter2]);
            TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "%s - SequenceNo. %02X (hex)", Buffer, TxRetryBuffer[Counter].SequenceNumber);
            TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Retry %d for UARTMX_SEQ_NUMBER %d to Host %d Task %d", TxRetryBuffer[Counter].RetryCount, TxRetryBuffer[Counter].SequenceNumber, TxRetryBuffer[Counter].RecipientHostID, TxRetryBuffer[Counter].RecipientTaskID);
          }
        }
        else
        {
          // Maximum retries reached - report error and remove from buffer
          TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "No ACK after 3 retries for UARTMX_SEQ_NUMBER %d to HostID %d TaskID %d\n", TxRetryBuffer[Counter].SequenceNumber, TxRetryBuffer[Counter].RecipientHostID, TxRetryBuffer[Counter].RecipientTaskID);
          
          // Remove from buffer
          TxRetryBuffer[Counter].Active=0;
          TxRetryBufferEntries--;
        }
      }
    }
  }
  return(TxRetryBufferEntries);
}


/*
  Description:
  ...

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_UartMX (TISM_Task ThisTask)
{
  if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize the UART.
                if(ThisTask.TaskDebug)
                {
                  TISM_EventLoggerLogEvent(ThisTask,TISM_LOG_EVENT_NOTIFY,"Initializing with priority %d.", ThisTask.TaskPriority);
                  TISM_EventLoggerLogEvent(ThisTask,TISM_LOG_EVENT_NOTIFY,"Our Host ID is %d.", System.HostID);
                }

                // Clear the MessageTypeToTaskMapping list and the UartMX circular buffer.
                TISM_NetworkManagerTaskID=TISM_GetTaskID("T_NetMgr");
                for(uint8_t Counter=0; Counter<255; Counter++)
                  MessageTypeToTaskMapping[Counter]=TISM_NetworkManagerTaskID;     // Default: all message types mapped to the TISM_NetworkManager task.

                // Empty the sliding window by setting all fields to '0'.
                for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
                {
                  TxRetryBuffer[Counter].Active=0;
                  TxRetryBuffer[Counter].RetryCount=0;
                  TxRetryBuffer[Counter].SendTime=0;
                  TxRetryBuffer[Counter].SequenceNumber=0;
                  TxRetryBuffer[Counter].PacketLength=0;
                  TxRetryBuffer[Counter].RecipientHostID=0;
                  TxRetryBuffer[Counter].RecipientTaskID=0;
                }
                TxRetryBufferEntries=0;
                
                // Initialize TX sequence number, RX state machine and circular buffer
                TxSequenceNumber=0;
                RxState=RX_IDLE;
                RxPacketIndex=0;
                RxPacketStartTime=0;
                Head=0;
                Tail=0;

                // Initialize received packets circular buffer
                for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
                {
                  RxSequenceHistory[Counter].SenderHostID=0x00; // 0x00 is never a valid SenderHostID
                  RxSequenceHistory[Counter].SequenceNumber=0;
                }

                // Configure the UART
                uart_init(UARTMX_UARTID,UARTMX_BAUDRATE);
                gpio_set_function(UARTMX_TXGPIO,GPIO_FUNC_UART);
                gpio_set_function(UARTMX_RXGPIO,GPIO_FUNC_UART);
                uart_set_format(UARTMX_UARTID,8,1,UART_PARITY_NONE);
  
                // Set an interrupt handler to process incoming messages
                uart_set_fifo_enabled(UARTMX_UARTID,true);
                IRQ=(UARTMX_UARTID==uart0?UART0_IRQ:UART1_IRQ);
                irq_set_exclusive_handler(IRQ,TISM_UartMXHandler);
                irq_set_enabled(IRQ,true);
                uart_set_irq_enables(UARTMX_UARTID,true,false);
    
                // This task is executed by the scheduler only if incoming data is received.
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.		
                if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // Process incoming packets from UartBuffer using state machine
                while(TISM_UartMXPacketsWaiting()>0)
                {
                  switch(RxState)
                  {
                    case RX_IDLE:         // Search for START_MARKER in buffer
                                          uint16_t offset=TISM_UartMXFindStartMarker();
                                          if(offset!=0xFFFF)
                                          {
                                            // Found START_MARKER - drop all bytes before it
                                            if(offset>0)
                                              TISM_UartMXDropBytes(offset);
                                                
                                            // Begin packet collection
                                            RxState=RX_COLLECTING;
                                            RxPacketIndex=0;
                                            RxPacketStartTime=time_us_64();
                                            TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_SLEEP, false);  // Wake this task up
                                          }
                                          else
                                          {
                                            // No START_MARKER found - drop one byte to prevent blocking
                                            if(TISM_UartMXPacketsWaiting()>0)
                                              TISM_UartMXDropBytes(1);
                                          }
                                          break;                    
                    case RX_COLLECTING:   uint8_t Byte;

                                          // Check buffer overflow BEFORE writing
                                          if(RxPacketIndex>=UARTMX_MAX_PACKET_SIZE)
                                          {
                                            // Buffer full without valid END_MARKER
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "RX packet too long, dropping.");

                                            RxState=RX_IDLE;
                                            RxPacketIndex=0;
                                            break;
                                          }

                                          // Read the UART FIFO until we have a packet.
                                          while(TISM_UartMXReadByte(&Byte) && RxState==RX_COLLECTING)
                                          {
                                            // Write byte to packet buffer
                                            RxPacketBuffer[RxPacketIndex++]=Byte;

                                            // Check for packet timeout (100ms)
                                            if((time_us_64()-RxPacketStartTime)>UARTMX_RX_TIMEOUT_US)
                                            {
                                              RxState=RX_IDLE;
                                              TISM_UartMXDropBytes(RxPacketIndex-1);  // Remove packet data from circular buffer.
                                              RxPacketIndex=0;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "RX packet timeout, dropping.");
                                              break;
                                            }
                                                
                                            // Check for END_MARKER (packet size is UARTMX_MIN_PACKET_SIZE + indicated packet size, in bytes). Make sure packet size remains valid.
                                            if(RxPacketIndex>=(UARTMX_MIN_PACKET_SIZE+(RxPacketBuffer[3]<=UARTMX_MAX_PAYLOAD_SIZE?RxPacketBuffer[3]:0)) && Byte==UARTMX_END_MARKER)
                                            {
                                              // Complete packet received - move to validation
                                              RxState=RX_VALIDATING;
                                            }
                                          }

                                          // Need to fall through to the next state?
                                          if(RxState!=RX_VALIDATING)
                                            break;                                           
                      case RX_VALIDATING: if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Validating RX packet.");

                                          uint8_t PacketLength=RxPacketIndex;
                                          bool PacketValid=true;
                                          uint8_t DropReason=OK;      // Error values: UARTMX_RX_ERR_START, UARTMX_RX_ERR_END, UARTMX_RX_ERR_LENGTH, UARTMX_RX_ERR_VERSION, UARTMX_RX_ERR_NETWORKID, UARTMX_RX_ERR_CRC, UARTMX_RX_ERR_MSGSIZE and UARTMX_RX_TISMMSG_ERR
                                            
                                          // Dump full packet (when debugging)
                                          if(ThisTask.TaskDebug) 
                                          {
                                            char Buffer[UARTMX_MAX_PACKET_SIZE*3+1];           // Each byte takes 3 chars (2 hex + space) + null terminator
                                            TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Inbound raw data (hex):");
                                            Buffer[0]='\0';                    
                                            for(uint8_t Counter=0; Counter<PacketLength; Counter++) 
                                              snprintf(Buffer+(Counter*3), 4, " %02X", RxPacketBuffer[Counter]);
                                            TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "%s", Buffer);
                                          }

                                          /*-------------------------------------
                                             Validate the received packet frame
                                             - Start marker
                                             - End marker
                                             - Packet length
                                             - CRC
                                           -------------------------------------*/
                                          // Check START marker. END marker and packet length. Stop further validating if packet frame is invalid.
                                          if(RxPacketBuffer[0]!=UARTMX_START_MARKER)
                                          {
                                            PacketValid=false;
                                            DropReason=UARTMX_RX_ERR_START;
                                            
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Start marker error, dropping packet.");
                                          }
                                          else if(RxPacketBuffer[PacketLength-1]!=UARTMX_END_MARKER)
                                          {
                                            PacketValid=false;
                                            DropReason=UARTMX_RX_ERR_END;
                                              
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "End marker error, dropping packet.");
                                          }
                                          else if(PacketLength<UARTMX_MIN_PACKET_SIZE || PacketLength>UARTMX_MAX_PACKET_SIZE)
                                          {
                                            PacketValid=false;
                                            DropReason=UARTMX_RX_ERR_LENGTH;
                                              
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Length error, dropping packet.");
                                          }

                                          // Check CRC16 - calculated over bytes from index 1 to (length-4).
                                          // CRC is stored at (length-3) and (length-2), END_MARKER at (length-1).
                                          if(PacketValid)
                                          {
                                            uint16_t ReceivedCRC=((uint16_t)RxPacketBuffer[PacketLength-3] << 8) | RxPacketBuffer[PacketLength-2];
                                            uint16_t CalculatedCRC=UartMXCalculateCRC16(&RxPacketBuffer[1], PacketLength-4);
                                            if(ReceivedCRC!=CalculatedCRC)
                                            {
                                              PacketValid=false;
                                              DropReason=UARTMX_RX_ERR_CRC;
                                                
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "CRC error, dropping packet.");
                                            }
                                          }

                                          /*-------------------------------------
                                             Validate the frame header
                                             - Version ID
                                             - Network ID
                                             - Message size
                                             - SenderHostID
                                             - RecipientHostID
                                           -------------------------------------*/
                                          // Complete and valid packet frame received?
                                          if(PacketValid)
                                          {
                                            // Extract header fields
                                            uint8_t HeaderByte1=RxPacketBuffer[1], VersionID=(HeaderByte1 >> 4) & 0x0F, NetworkID=HeaderByte1 & 0x0F, SequenceNumber=RxPacketBuffer[2], MessageSize=RxPacketBuffer[3];
                                            
                                            // Check VERSION_ID
                                            if(PacketValid && VersionID!=UARTMX_VERSION)
                                            {
                                              PacketValid=false;
                                              DropReason=UARTMX_RX_ERR_VERSION;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Protocol version error, dropping packet.");
                                            }
                                            
                                            // Check NETWORK_ID
                                            if(PacketValid && NetworkID!=System.NetworkID)
                                            {
                                              PacketValid=false;
                                              DropReason=UARTMX_RX_ERR_NETWORKID;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Network ID error, dropping packet.");
                                            }

                                            // Check MESSAGE_SIZE (payload), as we're transporting TISM-messages size must be either 0, 1, 4, 8, or 16)
                                            if(PacketValid && MessageSize!=0 && MessageSize!=1 && MessageSize!=4 && MessageSize!=8 && MessageSize!=16)
                                            {
                                              PacketValid=false;
                                              DropReason=UARTMX_RX_ERR_MSGSIZE;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Packet payload size error, dropping packet.");
                                            }                                            

                                            // Prevent invalid/'spoofed'/duplicate values for SenderHostID (cannot be 0x00 or our own HostID). Also drop packets not addressed to us (when not a broadcast).
                                            // Check SenderHostID and RecipientHostID for invalid values.
                                            if(PacketValid)
                                            {
                                              if(RxPacketBuffer[4]==System.HostID || RxPacketBuffer[4]==HOST_ID_LOCAL || RxPacketBuffer[4]==UARTMX_BROADCAST ||      // SenderHostID
                                                 RxPacketBuffer[6]==HOST_ID_LOCAL || (RxPacketBuffer[6]!=UARTMX_BROADCAST && RxPacketBuffer[6]!=System.HostID))      // RecipientHostID
                                              {
                                                PacketValid=false;
                                                DropReason=UARTMX_RX_ERR_SENDERHOSTID;
                                                
                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Invalid Sender/Recipient HostID value(s), dropping packet.");
                                              }
                                            }                                              

                                            /*------------------------------------------
                                              Validate TISM Message field combinations:
                                               - MessageType
                                               - Payload Size
                                               - Packet Length
                                               - SenderTaskID
                                               - RecipientHostID and RecipientTaskID
                                               - Payload0
                                           ------------------------------------------*/                                            
                                            // Extract TISM message fields
                                            uint8_t SenderHostID=RxPacketBuffer[4], SenderTaskID=RxPacketBuffer[5], RecipientHostID=RxPacketBuffer[6], RecipientTaskID=RxPacketBuffer[7], MessageType=RxPacketBuffer[8];

                                            // Validate TISM-message type vs packet size, according to the UARTMX protocol definition.
                                            if(PacketValid && !TISM_UartMXValidateMessage(MessageType, MessageSize, PacketLength, SenderTaskID, RecipientHostID, RecipientTaskID, RxPacketBuffer[12]))
                                            {
                                              PacketValid=false;
                                              DropReason=UARTMX_RX_TISMMSG_ERR;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Invalid TISM message structure for message type 0x%02X, dropping packet.", MessageType);
                                            }
          
                                            /* ------------------------------------------
                                              Duplicate detection based on SenderHostID
                                              and UARTMX_SEQ_NUMBER
                                              ------------------------------------------*/
                                            // Default: assume this is not a duplicate
                                            bool IsDuplicate=false;

                                            // Only relevant for packets that passed all previous checks and are not ACK/NAK themselves.
                                            if(PacketValid && MessageType!=TISM_ACK && MessageType!=TISM_NAK)
                                            {
                                              // Check history buffer for same (SenderHostID, SequenceNumber)
                                              for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
                                              {
                                                if(RxSequenceHistory[Counter].SenderHostID==SenderHostID && RxSequenceHistory[Counter].SequenceNumber==SequenceNumber)
                                                {
                                                    IsDuplicate=true;

                                                    if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Duplicate packet detected from HostID %d, SequenceNr=%d.", SenderHostID, SequenceNumber);
                                                    break;
                                                }
                                              }

                                              // If not a duplicate, register this packet in the history buffer
                                              if(!IsDuplicate)
                                              {
                                                RxSequenceHistory[RxSequenceHead].SenderHostID=SenderHostID;
                                                RxSequenceHistory[RxSequenceHead].SequenceNumber=SequenceNumber;
                                                RxSequenceHead=(RxSequenceHead+1)%UARTMX_RETRY_BUFFER;
                                              }
                                            }

                                            /* ---------------------------------------------------
                                              Confirm receipt of packet via ACK/NAK:
                                              - Only when packets are directed to us
                                              - Only for non-broadcasts
                                              - Only non-ACK/NAK packets
                                              - Duplicate received packets are ALSO acknowledged
                                              ----------------------------------------------------*/  
                                            if(RecipientHostID==System.HostID && MessageType!=TISM_ACK && MessageType!=TISM_NAK)
                                            {
                                              // The ACK/NAK packet needs to be sent ASAP via the UART
                                              TISM_Message ACKNACKmessage={.SenderHostID=System.HostID,
                                                                           .SenderTaskID=0x00,
                                                                           .RecipientHostID=SenderHostID,
                                                                           .RecipientTaskID=0x00,
                                                                           .MessageType=(PacketValid?TISM_ACK:TISM_NAK),
                                                                           .Payload0=RxPacketBuffer[2]};
                                              uint8_t Packet[UARTMX_ACKNAK_PACKET_SIZE];
                                              TISM_UartMXSendPacket(&Packet[0], TISM_UartMXBuildPacket(&ACKNACKmessage, &Packet[0], TxSequenceNumber));
                                              TxSequenceNumber++;
                                              
                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "%s packet received, %s packet returned.", (PacketValid?"Valid":"Invalid"), (PacketValid?"ACK":"NAK"));
                                            }

                                            // ACK/NAK sent; if the received packet is a duplicate, invalidate it to prevent further processing.
                                            if(IsDuplicate)
                                              PacketValid=false;
                                          
                                            /* ------------------------------------------
                                               Checking done
                                               Process packet contents
                                             ------------------------------------------*/  
                                            if(PacketValid)
                                            {
                                              // Is the received packet a broadcast? Then rewrite the RecipientHostID to prevent issues with processing.
                                              if(RecipientHostID==UARTMX_BROADCAST)
                                                RecipientHostID=System.HostID;
                                                
                                              // Ensure certain TISM message types are sent to UartMX or NetworkManager.
                                              switch(MessageType)
                                              {
                                                case TISM_ACK:
                                                case TISM_NAK:                    RecipientTaskID=ThisTask.TaskID;            // ACK/NAK always handled by UartMX
                                                                                  break;
                                                case TISM_DISCOVER:               
                                                case TISM_INTRODUCE:
                                                case TISM_RESOLVE_TASKNAME:
                                                case TISM_RESOLVE_TASKNAME_REPLY:
                                                case TISM_RESOLVE_TASKID:
                                                case TISM_RESOLVE_TASKID_REPLY:   RecipientTaskID=TISM_NetworkManagerTaskID;  // These messages always handled by TISM Network Manager
                                                                                  break;
                                                // Other message types are routed to the RecipientTaskID as specified by the sender.                                                                              
                                              } 

                                              // Parse payload fields
                                              uint32_t Payload0=0, Payload1=0;
                                              uint64_t Timestamp=0;
                                              uint8_t DataIndex=9;  // Start of payload data

                                              // If no Timestamp in packet, use reception time
                                              // if(Timestamp==0)
                                                Timestamp=time_us_64();    // for now; overwrite the incoming timestamp with local time, as microcontroller clocks are not synchronized.

                                              // Parse packet based on message type; ACK/NAK packets are processed directly.
                                              if(MessageType==TISM_ACK || MessageType==TISM_NAK)
                                              {
                                                // ACK/NAK has 1 byte payload (sender's 8 bit sequence number)
                                                Payload0=RxPacketBuffer[DataIndex];

                                                // Process ACK/NAK - remove packet with corresponding sequence number from retry buffer
                                                //TISM_UartMXProcessAckNak((uint8_t)RxPacketBuffer[DataIndex], MessageType==TISM_ACK);
                                                TISM_UartMXProcessAckNak((uint8_t)Payload0, MessageType==TISM_ACK);
                                                
                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "%s packet processed for outbound message UARTMX_SEQ_NUMBER %d", (MessageType==TISM_ACK)?"ACK":"NAK", (uint8_t)Payload0);
                                              }
                                              else
                                              {
                                                // Regular packet and message payloads
                                                if(MessageSize>=4)
                                                {
                                                  Payload0=((uint32_t)RxPacketBuffer[DataIndex]<<24) | ((uint32_t)RxPacketBuffer[DataIndex+1]<<16) | ((uint32_t)RxPacketBuffer[DataIndex+2]<<8) | RxPacketBuffer[DataIndex+3];
                                                  DataIndex+=4;
                                                }                                                  
                                                if(MessageSize>=8)
                                                {
                                                  Payload1=((uint32_t)RxPacketBuffer[DataIndex] << 24) | ((uint32_t)RxPacketBuffer[DataIndex+1] << 16) | ((uint32_t)RxPacketBuffer[DataIndex+2] << 8) | RxPacketBuffer[DataIndex+3];
                                                  DataIndex+=4;
                                                }                                                  
                                                
                                                // for now; overwrite the incoming timestamp with local time, as microcontroller clocks are not synchronized.
                                                //if(MessageSize==16)
                                                //{
                                                //  Timestamp=((uint64_t)RxPacketBuffer[DataIndex] << 56) | ((uint64_t)RxPacketBuffer[DataIndex+1] << 48) | ((uint64_t)RxPacketBuffer[DataIndex+2] << 40) | ((uint64_t)RxPacketBuffer[DataIndex+3] << 32) |
                                                //            ((uint64_t)RxPacketBuffer[DataIndex+4] << 24) | ((uint64_t)RxPacketBuffer[DataIndex+5] << 16) | ((uint64_t)RxPacketBuffer[DataIndex+6] << 8) | RxPacketBuffer[DataIndex+7];
                                                //}

                                                // If RecipientTaskID is 0x00 (unknown) or 0xFF (broadcast) use subscription mapping to determine target task.
                                                // In most cases messages will be redirected to TISM_NetworkManager.
                                                if(RecipientTaskID==0 || RecipientTaskID==UARTMX_BROADCAST)
                                                {
                                                  RecipientTaskID=MessageTypeToTaskMapping[MessageType];
                                                  
                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Incoming message type redirected to TaskID %d.", RecipientTaskID);                                                  
                                                }

                                                // Finally, forward message to target task on this host.
                                                TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, SenderHostID, SenderTaskID, RecipientHostID, RecipientTaskID, MessageType, Payload0, Payload1, Timestamp);

                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Packet with message type %s (0x%02X) routed to Task ID %d.", TISM_MessageTypeToString(MessageType), MessageType, RecipientTaskID);
                                              }
                                            }                                      
                                          }
                                          
                                          // For debugging; did we drop an invalid packet?
                                          if(ThisTask.TaskDebug && !PacketValid)
                                          {
                                            // Invalid packet frame received, ignore.
                                            TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "RX invalid packet frame received, dropping.");
                                          }
                                            
                                          // Reset state machine for next packet
                                          RxState=RX_IDLE;
                                          TISM_UartMXDropBytes(PacketLength);  // Remove packet data from circular buffer.
                                          RxPacketIndex=0;
                                          break;
                  }                      
                }

                /* ----------------------------------------------------------------
                    Now process the UartMX's TISM message queue
                    - Route messages for other hosts
                    - Process messages intented for this task (eg. subscriptions)
                 -----------------------------------------------------------------*/  
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Processing message '%ld' type %s (0x%02X) from TaskID %d (HostID %d), addressed to TaskID %d (HostID %d).", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->RecipientTaskID, MessageToProcess->RecipientHostID);

                  // Is this a message that needs to be routed somewhere else?
                  if(MessageToProcess->RecipientHostID!=System.HostID)
                  {
                    // This message needs to be sent via the UART
                    uint8_t Packet[UARTMX_MAX_PACKET_SIZE], Length;

                    // Compose and send the packet
                    Length=TISM_UartMXBuildPacket(MessageToProcess, &Packet[0], TxSequenceNumber);
                    if(TISM_UartMXSendPacket(&Packet[0], Length))
                    {
                      // Succesfully sent; register the packet in the retry buffer (if ACK/NAK expected), but not for outbound ACK/NAK packets and broadcasts!
                      if(MessageToProcess->MessageType!=TISM_ACK && MessageToProcess->MessageType!=TISM_NAK && MessageToProcess->RecipientHostID!=UARTMX_BROADCAST)
                        TISM_UartMXAddToRetryBuffer(ThisTask, &Packet[0], Length, TxSequenceNumber, MessageToProcess->RecipientHostID, MessageToProcess->RecipientTaskID);
                      else
                        if(MessageToProcess->RecipientHostID==UARTMX_BROADCAST)
                        {
                          // If the outbound packet is a broadcast, we repeat it a few times without increasing the sequence number.
                          // This ensures recipients receive the broadcast; duplicates are filtered out by the recipients.
                          for(uint8_t TXcounter=0; TXcounter<(UARTMX_TX_RETRIES-1); TXcounter++)
                          {
                            sleep_us(UARTMX_TX_BROADC_DELAY_US);       // We wait here; no other packets can be transmitted in between retransmissions!
                            TISM_UartMXSendPacket(&Packet[0], Length);
                          }
                        }

                      // Dump full packet (when debugging)
                      if(ThisTask.TaskDebug) 
                      {
                        char Buffer[UARTMX_MAX_PACKET_SIZE*3+1];
                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Outbound raw data (hex):");
                        Buffer[0]='\0';                    
                        for(uint8_t Counter=0; Counter<Length; Counter++) 
                          snprintf(Buffer+(Counter*3), 4, " %02X", Packet[Counter]);
                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "%s - SequenceNo. %02X (hex)", Buffer, TxSequenceNumber);
                      }
                    }
                    TxSequenceNumber++;                        // Increment sequence number for next packet
                  }
                  else
                  {
                    // Did we receive a packet for another TaskID? If it is routed here, it's a stray packet.
                    if(MessageToProcess->RecipientTaskID!=ThisTask.TaskID)
                    {
                      // This shouldn't happen!
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Stray message received; '%ld' type %s (0x%02X) from TaskID %d (HostID %d) received, addressed to TaskID %d (HostID %d).", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->RecipientTaskID, MessageToProcess->RecipientHostID);
                    }
                    else
                    {
                      // Process the messages intended for this task.  
                      switch(MessageToProcess->MessageType)
                      {
                        case TISM_TEST:           // Test packet, no action to take. Just enter a log entry.
                                                  TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_TEST message received from TaskID %d (HostID %d). No action taken.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                  break;                      
                        case TISM_PING          : // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                                  TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                                  break;
                        case TISM_MX_SUBSCRIBE  : // Subscribe a task to an incoming message with the specified message type.
                                                  // Allow only subscription for user defined message types; >=UARTMX_USER_MSG_TYPE_START
                                                  // Is there already a subscription to this message type? Then give a warning.
                                                  if((uint8_t)MessageToProcess->Payload0<UARTMX_USER_MSG_TYPE_START)
                                                  {
                                                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "TaskID %d cannot subscribe to reserved Message Type %s (%d).", MessageToProcess->SenderTaskID, TISM_MessageTypeToString((uint8_t)MessageToProcess->Payload0), (uint8_t)MessageToProcess->Payload0);
                                                    break;
                                                  }

                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Subscribing Task ID %d to Message Type %d.", MessageToProcess->SenderTaskID, (uint8_t)MessageToProcess->Payload0);
                                                  
                                                  if(MessageTypeToTaskMapping[(uint8_t)MessageToProcess->Payload0]!=TISM_NetworkManagerTaskID)
                                                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning: overwriting subscription for Message Type %d (was Task ID %d, changing to %d).",(uint8_t)MessageToProcess->Payload0,MessageTypeToTaskMapping[(uint8_t)MessageToProcess->Payload0],MessageToProcess->SenderTaskID);
                                                  MessageTypeToTaskMapping[(uint8_t)MessageToProcess->Payload0]=MessageToProcess->SenderTaskID;
                                                  break;
                        case TISM_MX_UNSUBSCRIBE: // Unsubscribe the task from a specific message type.
                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Unsubscribing Task ID %d from Message Type %d.", MessageToProcess->SenderTaskID, (uint8_t)MessageToProcess->Payload0);

                                                  MessageTypeToTaskMapping[(uint8_t)MessageToProcess->Payload0]=TISM_NetworkManagerTaskID;   // TISM_NetworkManager is the default handler.
                                                  break;
                        default                 : // Unknown message type - ignore.
                                                  break;
                      }
                    }
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Everything processed? If there are packets in the retry buffer, set the next wakeup time. Else go to sleep.
                // Any packets incoming packets on the UART or messages pending?
                if(TISM_UartMXPacketsWaiting()==0 && TISM_PostmanTaskMessagesWaiting(ThisTask)==0)
                {
                  if(TISM_UartMXProcessRetries(ThisTask)>0)
                  {
                    // Retry-buffer active; calculate the earliest wakeup time.
                    uint64_t EarliestWakeup=UINT64_MAX;
                    for(uint8_t Counter=0; Counter<UARTMX_RETRY_BUFFER; Counter++)
                    {
                      if(TxRetryBuffer[Counter].Active)
                      {
                        uint64_t Deadline=TxRetryBuffer[Counter].SendTime+UARTMX_TX_TIMEOUT_US;
                        if(Deadline<EarliestWakeup) 
                          EarliestWakeup=Deadline;
                      }
                    }
                    TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_WAKEUPTIME, EarliestWakeup);
                  }
                  TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_SLEEP, true);
                }
				        break;
	  case STOP:  // Task required to stop this task.

		            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

#endif
