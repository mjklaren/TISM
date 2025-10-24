/* 
  
  TISM.h
  ======
  The main header file for "The Incredible State Machine".
  This file contains all definitions, global data structs and user acccesible functions of the TISM system.
  
  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#ifndef TISM
#define TISM

#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"


/*

  All definitions used in the TISM-system.

*/

// General definitions used in TISM_System
#define STDOUT                   stdout                  // Change these 3 file descriptors to redirect output to other destinations (e.g. files).
#define STDIN                    stdin
#define STDERR                   stderr
#define UNDEFINED                -1
#define MAX_CORES                2                       // Number of cores; the RP2040 and RP2350 have 2.
#define MAX_TASK_NAME_LENGTH     30                      // Maximum length of the name of a task.
#define CORE0                    0
#define CORE1                    1                       
#define SYSTEM_READY_PORT        22                      // Use this GPIO to indicate that system is ready and running. 'High' means system is ready. Can be used for onboard LED (GPIO 25).
                                                         // Uncomment to disable.

// Behaviors of the TISM-scheduler
#define MAX_TASKS                30                      // Maximum number of TISM tasks. Maximum value = 250.
#define QUEUE_RUN_ASCENDING      1                       // Run the process queue from 0 upwards.
#define QUEUE_RUN_DESCENDING     -1                      // Run the process queue from <NumberOfTasks> downwards.
#define STARTUP_DELAY            5000                    // Milliseconds - Delay, wait for things to settle down before we start.
#define PRIORITY_HIGH            2500                    // Microseconds - High priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_NORMAL          5000                    // Microseconds - Normal priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_LOW             10000                   // Microseconds - Low priority task; time after which task should be restarted. Lower = higher prio.

// Components of TISM that can be disabled. Once disabled, they won't be compiled into the executable.
//#define TISM_DISABLE_PRIORITIES                          // Disable priorities mechanism; all tasks are executed round robin.
//#define TISM_DISABLE_SCHEDULER                           // Disables the scheduler; all tasks start consecutively, no planning. Also disables the TISM_SoftwareTimer.
//#define TISM_DISABLE_DUALCORE                            // Disables dual processor core operation; only use the first core.

// System and task states
#define DOWN                     0       
#define STOP                     1
#define RUN                      2
#define INIT                     3
#define REBOOT                   4                       // TODO - to be implemented - watchdog_reboot(0, SRAM_END, 10);

// Definitions for the software timer
#define TISM_CANCEL_TIMER        0
#define TISM_CANCEL_TIMER_BY_NR  1
#define TISM_SET_TIMER           2

// Definitions used for TISM event logging.
#define EVENT_LOG_ENTRY_LENGTH   150                     // Maximum text length of a single entry in the log, max. 65535. Determines the number of entries in the circular buffer.

// Error messages; these are between 0 and 49
#define OK                       0
#define ERR_TOO_MANY_TASKS       1                       // Attempt to register too many tasks.
#define ERR_INITIALIZING         2                       // Error occured during initialization.
#define ERR_MAILBOX_FULL         3                       // Mailbox full, delivery failed.
#define ERR_RECIPIENT_INVALID    4                       // TaskID of the recipient is invalid
#define ERR_TASK_NOT_FOUND       5                       // Task for specified TaskID not found
#define ERR_TASK_SLEEPING        6                       // Task to assign something to is not running (sleeping)
#define ERR_RUNNING_TASK         7                       // Error occured when executing a task, or task returned an error
#define ERR_INVALID_OPERATION    8                       // Invalid operation requested

// Definitions used for TISM_messaging. Currently a maximum number of 255 message types are allowed.
#define MAX_MESSAGES             25                      // Length of the message queues (circular buffers; inbound and outbound) for ALL tasks. Max value is 255 (8 bits). 
                                                         // When a lot of messages are generated (e.g. SYSTEM_DEBUG_LEVEL=2) this should be around 100.
#define HOSTID                   2                       // Host ID - for future use.
#define ALL_HOSTS                255                     // Used for broadcasting messages to all hosts.
#define TASKID_UNSPECIFIED       255                     // Used when a message is sent but the target TaskID is unknown.

// Standard message types used in the TISM messaging system; TISM system message type values are between 50 and 99.
#define TISM_TEST                50                      // Dummy message.
#define TISM_PING                51                      // Check to see if recipient task is still functional.
#define TISM_ECHO                52                      // Response to ping-request.
#define TISM_LOG_EVENT_NOTIFY    53                      // Log entry of type 'notification'
#define TISM_LOG_EVENT_ERROR     54                      // Log entry of type 'error'

// Message types for altering the state of the system or specific tasks
#define TISM_SET_SYS_STATE       55                      // Change the state of the whole system (aka runlevel).
#define TISM_SET_TASK_STATE      56                      // Change the state of a task.
#define TISM_SET_TASK_PRIORITY   57                      // Set the priority of a specific task to PRIORITY
#define TISM_SET_TASK_SLEEP      58                      // Set the sleep state of a specific state ('true' or 'false')
#define TISM_SET_TASK_WAKEUPTIME 59                      // Set the timestamp of the next wake up (in usec)
#define TISM_SET_TASK_DEBUG      60                      // Set the debug level of a specific task   
#define TISM_WAKE_ALL_TASKS      61                      // Wake all tasks.
#define TISM_DEDICATE_TO_TASK    62                      // Dedicate the whole system to a specific task - use with caution.

// Messages used for Message eXchange between different hosts
#define TISM_MX_SUBSCRIBE        63                      // Subscribe a task to an incoming message with the specified message type
#define TISM_MX_UNSUBSCRIBE      64                      // Unsubscribe the task from a specific message type

// GPIO numbers of the Raspberry Pi Pico, mostly used by TISM_IRQHandler.c
#define NUMBER_OF_GPIO_PORTS     29                      // Number of GPIOs on the GP2040.
#define GPIO_0                   0       
#define GPIO_1                   1
#define GPIO_2                   2
#define GPIO_3                   3
#define GPIO_4                   4
#define GPIO_5                   5
#define GPIO_6                   6
#define GPIO_7                   7
#define GPIO_8                   8
#define GPIO_9                   9
#define GPIO_10                  10
#define GPIO_11                  11
#define GPIO_12                  12
#define GPIO_13                  13
#define GPIO_14                  14
#define GPIO_15                  15
#define GPIO_16                  16
#define GPIO_17                  17
#define GPIO_18                  18
#define GPIO_19                  19
#define GPIO_20                  20
#define GPIO_21                  21
#define GPIO_22                  22
#define GPIO_23                  23
#define GPIO_24                  24
#define GPIO_25                  25
#define GPIO_26                  26
#define GPIO_27                  27
#define GPIO_28                  28
#define IRQ_UNSUBSCRIBE          0                       // Unsubscribe from IRQ events for the specified GPIO
#define IRQHANDLER_MAX_MESSAGES  250

// Definitions for TISM_Watchdog.c
#define TISM_DISABLE_WATCHDOG                            // Disable/enable the watchdog. Can be used for debugging.
#define WATCHDOG_CHECK_INTERVAL 30000000                 // Microseconds - Interval between 'are you alive' checks
#define WATCHDOG_TASK_TIMEOUT   5000000                  // Microseconds - Timeout period before we expect a task replies to a PING message.
#define WATCHDOG_MAX_COUNTER    50000                    // Max. size of the counter of outbound messages - if reached, reset counter to 0.

// Debugging options - use with caution!
#define EVENTLOGGER_MAX_MESSAGES 250                     // Length of the message queue for the EventLogger. Extensive logging requires sufficient queue size of the Eventlogger.
#define DEBUG_HIGH               2                       // Debug levels
#define DEBUG_LOW                1
#define DEBUG_NONE               0
//#define RUN_STEP_BY_STEP                                 // When enabled, run tasks in a slow pace, with messages to STDOUT.
#define RUN_STEP_BY_STEP_DELAY   200                     // Wait period before and after each task (msec).
#define SYSTEM_DEBUG_LEVEL       0                       // System wide debug level (see above for values). Task debug levels set in main program.
//#define EXTREME_DEBUGGING                                // When enabled, debug levels for all tasks set to DEBUG_HIGH. BE CAREFUL!


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
  uint8_t HostID, State, NumberOfTasks, RunPointer[MAX_CORES], RunPointerDirection[MAX_CORES];

#ifndef TISM_DISABLE_DUALCORE
  mutex_t RunningTaskMutex;
  uint8_t CorePointer[MAX_CORES];
#endif

  // The actual tasks
  TISM_Task Task[MAX_TASKS];

  // Task IDs for TISM system tasks; used for sending messages to system tasks.
  uint8_t TISM_PostmanTaskID, TISM_IRQHandlerTaskID, TISM_TaskManagerTaskID, TISM_WatchdogTaskID, TISM_EventLoggerTaskID;

#ifndef TISM_DISABLE_SCHEDULER  
  uint8_t TISM_SoftwareTimerTaskID;
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
  uint32_t Message, Specification; 
  uint64_t MessageTimestamp;                                                
} TISM_Message;


/*

  Function prototypes for the TISM system.

*/

// TISM.c - "The Incredible State Machine" - functions to set up the system and some generic tools.
int TISM_GetTaskID(char *TaskName);
bool TISM_IsValidTaskID(int TaskID);
bool TISM_IsTaskAwake(int TaskID);
bool TISM_IsSystemTask(int TaskID);
int TISM_RegisterTask(uint8_t (*Function)(TISM_Task), char *Name, uint32_t TaskPriority);
int TISM_InitializeSystem();


// IRQHandler.c - Routines to process external interrupts (IRQs). Other functions can 'subscribe' to these events, after which IRQHandler will
//                send messages when events occur. This task uses the global circular buffer IRQHandlerInboundQueue defined in TISM_Definitions.h
bool TISM_IRQHandlerSubscribe(TISM_Task ThisTask, uint8_t GPIO, uint32_t Events, bool GPIOPullDown, uint32_t AntiBounceTimeout);
uint8_t TISM_IRQHandler(TISM_Task ThisTask);


// TISM_Postman.c - Tools for managing the postboxes (outbound and inbound queues) and delivery of messages between tasks, using thread safe circular buffers.
uint16_t TISM_PostmanMessagesWaiting(TISM_CircularBuffer *Buffer);
uint16_t TISM_PostmanSlotsAvailable(TISM_CircularBuffer *Buffer);
bool TISM_PostmanWriteMessage(TISM_CircularBuffer *Buffer,uint8_t SenderHostID,uint8_t SenderTaskID,uint8_t RecipientHostID,uint8_t RecipientTaskID,uint8_t MessageType,uint32_t Message,uint32_t Specification,uint64_t MessageTimestamp);
TISM_Message *TISM_PostmanReadMessage(TISM_CircularBuffer *Buffer);
void TISM_PostmanDeleteMessage(TISM_CircularBuffer *Buffer);
TISM_CircularBuffer *TISM_PostmanBufferInit(uint16_t Size,uint16_t ElementSize);
bool TISM_PostmanBufferResize(TISM_CircularBuffer *Buffer,uint16_t NewSize,uint16_t NewElementSize);
void TISM_PostmanClearBuffer(TISM_CircularBuffer *Buffer);
void TISM_PostmanBufferDeinit(TISM_CircularBuffer *Buffer);
uint16_t TISM_PostmanTaskMessagesWaiting(TISM_Task ThisTask);
TISM_Message *TISM_PostmanTaskReadMessage(TISM_Task ThisTask);
void TISM_PostmanTaskDeleteMessage(TISM_Task ThisTask);
bool TISM_PostmanTaskWriteMessage(TISM_Task ThisTask,uint8_t RecipientHostID,uint8_t RecipientTaskID,uint8_t MessageType,uint32_t Message,uint32_t Specification);
uint8_t TISM_Postman (TISM_Task ThisTask);


//   TISM_Scheduler.c - The scheduler of the TISM-system (non-preemptive/cooperative multitasking).
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


//   TISM_TaskManager.c - Library with functions to manipulate task properties and system states, when requested via messages.
uint8_t TISM_TaskManagerSetTaskAttribute(struct TISM_Task ThisTask, uint8_t TargetTaskID, uint8_t AttributeToChange, uint32_t Setting);
uint8_t TISM_TaskManagerSetMyTaskAttribute(struct TISM_Task ThisTask, uint8_t AttributeToChange, uint32_t Setting);
uint8_t TISM_TaskManagerSetSystemState(struct TISM_Task ThisTask, uint8_t SystemState);
uint8_t TISM_TaskManager(TISM_Task ThisTask);

// TISM_Watchdoc.c - Task to check if other tasks are still alive. Generate warnings to the EventLogger in case of timeouts.	
uint8_t TISM_Watchdog(TISM_Task ThisTask);


// TISM_EventLogger.c - A uniform and thread-safe method for handling of log entries.
bool TISM_EventLoggerLogEvent (struct TISM_Task ThisTask, uint8_t LogEntryType, const char *format, ...);
uint8_t TISM_EventLogger (TISM_Task ThisTask);


#endif