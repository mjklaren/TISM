/* 
  
  TISM.h
  ======
  The main header file for "The Incredible State Machine".
  This file contains all definitions, global data structs and user acccesible functions of the TISM system.
  
  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#ifndef TISM
#define TISM

#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/critical_section.h"



/*

  General definitions used in TISM_System.

*/
#define STDOUT                      stdout                   // Change these 3 file descriptors to redirect output to other destinations (e.g. files).
#define STDIN                       stdin
#define STDERR                      stderr
#define UNDEFINED                   -1
#define MAX_CORES                   2                        // Number of cores; the RP2040 and RP2350 have 2.
#define MAX_TASK_NAME_LENGTH        8                        // Maximum length of the name of a task (excl. terminating null).
#define CORE0                       0
#define CORE1                       1                       
#define HOSTNAME_PREFIX             "PICO"                   // Prefix used for setting the hostname; the HostID is added.
#define HOSTID                      7                        // Host ID - THIS NEEDS TO BE UNIQUE in the network. 0 (local host) and 255 (broadcast) reserved.
#define HOST_ID_LOCAL               0x00
#define NETWORKID                   0xF                      // Network ID (0x0-0xF) - needs to be the same for all hosts in the same network. 0xF is default.
#define SYSTEM_READY_PORT           22                       // Use this GPIO to indicate that system is ready and running. 'High' means system is ready. Can be used for onboard LED (GPIO 25).
                                                             // Uncomment to disable.
// System and task states
#define DOWN                        0       
#define STOP                        1
#define RUN                         2
#define INIT                        3
#define REBOOT                      4                        // TODO - to be implemented - watchdog_reboot(0, SRAM_END, 10);

// Debugging options - use with caution!
#define EVENTLOGGER_MAX_MESSAGES    250                      // Length of the message queue for the EventLogger. Extensive logging requires sufficient queue size of the Eventlogger.
#define DEBUG_HIGH                  2                        // Debug levels
#define DEBUG_LOW                   1
#define DEBUG_NONE                  0
//#define RUN_STEP_BY_STEP                                     // When enabled, run tasks in a slow pace (step by step), with logging sent to STDOUT.
#define RUN_STEP_BY_STEP_DELAY      200                      // Wait period used in step-by-step mode, before and after each task (in msec).
#define SYSTEM_DEBUG_LEVEL          0                        // System wide debug level (see above for values). Task debug levels set in main.c.
//#define EXTREME_DEBUGGING                                    // When enabled, debug levels for all tasks set to DEBUG_HIGH. BE CAREFUL!

// GPIO numbers of the Raspberry Pi Pico, mostly used by TISM_IRQHandler.c, to subscribe to GPIO events, but useful throughtout the TISM-system
#define NUMBER_OF_GPIO_PORTS        29                       // Number of GPIOs on the GP2040.
#define GPIO_0                      0       
#define GPIO_1                      1
#define GPIO_2                      2
#define GPIO_3                      3
#define GPIO_4                      4
#define GPIO_5                      5
#define GPIO_6                      6
#define GPIO_7                      7
#define GPIO_8                      8
#define GPIO_9                      9
#define GPIO_10                     10
#define GPIO_11                     11
#define GPIO_12                     12
#define GPIO_13                     13
#define GPIO_14                     14
#define GPIO_15                     15
#define GPIO_16                     16
#define GPIO_17                     17
#define GPIO_18                     18
#define GPIO_19                     19
#define GPIO_20                     20
#define GPIO_21                     21
#define GPIO_22                     22
#define GPIO_23                     23
#define GPIO_24                     24
#define GPIO_25                     25
#define GPIO_26                     26
#define GPIO_27                     27
#define GPIO_28                     28
#define IRQ_UNSUBSCRIBE             0                        // Unsubscribe from IRQ events for the specified GPIO
#define IRQHANDLER_MAX_MESSAGES     250

// Behaviors of the TISM-scheduler
#define MAX_TASKS                   30                       // Maximum number of TISM tasks. Maximum value = 250.
#define QUEUE_RUN_ASCENDING         1                        // Run the process queue from 0 upwards.
#define QUEUE_RUN_DESCENDING        -1                       // Run the process queue from <NumberOfTasks> downwards.
#define STARTUP_DELAY               5000                     // Milliseconds - Delay, wait for things to settle down before we start.
#define PRIORITY_HIGH               2500                     // Microseconds - High priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_NORMAL             5000                     // Microseconds - Normal priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_LOW                10000                    // Microseconds - Low priority task; time after which task should be restarted. Lower = higher prio.

// Components of TISM that can be disabled. Once disabled, they won't be compiled into the executable.
//#define TISM_DISABLE_PRIORITIES                          // Disables priorities mechanism; all tasks are executed round robin.
//#define TISM_DISABLE_SCHEDULER                           // Disables the scheduler; all tasks start consecutively, no planning. Also disables the TISM_SoftwareTimer.
//#define TISM_DISABLE_DUALCORE                            // Disables dual processor core operation; only use the first core.
//#define TISM_DISABLE_UARTMX                              // Disables Message eXchange via the UART.
//#define TISM_DISABLE_PROTECTIONS                         // Disables protection of TISM-tasks (changes to task attributes of "T_* -tasks").


// Definitions used for TISM event logging.
#define EVENT_LOG_ENTRY_LENGTH      150                      // Maximum text length of a single entry in the log, max. 65535. Determines the number of entries in the circular buffer.


// Definitions for TISM_Watchdog.c
#define TISM_DISABLE_WATCHDOG                                // Disable/enable the watchdog. Can be helpful for debugging.
#define WATCHDOG_CHECK_INTERVAL     30000000                 // Microseconds - Interval between 'are you alive' checks
#define WATCHDOG_TASK_TIMEOUT       5000000                  // Microseconds - Timeout period before we expect a task replies to a PING message.
#define WATCHDOG_MAX_COUNTER        50000                    // Max. size of the counter of outbound messages - if reached, reset counter to 0.


// Definitions used for TISM_messaging and message exchange via the UART. Some taken from UartMX protocol specification. 
#define MAX_MESSAGES                25                       // Length of the message queues (circular buffers) for all tasks. Max value is 255 (8 bits). 
#define OUTBOUND_MAX_MESSAGES       100                      // Length of the message queues (circular buffers) for the outbound queues for each core. Max value is 255 (8 bits). 
#define UARTMX_TXGPIO               GPIO_1                   // GPIO for transmission of messages.
#define UARTMX_RXGPIO               GPIO_0                   // GPIO for reception of messages.
#define UARTMX_UARTID               uart0                    // Corresponding UART.
#define UARTMX_VERSION              0x00                     // Version ID - version of the UARTMX protocol. 0x00=first version.
#define UARTMX_START_MARKER         0xD3                     // Start of a TISM message packet
#define UARTMX_END_MARKER           0x55                     // End of a TISM message packet
#define UARTMX_CRC_POLINOMIAL       0x1021
#define UARTMX_CRC_INIT_VALUE       0xFFFF
#define UARTMX_BROADCAST            0xFF                     // Broadcast HostID for UARTMX messages. 
#define UARTMX_MIN_PACKET_SIZE      12                       // Smallest packet size (in bytes)
#define UARTMX_ACKNAK_PACKET_SIZE   13                       // Fixed size of ACK/NAK packets (in bytes)
#define UARTMX_MAX_PAYLOAD_SIZE     16                       // Max. supported payload, including timestamp (4+4+8 bytes)
#define UARTMX_PINGECHO_PACKET_SIZE 16                       // Fixed size of Ping and Echo packets (in bytes)
#define UARTMX_HOSTDISC_PACKET_SIZE 20                       // Fixed size of packets used for TISM host discovery (in bytes)
#define UARTMX_MAX_PACKET_SIZE      28                       // Maximum packet size (in bytes)
#define UARTMX_BUFFER               UARTMX_MAX_PACKET_SIZE*4 // Size of the circular buffer for incoming UART data (in bytes)
#define UARTMX_RETRY_BUFFER         10                       // Number of slots in the transmit retry buffer (sliding window size).
#define UARTMX_MAX_INCOMPLETE_BYTES 32                       // Max bytes to keep without finding valid packet
#define UARTMX_USER_MSG_TYPE_START  0x64                     // First message type available for user defined messages.
#define UARTMX_TX_RETRIES           3                        // Number of retries for unacknowledged messages; also number of outbound broadcast transmissions.
#define UARTMX_BAUDRATE             19200                    // Baudrate of UART; 8 databits, 1 stopbits, no parity is assumed.
#define UARTMX_RX_TIMEOUT_US        43550                    // Microseconds - time after which an incomplete RX packet is discarded; example values below.
#define UARTMX_TX_TIMEOUT_US        58500                    // Microseconds - time after which unacknowledged messages are retried; example values below.
#define UARTMX_TX_BROADC_DELAY_US   87100                    // Microseconds - time between retransmissions of outbound broadcast messages; example values below.
                                                             /*  UARTMX_		UARTMX_RX_	UARTMX_TX_	  UARTMX_TX_
                                                                 BAUDRATE   TIMEOUT_US  TIMEOUT_US  BROADC_DELAY_US
                                                                 ========   ==========  ==========  ===============
                                                                   9600	      87100	      91000	        143000
                                                                  19200	      43550	      58500	         87100
                                                                  38400	      22100	      45500	         44200
                                                                  57600	      14950	      39000	         29900
                                                                 115200	       7800	      32500	         15600       */


/*
  
  Error messages; these are between 0x00 (0) and 0x31 (49)

*/
// TISM system error messages
#define OK                          0x00                     // (0) Everything OK
#define ERR_TOO_MANY_TASKS          0x01                     // (1) Attempt to register too many tasks.
#define ERR_INITIALIZING            0x02                     // (2) Error occured during initialization.
#define ERR_MAILBOX_FULL            0x03                     // (3) Mailbox full, delivery failed.
#define ERR_RECIPIENT_INVALID       0x04                     // (4) TaskID of the recipient is invalid
#define ERR_TASK_NOT_FOUND          0x05                     // (5) Task for specified TaskID not found
#define ERR_TASK_SLEEPING           0x06                     // (6) Task to assign something to is not running (sleeping)
#define ERR_RUNNING_TASK            0x07                     // (7) Error occured when executing a task, or task returned an error
#define ERR_INVALID_OPERATION       0x08                     // (8) Invalid operation requested

// UartmMX error messages
#define UARTMX_RX_ERR_START         0x09                     // (9) Start marker expected, not received
#define UARTMX_RX_ERR_END           0x0A                     // (10) End marker expected, not received  
#define UARTMX_RX_ERR_LENGTH        0x0B                     // (11) Length of message invalid  
#define UARTMX_RX_ERR_VERSION       0x0C                     // (12) Version ID invalid 
#define UARTMX_RX_ERR_NETWORKID     0x0D                     // (13) Network ID mismatch 
#define UARTMX_RX_ERR_CRC           0x0E                     // (14) CRC check failed
#define UARTMX_RX_ERR_SENDERHOSTID  0x0F                     // (15) Sender HostID invalid (0x00 or our own HostID)
#define UARTMX_RX_ERR_MSGSIZE       0x10                     // (16) Message size invalid
#define UARTMX_RX_TISMMSG_ERR       0x11                     // (17) TISM message validation failed (e.g. message type versus size invalid )


/*
  
  TISM Message types, used in the messaging system. TISM system message type values range from 0x32 (50) to 0x63 (99).
  User generated messages can have message types ranging from 0x64 (100) to 0xFE (254). 0xFF (255) is reserved.

*/
// Standard message types used in the TISM messaging system; 
#define TISM_TEST	                  0x32	  	               // (50) Dummy message, does nothing.
#define TISM_ACK	                  0x33	                   // (51) Positive acknowledgement of message receipt
#define TISM_NAK	                  0x34	                   //	(52) Negative acknowledgement of message receipt
#define TISM_PING	                  0x35	                   // (53) Check to see if recipient host or task is still functional.
#define TISM_ECHO	                  0x36	                   // (54) Response to ping-request.
#define TISM_DISCOVER	              0x37	                   // (55) Broadcast; discovery of available hosts, hostnames and running tasks.
#define TISM_INTRODUCE	            0x38	                   // (56) Host introducing itself to the network, or response to discovery-request
#define TISM_RESOLVE_TASKNAME	      0x39	                   // (57) Request for TaskID belonging to specified task name
#define TISM_RESOLVE_TASKNAME_REPLY	0x3A	                   // (58) Reply; requested TaskID
#define TISM_RESOLVE_TASKID	        0x3B	                   // (59) Request for TaskName belonging to specified task ID
#define TISM_RESOLVE_TASKID_REPLY	  0x3C	                   // (60) Reply; requested TaskName

// Message types used for eventlogging
#define TISM_LOG_EVENT_NOTIFY       0x3D                     // (61) Log entry of type 'notification'
#define TISM_LOG_EVENT_ERROR        0x3E                     // (62) Log entry of type 'error'

// Message types for altering the state of the system or specific tasks
#define TISM_SET_SYS_STATE          0x3F                     // (63) Change the state of the whole system (aka runlevel).
#define TISM_SET_TASK_STATE         0x40                     // (64) Change the state of a task.
#define TISM_SET_TASK_PRIORITY      0x41                     // (65) Set the priority of a specific task to PRIORITY
#define TISM_SET_TASK_SLEEP         0x42                     // (66) Set the sleep state of a specific state ('true' means sleeping)
#define TISM_SET_TASK_WAKEUPTIME    0x43                     // (67) Set the timestamp of the next wake up (in usec)
#define TISM_SET_TASK_DEBUG         0x44                     // (68) Set the debug level of a specific task   
#define TISM_WAKE_ALL_TASKS         0x45                     // (69) Wake all tasks.
#define TISM_DEDICATE_TO_TASK       0x46                     // (70) Dedicate the whole system to a specific task - use with caution.

// Message types used for Message eXchange between different hosts; tasks that can 'subscribe' to incoming message types.
#define TISM_MX_SUBSCRIBE           0x47                     // (71) TISM_MX_SUBSCRIBE 	0x48	Subscribe a task to an incoming message with the specified message type.
#define TISM_MX_UNSUBSCRIBE         0x48                     // (72) Unsubscribe the task from a specific message type.

// Message types used for the software timer
#define TISM_CANCEL_TIMER           0x49                     // Cancel the software timer
#define TISM_CANCEL_TIMER_BY_NR     0x4A                     // Cancel the software timer, specified by the unique ID
#define TISM_SET_TIMER              0x4B                     // Set a software timer
#define TISM_DISPLAY_TIMERS         0x4C                     // Display active timers in the event log


/*

  Data structures used in the TISM system.

*/

// Structure of a circular buffer.
typedef struct TISM_CircularBuffer
{
  uint16_t Head, Tail, Size;  // Size is number of element slots
  uint16_t ElementSize;       // Size of each element in bytes
  void *Data;                 // Pointer to buffer data
} TISM_CircularBuffer;


// Structure containing all TISM tasks data - the tasks running within the system.
typedef struct TISM_Task
{  
  uint8_t TaskID, RunningOnCoreID, TaskState, TaskDebug, (*TaskFunction) (struct TISM_Task);
  bool TaskSleeping;
  char TaskName[MAX_TASK_NAME_LENGTH+1];
  struct TISM_CircularBuffer *InboundMessageQueue;                                // Inbound queue for each task. 
  struct TISM_CircularBuffer *OutboundMessageQueue;                               // Pointer to outbound queue - depending on the core the task is running on.
  uint32_t TaskPriority;

#ifndef TISM_DISABLE_SCHEDULER
  uint64_t TaskWakeUpTimer;
#endif
} TISM_Task;


// Structure of the TISM-system - the system itself. This is a global variable.
typedef struct TISM_System
{
  // Generic system values.
  uint8_t HostID, NetworkID, State, NumberOfTasks, RunPointer[MAX_CORES], RunPointerDirection[MAX_CORES];
  uint64_t UniqueID;
  char Hostname[MAX_TASK_NAME_LENGTH+1];
  bool RebootAfterShutdown;

#ifndef TISM_DISABLE_DUALCORE
  mutex_t RunningTaskMutex;
  uint8_t CorePointer[MAX_CORES];
#endif

  // The actual tasks
  TISM_Task Task[MAX_TASKS];

  // Task IDs for TISM system tasks; used for sending messages to system tasks.
  uint8_t TISM_PostmanTaskID, TISM_IRQHandlerTaskID, TISM_WatchdogTaskID, TISM_EventLoggerTaskID;

#ifndef TISM_DISABLE_SCHEDULER  
  uint8_t TISM_SoftwareTimerTaskID;
#endif

#ifndef TISM_DISABLE_UARTMX
  uint8_t TISM_UartMXTaskID, TISM_NetworkManagerTaskID;
#endif

  // Pointers to circular buffers for IRQ handling and outbound messages (1 per code).
  TISM_CircularBuffer *IRQHandlerInboundQueue, *OutboundMessageQueue[MAX_CORES];

  // Debug related variables.
  uint8_t SystemDebug;
} TISM_System;
extern TISM_System System;


// Structure for a TISM messaging system using circular buffers (aka ringbuffer).
typedef struct TISM_Message
{
  uint8_t SenderHostID, SenderTaskID, RecipientHostID, RecipientTaskID, MessageType;
  uint32_t Payload0, Payload1;     
  uint64_t MessageTimestamp;                                                
} TISM_Message;


// Structure for registering remote tasks on remote hosts
typedef struct RemoteTask
{
    uint8_t TaskID;
    char TaskName[MAX_TASK_NAME_LENGTH+1];
    struct RemoteTask *NextTask;
} RemoteTask;


// Structure for registering remote hosts (our 'network neighborhood')
typedef struct RemoteHost
{
  uint8_t HostID, NumberOfTasks, MinutesSinceLastSeen;
  char Hostname[MAX_TASK_NAME_LENGTH+1];
  bool ReachedOut;
  struct RemoteTask *NextTask;
  struct RemoteHost *NextHost;  
} RemoteHost;


/*

  Function prototypes for the TISM system.

*/

// TISM.c - "The Incredible State Machine" - functions to set up the system and some generic tools.
const char* TISM_MessageTypeToString(uint8_t MessageType);
void TISM_StringToPayloads(char *String, uint32_t *Payload0, uint32_t *Payload1);
char* TISM_PayloadsToString(uint32_t Payload0, uint32_t Payload1, char *Buffer);
uint8_t TISM_GetTaskID(char *TaskName);
bool TISM_IsValidTaskID(uint8_t TaskID);
bool TISM_IsTaskAwake(uint8_t TaskID);
bool TISM_IsSystemTask(uint8_t TaskID);
uint8_t TISM_RegisterTask(uint8_t (*Function)(TISM_Task), char *Name, uint32_t TaskPriority);
uint8_t TISM_InitializeSystem();

// IRQHandler.c - Routines to process external interrupts (IRQs). Other functions can 'subscribe' to these events, after which IRQHandler will
//                send messages when events occur. This task uses the global circular buffer IRQHandlerInboundQueue defined in TISM_Definitions.h
bool TISM_IRQHandlerSubscribe(TISM_Task ThisTask, uint8_t GPIO, uint32_t Events, bool GPIOPullDown, uint32_t AntiBounceTimeout);
uint8_t TISM_IRQHandler(TISM_Task ThisTask);

// TISM_Postman.c - Tools for managing the postboxes (outbound and inbound queues) and delivery of messages between tasks, using thread safe circular buffers.
uint16_t TISM_PostmanMessagesWaiting(TISM_CircularBuffer *Buffer);
uint16_t TISM_PostmanSlotsAvailable(TISM_CircularBuffer *Buffer);
bool TISM_PostmanWriteMessage(TISM_CircularBuffer *Buffer, uint8_t SenderHostID, uint8_t SenderTaskID, uint8_t RecipientHostID, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Payload0, uint32_t Payload1, int64_t MessageTimestamp);
TISM_Message *TISM_PostmanReadMessage(TISM_CircularBuffer *Buffer);
void TISM_PostmanDeleteMessage(TISM_CircularBuffer *Buffer);
TISM_CircularBuffer *TISM_PostmanBufferInit(uint16_t Size, uint16_t ElementSize);
bool TISM_PostmanBufferResize(TISM_CircularBuffer *Buffer, size_t NewSize, size_t NewElementSize);
void TISM_PostmanClearBuffer(TISM_CircularBuffer *Buffer);
void TISM_PostmanBufferDeinit(TISM_CircularBuffer *Buffer);
uint16_t TISM_PostmanTaskMessagesWaiting(TISM_Task ThisTask);
TISM_Message *TISM_PostmanTaskReadMessage(TISM_Task ThisTask);
void TISM_PostmanTaskDeleteMessage(TISM_Task ThisTask);
bool TISM_PostmanTaskWriteMessage(TISM_Task ThisTask, uint8_t RecipientHostID, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Payload0, uint32_t Payload1);
uint8_t TISM_Postman (TISM_Task ThisTask);

//   TISM_Scheduler.c - The scheduler of the TISM-system (non-preemptive/cooperative multitasking), including functions to manipulate task properties and system states.
bool TISM_SchedulerIsTaskSleeping(uint8_t TaskID);
uint8_t TISM_SchedulerSetTaskAttribute(TISM_Task ThisTask, uint8_t TargetTaskID, uint8_t AttributeToChange, uint64_t Setting);
uint8_t TISM_SchedulerSetMyTaskAttribute(TISM_Task ThisTask, uint8_t AttributeToChange, uint64_t Setting);
void TISM_SchedulerSetSystemState(TISM_Task ThisTask, uint8_t SystemState);
uint8_t TISM_Scheduler(uint8_t ThisCoreID);

// TISM_SoftwareTimer.c - Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.
#ifndef TISM_DISABLE_SCHEDULER
uint64_t TISM_SoftwareTimerSetVirtual(uint64_t TimerUsec);
bool TISM_SoftwareTimerVirtualExpired(uint64_t TimerUsec);
uint32_t TISM_SoftwareTimerSet(TISM_Task ThisTask, uint8_t TimerID, bool RepetitiveTimer, uint32_t TimerIntervalMsec);
bool TISM_SoftwareTimerCancel(TISM_Task ThisTask, uint8_t TimerID);
bool TISM_SoftwareTimerCancelbySequenceNr(TISM_Task ThisTask, uint32_t SequenceNr);
uint8_t TISM_SoftwareTimer(TISM_Task ThisTask);
#endif

// TISM_UartMX - Task for processing of incoming messages via the Uart.
#ifndef TISM_DISABLE_UARTMX
uint8_t TISM_UartMXPacketsWaiting();
bool TISM_UartMXSubscribe(TISM_Task ThisTask, uint8_t MessageType);
bool TISM_UartMXUnsubscribe(TISM_Task ThisTask, uint8_t MessageType);
uint8_t TISM_UartMX (TISM_Task ThisTask);

// NetworkManager - Task to manage multiple hosts in a network using UartMX protocol.
uint8_t TISM_NetworkManager (TISM_Task ThisTask);
#endif

// TISM_Watchdoc.c - Task to check if other tasks are still alive. Generate warnings to the EventLogger in case of timeouts.	
uint8_t TISM_Watchdog(TISM_Task ThisTask);

// TISM_EventLogger.c - A uniform and thread-safe method for handling of log entries.
bool TISM_EventLoggerLogEvent (struct TISM_Task ThisTask, uint8_t LogEntryType, const char *format, ...);
uint8_t TISM_EventLogger (TISM_Task ThisTask);

// TISM_NetworkManager.c - Task responsible for managing the network 'neighborhood' in the TISM-system.
uint8_t TISM_NetworkManagerNumberOfRemoteHosts(void);
bool TISM_NetworkManagerResolveHostID(TISM_Task ThisTask, uint8_t HostID, RemoteHost *Host, bool ContactHost);
uint8_t TISM_NetworkManagerResolveHostname(TISM_Task ThisTask, const char *Hostname, bool ContactHost);
bool TISM_NetworkManagerResolveTaskID(TISM_Task ThisTask, uint8_t HostID, uint8_t TaskID, char *TaskName, bool ContactHost);
uint8_t TISM_NetworkManagerResolveTaskname(TISM_Task ThisTask, uint8_t HostID, char *TaskName, bool ContactHost);
bool TISM_NetworkManagerDiscoverHost(TISM_Task ThisTask, uint8_t SequenceNumber, RemoteHost *Host);

// TISM_Console.c - A simple console task, that can be used for debugging and interaction with the system.
uint8_t TISM_Console (TISM_Task ThisTask);


#endif