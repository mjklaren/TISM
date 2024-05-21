/* 
  
  TISM.h - The main header file for "The Incredible State Machine".
  This file contains all definitions, global data structs and user acccesible functions of the TISM system.
  
  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#ifndef TISM
#define TISM

#include <stdbool.h>

/*

  All definitions used in the TISM-system.

*/

// General definitions used in TISM_System
#define STDOUT                   stdout  // Change these 3 file descriptors to redirect output to other destinations (e.g. files).
#define STDIN                    stdin
#define STDERR                   stderr
#define UNDEFINED                -1
#define MAX_CORES                2       // Number of cores; currently the RP2040 has 2.
#define QUEUE_RUN_ASCENDING      1       // Run the process queue from 0 upwards.
#define QUEUE_RUN_DESCENDING     -1      // Run the process queue from <NumberOfTasks> downwards.
#define CORE0                    0
#define CORE1                    1                       
#define MAX_TASKS                30      // Maximum number of TISM tasks. Maximum value = 250.
#define MAX_TASK_NAME_LENGTH     30      // Maximum length of the name of a task
#define DEBUG_HIGH               2       // Debug levels
#define DEBUG_LOW                1
#define DEBUG_NONE               0
#define STARTUP_DELAY            5000    // Milliseconds - Delay, wait for things to settle down before we start.
#define PRIORITY_HIGH            50000   // Microseconds - High priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_NORMAL          100000  // Microseconds - Normal priority task; time after which task should be restarted. Lower = higher prio.
#define PRIORITY_LOW             500000  // Microseconds - Low priority task; time after which task should be restarted. Lower = higher prio.
#define SYSTEM_READY_PORT        25      // Use this GPIO to indicate that system is ready and running. 'High' means system is ready. Default value=25 (onboard LED).

// System and task states
#define DOWN                     0       
#define STOP                     1
#define RUN                      2
#define INIT                     3

// Definitions for the software timer
#define TISM_CANCEL_TIMER        0
#define TISM_SET_TIMER           1

// Error messages; these are between 0 and 49
#define OK                       0
#define ERR_TOO_MANY_TASKS       1       // Attempt to register too many tasks.
#define ERR_INITIALIZING         2       // Error occured during initialization.
#define ERR_MAILBOX_FULL         3       // Mailbox full, delivery failed.
#define ERR_RECIPIENT_INVALID    4       // TaskID of the recipient is invalid
#define ERR_TASK_NOT_FOUND       5       // Task for specified TaskID not found
#define ERR_TASK_SLEEPING        6       // Task to assign something to is not running (sleeping)
#define ERR_RUNNING_TASK         7       // Error occured when executing a task, or task returned an error
#define ERR_INVALID_OPERATION    8       // Invalid operation requested

// Definitions used for TISM_messaging. Currently a maximum number of 255 message types are allowed.
#define MAX_MESSAGES             50      // Length of the message queues (circular buffers). Max value is 65535 (16 bits).

// Standard message types used in the TISM messaging system; TISM system message type values are between 50 and 99.
#define TISM_TEST                50      // Dummy message.
#define TISM_PING                51      // Check to see if recipient task is still functional.
#define TISM_ECHO                52      // Response to ping-request.

// Message types for altering the state of the system or specific tasks
#define TISM_SET_SYS_STATE       53      // Change the state of the whole system (aka runlevel).
#define TISM_SET_TASK_STATE      54      // Change the state of a task.
#define TISM_SET_TASK_PRIORITY   55      // Set the priority of a specific task to PRIORITY
#define TISM_SET_TASK_SLEEP      56      // Set the sleep state of a specific state ('true' or 'false')
#define TISM_SET_TASK_WAKEUPTIME 57      // Set the timestamp of the next wake up (in usec)
#define TISM_SET_TASK_DEBUG      58      // Set the debug level of a specific task   
#define TISM_WAKE_ALL_TASKS      59      // Wake all tasks.
#define TISM_DEDICATE_TO_TASK    60      // Dedicate the whole system to a specific task - use with caution.

// GPIO numbers of the Raspberry Pi Pico, mostly used by TISM_IRQHandler.c
#define NUMBER_OF_GPIO_PORTS     29      // Number of GPIOs on the GP2040.
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
#define IRQ_UNSUBSCRIBE          0       // Unsubscribe from IRQ events for the specified GPIO

// Definitions for TISM_Watchdog.c
#define WATCHDOG_CHECK_INTERVAL 30000000 // Microseconds - Interval between 'are you alive' checks
#define WATCHDOG_TASK_TIMEOUT   5000000  // Microseconds - Timeout period before we expect a task replies to a PING message.
#define WATCHDOG_MAX_COUNTER    50000    // Max. size of the counter of outbound messages - if reached, reset counter to 0.


/*

  Data structures used in the TISM system.

*/

// Structure containing all TISM tasks data - the tasks running within the system.
typedef struct TISM_Task
{  
  uint8_t TaskID, RunningOnCoreID, TaskState, TaskDebug, (*TaskFunction) (struct TISM_Task);
  uint32_t TaskPriority;
  bool TaskSleeping;
  char TaskName[MAX_TASK_NAME_LENGTH+1];
  struct TISM_CircularBuffer *InboundMessageQueue;                                // Inbound queue for each task. 
  struct TISM_CircularBuffer *OutboundMessageQueue;                               // Pointer to outbound queue - depending on the core the task is running on.
  uint64_t TaskWakeUpTimer;
} TISM_Task;


// Structure of the TISM-system - the system itself. This is a global variable.
typedef struct TISM_System
{
  // Generic system values.
  uint8_t State, NumberOfTasks, RunPointer[MAX_CORES], RunPointerDirection[MAX_CORES];
  
  // The actual tasks
  TISM_Task Task[MAX_TASKS];

  // Task IDs for TISM system tasks; used for sending messages to system tasks.
  uint8_t TISM_PostmanTaskID, TISM_IRQHandlerTaskID, TISM_TaskManagerTaskID, TISM_WatchdogTaskID, TISM_SoftwareTimerTaskID;

  // Debug related variables.
  uint8_t SystemDebug;
} TISM_System;
TISM_System System;


// Structures for a TISM messaging system using circular buffers (aka ringbuffer).
typedef struct TISM_Message
{
  uint8_t SenderTaskID, RecipientTaskID, MessageType;
  uint32_t Message, Specification; 
  uint64_t MessageTimestamp;                                                 
} TISM_Message;


// Structure of a circular buffer. One for interrupt handling, one inbound queue per task, one outbound queue per core (=scheduler instance). These are global variables.
typedef struct TISM_CircularBuffer
{
  struct TISM_Message Message[MAX_MESSAGES];
  uint16_t Head, Tail;
} TISM_CircularBuffer;
TISM_CircularBuffer IRQHandlerInboundQueue, InboundMessageQueue[MAX_TASKS], OutboundMessageQueue[MAX_CORES];


/*

  TISM.c
  ======
  "The Incredible State Machine" - functions to set up the system and some generic tools.

*/

/*
  Description 
  Get the TaskID for the task specified by name from the global Task structs.

  Parameters:
  *TaskName    - Pointer to a string with the name of the task.

  Return value:
  -1           - Specified task name not found.
  <int value>  - Task ID for the specific task, starting with 0.
*/
int TISM_GetTaskID(char *TaskName);


/*
  Description
  Check if the specified TaskID is valid (=in use)

  Parameters:
  int TaskID   - The number of the task to check.

  Return value:
  false        - Invalid task ID
  true         - Valid task ID
*/
bool TISM_IsValidTaskID(int TaskID);


/*
  Description
  Check if the specified Task is awake.

  Parameters:
  int TaskID   - The number of the task to check.

  Return value:
  false        - Task is sleeping, or invalid task ID.
  true         - Task is awake
*/
bool TISM_IsTaskAwake(int TaskID);


/*
  Description
  Check if the specified Task is a system task by checking first 5 characters for "TISM_".

  Parameters:
  int TaskID   - The number of the task to check.

  Return value:
  true         - Specified task is a system task.
  false        - Specified task is not a system task.
*/
bool TISM_IsSystemTask(int TaskID);


/*
  Description
  Register a new task in the global System struct.

  Parameters:
  int *Function           - Pointer to the function for this task; function returns int and takes no variables.
  char *Name              - Pointer to text buffer with name of this process.
  int TaskDefaultPriority - Priority for this task (PRIORITY_HIGH, PRIORITY_NORMAL, PRIORITY_LOW or other value in msec).

  Return value:
  ERR_TOO_MANY_TASKS      - Attempt was made to register > MAX_TASKS.
  OK                      - Succes
*/
int TISM_RegisterTask(uint8_t (*Function)(TISM_Task), char *Name, uint32_t TaskPriority);


/*
  Description
  Initialize the global System-struct by providing default values. Furthermore, register the standard TISM tasks.

  Parameters:
  None                    - This function works my modifying the global System and Task structs.

  Return value:
  ERR_INITIALIZING        - Error occured during initializing the system.
  OK                      - Succes
*/
int TISM_InitializeSystem();


/*
  IRQHandler.c
  ============
  Definition of ITSM IRQ handler that generates messages to task after specific events have occured.
  This task uses the global circular buffer IRQHandlerInboundQueue defined in TISM_Definitions.h

  The IRQ handler process:
  - Task subscribe thenselves to events on GPIOs by using TISM_IRQHandlerSubscribe-function, which sends a 
    subscription-message to TISM_IRQHandler (type=GPIO number, message=interrupt events).
  - TISM_IRQHander registers the subscriptions (using linear list) and the generic eventhandler to the specified GPIO.
  - When an interrupt is received TISM_IRQHandler (TISM_IRQHandlerCallback function) writes a message to IRQHandlerInboundQueue.
  - TISM_Scheduler continuously checks for the availability of new messages in this queue. If a message is waiting, 
    TISM_IRQHandler is started.
  - Based on the linear list TISM_IRQHandler sends messages to the corresponding tasks via the regular messaging method.
  - The subscribed tasks will receive a message coming from TISM_IRQHandler, GPIO number as message type and Event number as 
    message for further processing.
*/

/*
  Description 
  Subscribe a task to a specific event occuring on the specified GPIO by sending a request message
  to TISM_IRQHandler. No error checking on the specified GPIOs or Event types. WHen the specified event occurs,
  TISM_IRQHandler sends a message (message type is the GPIO number).

  Note: when a subscription is handled for the first time for a specific GPIO, the port function is set to
        GPIO_FUNC_SIO, direction is set to input and treated as 'pull down' port.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint GPIO                  - GPIO subscription to modify.
  uint32_t Events            - Events to subscribe to (one or more, by applying bitwise OR '|'):
                               GPIO_IRQ_LEVEL_LOW
                               GPIO_IRQ_LEVEL_HIGH
                               GPIO_IRQ_EDGE_FALL
                               GPIO_IRQ_EDGE_RISE                                                                                             
                               IRQ_UNSUBSCRIBE     - Unsubscribe from the specified GPIO interrupts
  bool GPIOPullDown          - Initialize GPIO with pull-down resistor when ´true´. The first registration determines the actual setting.
  uint32_t AntiBounceTimeout - Timeout period (in usec) that needs to expire before next similar interrupt is forwarded 
                               (anti-bounce measure). Max. timeout period is 16777215 usec (24 bits); we use the remaining bits
                               to pass other parameters through the message.

  Events: GPIO_IRQ_LEVEL_LOW = 0x1u , GPIO_IRQ_LEVEL_HIGH = 0x2u , GPIO_IRQ_EDGE_FALL = 0x4u , GPIO_IRQ_EDGE_RISE = 0x8u

  Return value:
  false - Message delivery failed.
  true  - Request sent.
*/
bool TISM_IRQHandlerSubscribe(TISM_Task ThisTask, uint8_t GPIO, uint32_t Events, bool GPIOPullDown, uint32_t AntiBounceTimeout);


/*
  Description
  The main task for the IRQ handler. Handles the initialization and registration of subscriptions to GPIO interrupts.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  OK              - Task run completed succesfully.
  None-zero value - Error
*/
uint8_t TISM_IRQHandler(TISM_Task ThisTask);


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

*/

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
uint16_t TISM_CircularBufferMessagesWaiting(struct TISM_CircularBuffer *Buffer);


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
uint16_t TISM_CircularBufferSlotsAvailable(struct TISM_CircularBuffer *Buffer);


/*
  Description
  Read the first unread message from stack, do not delete (don't move tail)

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  *TISM_message - Pointer to message of type struct TISM_Message; the current message in the buffer.
*/
struct TISM_Message *TISM_CircularBufferRead(struct TISM_CircularBuffer *Buffer);


/*
  Description
  Remove the first unread message from stack by advancing the tail +1.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/
void TISM_CircularBufferDelete(struct TISM_CircularBuffer *Buffer);


/*
  Description
  Insert data into the current position of the circular buffer (head) and and advance head +1.

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
bool TISM_CircularBufferWrite(struct TISM_CircularBuffer *Buffer, uint8_t SenderTaskID, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Message, uint32_t Specification);
 

/*
  Description
  (Virtually) remove all messages by setting the tail at the same position as the head.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/
void TISM_CircularBufferClear(struct TISM_CircularBuffer *Buffer);


/*
  Description
  Initialize the circular buffer. For read safety, add default values.

  Parameters:
  *TISM_CircularBuffer - Pointer to the TISM_CircularBuffer struct.

  Return value:
  None
*/

void TISM_CircularBufferInit(struct TISM_CircularBuffer *Buffer); 


/*
  TISM_Postman.c
  ==============
  Tools for managing the postboxes and delivery of messages between tasks.

  The TISM_Postman process:
  - Each core runs an instance of TISM_Scheduler; each instance has its own outbound messaging queue (circular buffer). 
    This is used for outgoing messages, generated by tasks during their run cycle.
  - Each task has a separate inbound message queue (circular buffer).
  - When a task cycle is completed, TISM_Scheduler checks the outbound queue for messages to be processed. If these are
    present, TISM_Postman is called by TISM_Scheduler to process messages for that specific outbound queue.
  - TISM_Postman handles the delivery of messages by processing the messages from the outbound queue for the specific
    scheduler-instance, and delivers it to the inbound queue of the specified task. 
  - As only one instance of TISM_Postman can run at a time, this process is thread-safe.

  TISM_Postman uses the functions in TISM_Messaging; the definitions of messaging struct is defined in TISM_Definitions.
  The outbound queues for the TISM_Scheduler instances are global variables; OutboundMessageQueue[CORE0] and OutboundMessageQueue[CORE1].
  TISM_Postman provides for 4 consumer functions to easily manage messaging.
*/

/*
  Description
  Wrapper for TISM_CircularBufferMessagesWaiting; allows tasks to easer check if a message is waiting in their inbound queue.
  
  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  <value>            - Integer value of number of messages waiting
  0                  - No messages waiting
*/
uint16_t TISM_PostmanMessagesWaiting(TISM_Task ThisTask);


/*
  Description
  Wrapper for TISM_CircularBufferWrite; allows tasks to easier write messages to the outbound queue.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t RecipientTaskID    - TaskID of the recipient.
  uint8_t MessageType        - Type of message (see TISM_Definitions.h).
  uint32_t Message           - Message. Could also contain a pointer to something (e.g. text buffer).
  uint32_t Specification     - Specification to the provided message. Could also contain a pointer to something (e.g. text buffer).

  Return value:
  false - Buffer is full.
  true  - Succes 
*/
bool TISM_PostmanWriteMessage(TISM_Task ThisTask, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Message, uint32_t Specification);


/*
  Description
  Wrapper for TISM_CircularBufferRead; allows tasks to easier read messages from the inbound queue.

  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  *TISM_message      - Pointer to message of type struct TISM_Message; the current message in the buffer.
*/
struct TISM_Message *TISM_PostmanReadMessage(TISM_Task ThisTask);


/*
  Description
  Wrapper for TISM_CircularBufferDelete; allows tasks to easier delete the first message from their inbound queue.

  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  none
*/
void TISM_PostmanDeleteMessage(TISM_Task ThisTask);


/*
  Description
  The main task for the Postman of TISM. Handles the distribution of messages between tasks.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  OK                 - Task run completed succesfully.
  None-zero value    - Error
*/
uint8_t TISM_Postman(TISM_Task ThisTask);


/*
  TISM_Scheduler.c
  ================
  The scheduler of the TISM-system (non-preemptive/cooperative multitasking).

  The scheduling process:
  - When each task is registered a priority is specified (PRIORITY_HIGH, PRIORITY_NORMAL and PRIORITY_LOW). This is
    actually a value describing the number of microseconds between each run of the specified task ("wake up timer").
  - The TaskIDs of all tasks are collected in a list; CORE0 cycles through the list from bottom to top, 
    CORE1 in the other direction, both via a separate instance of TISM_Scheduler.
  - When running through the list the priorities of tasks are considered via a cycle; first tasks with PRIORITY_HIGH
    are evaluated, then PRIORITY_NORMAL and higher, and last PRIORITY_LOW and higher. This means that tasks with
    PRIORITY_HIGH are executed more frequently and get the most CPU-time; PRIORITY_NORMAL a bit less etc.
  - For each "run through the list" the scheduler makes the following evaluations:
    - Are both cores (TISM_Scheduler instances) looking at the same TaskID ("collision")? If so, skip.
    - Is the task sleeping? If so, skip.
    - Is the wake-up timer for this task expired? If not, skip.
    If all these conditions do not apply the task is executed. When any other value than OK (0) is returned, the scheduler stops
    and generates a fatal error.
  - When a task has run succesfully the outbound messagequeue for the specific instance of TISM_Scheduler is checked. If
    messages are waiting, TISM_Postman (delivery of messages) and TISM_Taskmanager (wake up tasks who have received 
    messages) are started.
  - Last, check if any interrupts where received. If so then start TISM_IRQHandler, followed by TISM_Postman 
    and TISM_Taskmanager.

  As this is non-preemptive/cooperative multitasking, this mechanism only works if each task briefly executes and 
  then exits, freeing up time for other tasks to run.
*/

/*
  Description:
  The scheduler of TISM. Schedulers running on both cores run through the same task list; when wake-up timer is expired
  the task is started. The even core runs the queue ascending; uneven core descending.

  Parameters:
  int ThisCoreID          - ID of the current core we're trying to run tasks for.
  
  Return value:
  ERR_RUNNING_TASK        - One of the Tasks returned an error when executing.
  OK                      - System stopped without any errors.
*/
uint8_t TISM_Scheduler(uint8_t ThisCoreID);


/*

  TISM_SoftwareTimer.c
  ====================
  Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.
  Virtual timers are not 'real' timers but timer values are calculated and to be checked regularly for expiration.
  Software timers are registered and a message is sent to the requesting task once the timer has expired.
  As this is a software timer it isn´t very accurate; therefore timer values for software timers are specified in milliseconds.
  Despite the inaccuracy software timers are still very usefull for scheduling (repetitive) tasks.

*/

/*
  Description
  Create a virtual software timer. No events, just calculate the value of future timer and return the value.
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'.
  
  Parameters:
  uint64_t TimerUsec - Time in usec when timer should expired.

  Return value:
  uint64_t - Timestamp; 'NOW' + when the timer should expire.
*/
uint64_t TISM_SoftwareTimerSetVirtual(uint64_t TimerUsec);


/*
  Description
  Check the status of the virtual software timer - is it expired? If so, return true.
  
  Parameters:
  uint64_t TimerUsec - Timer value ('NOW' + timer in usec).

  Return value:
  true  - Timer is expired.
  false - Timer hasn´t expired.
*/
bool TISM_SoftwareTimerVirtualExpired(uint64_t TimerUsec);


/*
  Description
  Create a SoftwareTimerEntry struct and send a message to TISM_SoftwareTimer in order to register a new timer.
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t TimerID            - The identifier for this specific timer (unique for this Task ID).
  bool RepetitiveTimer       - Repetitive timer (true/false)
  uint32_t TimerIntervalMsec - Time in milliseconds when timer should expired (measured from 'NOW').

  Return value:
  true                       - Timer set 
  false                      - Error setting the timer
*/
bool TISM_SoftwareTimerSet(TISM_Task ThisTask, uint8_t TimerID, bool RepetitiveTimer, uint32_t TimerIntervalMsec);


/*
  Description
  Cancel a specific timer - erase the entry from the linear list.
  
  Parameters:
  uint8_t TaskID  - TaskID for which the timer was set.
  uint8_t TimerID - ID of the timer to cancel

  Return value:
  none
*/
bool TISM_SoftwareTimerCancel(TISM_Task ThisTask, uint8_t TimerID);


/*
  Description:
  This is the task that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  
  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_SoftwareTimer(TISM_Task ThisTask);


/*
  TISM_TaskManager.c
  ==================
  Library with functions to manipulate task properties and system states, when requested via messages.

  All manipulation of system and task states are handled via TISM_Taskmanager to prevent two tasks from changing
  the state of things at the same time, with unexpected results. As only one instance of TISM_Taskmanager can run at a
  time, thread safety is achieved. 
  
  Note: Still we're not completely thread-safe as TISM_Taskmanager can write to these variables, but 
        TISM_Scheduler running on another core can attempt to read the same variable at the same time.
*/

/*
  Description
  Set the specified attribute of a task (see attibutes above).

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t TargetTaskID       - TaskID of the task to change
  uint8_t AttributeToChange  - Attribute to change (see below)
  uint32_t Setting           - New setting (see below)

  Return value:
  ERR_INVALID_OPERATION      - Invalid request
  ERR_TASK_NOT_FOUND         - Specified task not found
  OK                         - Succes

  AttributeToChange and corresponding Setting values:
  TISM_SET_TASK_STATE        - Change the state of a task.
                               Setting: Custom value or predefined (recommended): DOWN, STOP, RUN or INIT
  TISM_SET_TASK_PRIORITY     - Set the priority of a specific task to PRIORITY
                               Setting: PRIORITY_LOW, PRIORITY_NORMAL or PRIORITY_HIGH
  TISM_SET_TASK_SLEEP        - Set the sleep state of a specific state 
                               Setting: true or false
  TISM_SET_TASK_WAKEUPTIME   - Set the timestamp of the next wake up (in usec); interpreted as "NOW"+timestamp
                               Setting: timestamp in usec
  TISM_SET_TASK_DEBUG        - Set the debug level of a specific task
                               Setting: DEBUG_NONE, DEBUG_NORMAL or DEBUG_HIGH  
  TISM_WAKE_ALL_TASKS        - Wake all tasks.
                               Setting: 0
  TISM_DEDICATE_TO_TASK      - Dedicate the whole system to a specific task (use with caution)
                               Setting: 0
*/
uint8_t TISM_TaskManagerSetTaskAttribute(struct TISM_Task ThisTask, uint8_t TargetTaskID, uint8_t AttributeToChange, uint32_t Setting);


/*
  Description
  Wrapper for TISM_TaskManagerSetTaskAttribute; set the specified attribute for the requesting task itself.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t AttributeToChange  - Attribute to change (see below)
  uint32_t Setting           - New setting (see below)

  Return value:
  ERR_INVALID_OPERATION      - Invalid request
  ERR_TASK_NOT_FOUND         - Specified task not found
  OK                         - Succes

  AttributeToChange and corresponding Setting values:
  TISM_SET_TASK_STATE        - Change the state of a task.
                               Setting: Custom value or predefined (recommended): DOWN, STOP, RUN or INIT
  TISM_SET_TASK_PRIORITY     - Set the priority of a specific task to PRIORITY
                               Setting: PRIORITY_LOW, PRIORITY_NORMAL or PRIORITY_HIGH
  TISM_SET_TASK_SLEEP        - Set the sleep state of a specific state 
                               Setting: true or false
  TISM_SET_TASK_WAKEUPTIME   - Set the timestamp of the next wake up (in usec); interpreted as "NOW"+timestamp
                               Setting: timestamp in usec
  TISM_SET_TASK_DEBUG        - Set the debug level of a specific task
                               Setting: DEBUG_NONE, DEBUG_NORMAL or DEBUG_HIGH  
  TISM_WAKE_ALL_TASKS        - Wake all tasks.
                               Setting: 0
  TISM_DEDICATE_TO_TASK      - Dedicate the whole system to a specific task (use with caution)
                               Setting: 0
*/
uint8_t TISM_TaskManagerSetMyTaskAttribute(struct TISM_Task ThisTask, uint8_t AttributeToChange, uint32_t Setting);


/*
  Description
  Set the state of the entire TISM system. Any task can alter the system state.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t SystemState        - System state (see above)

  Return value:
  non-zero value             - Error sending the request
  OK                         - Succes
*/
uint8_t TISM_TaskManagerSetSystemState(struct TISM_Task ThisTask, uint8_t SystemState);


/*
  Description:
  This is the TaskManager-function that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information. 

  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_TaskManager(TISM_Task ThisTask);


/*
  TISM_Watchdoc.c
  ===============
  Task to check if other tasks are still alive. Generate warnings to STDERR in case of timeouts.
  This is the function that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  
  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/	
uint8_t TISM_Watchdog(TISM_Task ThisTask);


// Inclusion of the code segments
#include "TISM_IRQHandler.c"
#include "TISM_SoftwareTimer.c"
#include "TISM_Scheduler.c"
#include "TISM_Postman.c"
#include "TISM_Messaging.c"
#include "TISM.c"
#include "TISM_TaskManager.c"
#include "TISM_Watchdog.c"


#endif