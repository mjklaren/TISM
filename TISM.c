/* 
  
  TISM.c
  ======
  "The Incredible State Machine" - functions to set up the system and some generic tools.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include <string.h>
#include <stdio.h>
#include "pico/unique_id.h"
#include "TISM.h"


// THE global variable - the struct containing the entire TISM-system.
TISM_System System;


/*
  Description
  Convert error number/message type to string. Return pointer to text string, representing the message type.

  Parameters:
  uint8_t MessageType - MessageType identifier.

  Return value:
  const char* Pointer to text string.
*/
const char* TISM_MessageTypeToString(uint8_t MessageType)
{
  switch (MessageType) 
  {
    // Error messages
    case OK:                            return "OK";
    case ERR_TOO_MANY_TASKS:            return "ERR_TOO_MANY_TASKS";
    case ERR_INITIALIZING:              return "ERR_INITIALIZING";
    case ERR_MAILBOX_FULL:              return "ERR_MAILBOX_FULL";
    case ERR_RECIPIENT_INVALID:         return "ERR_RECIPIENT_INVALID";
    case ERR_TASK_NOT_FOUND:            return "ERR_TASK_NOT_FOUND";
    case ERR_TASK_SLEEPING:             return "ERR_TASK_SLEEPING";
    case ERR_RUNNING_TASK:              return "ERR_RUNNING_TASK";
    case ERR_INVALID_OPERATION:         return "ERR_INVALID_OPERATION";
    case UARTMX_RX_ERR_START:           return "UARTMX_RX_ERR_START";
    case UARTMX_RX_ERR_END:             return "UARTMX_RX_ERR_END";
    case UARTMX_RX_ERR_LENGTH:          return "UARTMX_RX_ERR_LENGTH";
    case UARTMX_RX_ERR_VERSION:         return "UARTMX_RX_ERR_VERSION";
    case UARTMX_RX_ERR_NETWORKID:       return "UARTMX_RX_ERR_NETWORKID";
    case UARTMX_RX_ERR_CRC:             return "UARTMX_RX_ERR_CRC";
    case UARTMX_RX_ERR_SENDERHOSTID:    return "UARTMX_RX_ERR_SENDERHOSTID";
    case UARTMX_RX_ERR_MSGSIZE:         return "UARTMX_RX_ERR_MSGSIZE";
    case UARTMX_RX_TISMMSG_ERR:         return "UARTMX_RX_TISMMSG_ERR";

    // TISM message types
    case TISM_TEST:                     return "TISM_TEST";
    case TISM_ACK:                      return "TISM_ACK";
    case TISM_NAK:                      return "TISM_NAK";
    case TISM_PING:                     return "TISM_PING";
    case TISM_ECHO:                     return "TISM_ECHO";
    case TISM_DISCOVER:                 return "TISM_DISCOVER";
    case TISM_INTRODUCE:                return "TISM_INTRODUCE";
    case TISM_RESOLVE_TASKNAME:         return "TISM_RESOLVE_TASKNAME";
    case TISM_RESOLVE_TASKNAME_REPLY:   return "TISM_RESOLVE_TASKNAME_REPLY";
    case TISM_RESOLVE_TASKID:           return "TISM_RESOLVE_TASKID";
    case TISM_RESOLVE_TASKID_REPLY:     return "TISM_RESOLVE_TASKID_REPLY";
    case TISM_LOG_EVENT_NOTIFY:         return "TISM_LOG_EVENT_NOTIFY";
    case TISM_LOG_EVENT_ERROR:          return "TISM_LOG_EVENT_ERROR";
    case TISM_SET_SYS_STATE:            return "TISM_SET_SYS_STATE";
    case TISM_SET_TASK_STATE:           return "TISM_SET_TASK_STATE";
    case TISM_SET_TASK_SLEEP:           return "TISM_SET_TASK_SLEEP";
    case TISM_SET_TASK_WAKEUPTIME:      return "TISM_SET_TASK_WAKEUPTIME";
    case TISM_SET_TASK_DEBUG:           return "TISM_SET_TASK_DEBUG";
    case TISM_WAKE_ALL_TASKS:           return "TISM_WAKE_ALL_TASKS";
    case TISM_DEDICATE_TO_TASK:         return "TISM_DEDICATE_TO_TASK";
    case TISM_MX_SUBSCRIBE:             return "TISM_MX_SUBSCRIBE";
    case TISM_MX_UNSUBSCRIBE:           return "TISM_MX_UNSUBSCRIBE";
    case TISM_CANCEL_TIMER:             return "TISM_CANCEL_TIMER";
    case TISM_CANCEL_TIMER_BY_NR:       return "TISM_CANCEL_TIMER_BY_NR";
    case TISM_SET_TIMER:                return "TISM_SET_TIMER";

    // Other/user-defined messages
    default:                            return "<User defined>";
  }
}


/*
  Description
  Extract 8-character text from Payload0 and Payload1. Caller provides buffer (must be at least 9 bytes). Returns pointer to the provided, updated buffer.

  Parameters:
  uint32_t Payload0 - Payload0 field from TISM-message struct.
  uint32_t Payload1 - Payload1 field from TISM-message struct.
  choar *Buffer     - Pointer to buffer, atleast 9 bytes in size (2x4 bytes + null character).

  Return value:
  char* pointer to buffer (with updated content)
*/
char* TISM_PayloadsToString(uint32_t Payload0, uint32_t Payload1, char *Buffer)
{
  // Extract bytes from Payload0 (first 4 chars)
  Buffer[0]=(Payload0 >> 24) & 0xFF;
  Buffer[1]=(Payload0 >> 16) & 0xFF;
  Buffer[2]=(Payload0 >> 8) & 0xFF;
  Buffer[3]=Payload0 & 0xFF;
  
  // Extract bytes from Payload1 (last 4 chars)
  Buffer[4]=(Payload1 >> 24) & 0xFF;
  Buffer[5]=(Payload1 >> 16) & 0xFF;
  Buffer[6]=(Payload1 >> 8) & 0xFF;
  Buffer[7]=Payload1 & 0xFF;

  // "Sanitize" non-printable characters
  for(uint8_t Counter=0; Counter<8; Counter++)
  {
    if(Buffer[Counter]!='\0' && (Buffer[Counter]<0x20 || Buffer[Counter]>0x7E))
      Buffer[Counter]='?';  // Replace non-printable with '?'
  }
  Buffer[8]='\0';  // Null terminator
  return Buffer;
}


/*
  Description
  Convert a string (max. 8 chars) to Payload0 and Payload1. The string is padded with nulls if shorter than 8 characters.

  Parameters:
  char *String       - Pointer to text string.
  uint32_t *Payload0 - Pointer to payload0 field from TISM-message struct.
  uint32_t *Payload1 - Pointer to payload1 field from TISM-message struct.

  Return value:
  None. If String is NULL, Payload0 and Payload1 are 0.
*/
void TISM_StringToPayloads(char *String, uint32_t *Payload0, uint32_t *Payload1)
{
  char PaddedName[8]={0};  // Initialize with zeros
  *Payload0=0;
  *Payload1=0;

  // Copy string, max 8 chars
  if(String!=NULL)
  {
    uint8_t Length=strnlen(String, 8);    // Payload0 and Payload1 are combined max. 8 bytes
    memcpy(PaddedName, String, Length);
  
    // "Sanitize" non-printable characters
    for(uint8_t Counter=0; Counter<8; Counter++)
    {
      if(PaddedName[Counter]!='\0' && (PaddedName[Counter]<0x20 || PaddedName[Counter]>0x7E))
        PaddedName[Counter]='?';  // Replace non-printable with '?'
    }
    
    // Pack into Payload0 (first 4 bytes, big-endian)
    *Payload0=((uint32_t)PaddedName[0] << 24) | ((uint32_t)PaddedName[1] << 16) | ((uint32_t)PaddedName[2] << 8) | (uint32_t)PaddedName[3];
  
    // Pack into Payload1 (last 4 bytes, big-endian)
    *Payload1=((uint32_t)PaddedName[4] << 24) | ((uint32_t)PaddedName[5] << 16) | ((uint32_t)PaddedName[6] << 8) | (uint32_t)PaddedName[7];
  }
}


/*
  Description 
  Get the TaskID for the task specified by name from the global Task structs.

  Parameters:
  *TaskName    - Pointer to a string with the name of the task.

  Return value:
  0            - Specified task name not found.
  <value>      - Task ID for the specific task, starting with 0.
*/
uint8_t TISM_GetTaskID(char *TaskName)
{
  uint8_t TaskID=0;
  for(uint8_t Counter=0; Counter<System.NumberOfTasks; Counter++)
    if(strncmp(System.Task[Counter].TaskName, TaskName, MAX_TASK_NAME_LENGTH)==0)
      TaskID=Counter;
  return(TaskID);
}


/*
  Description
  Check if the specified TaskID is valid (=in use)

  Parameters:
  uint8_t TaskID - The number of the task to check.

  Return value:
  false          - Invalid task ID
  true           - Valid task ID
*/
bool TISM_IsValidTaskID(uint8_t TaskID)
{
  if((TaskID>=0) && (TaskID<System.NumberOfTasks))
    return(true);
  return(false);
}


/*
  Description
  Check if the specified Task is awake.

  Parameters:
  int TaskID   - The number of the task to check.

  Return value:
  false        - Task is sleeping, or invalid task ID.
  true         - Task is awake
*/
bool TISM_IsTaskAwake(uint8_t TaskID)
{
  if((TaskID>=0) && (TaskID<System.NumberOfTasks))
    return(!System.Task[TaskID].TaskSleeping);
  return(false);
}


/*
  Description
  Check if the specified Task is a system task by checking first 2 characters for "T_".

  Parameters:
  uint8_t TaskID - The number of the task to check.

  Return value:
  true           - Specified task is a system task.
  false          - Specified task is not a system task.
*/
bool TISM_IsSystemTask(uint8_t TaskID)
{
  return(strncmp(System.Task[TaskID].TaskName, "T_", 2)==0?true:false);
}


/*
  Description
  Register a new task in the global System struct.

  Parameters:
  uint8_t *Function       - Pointer to the function for this task; function returns int and takes no variables.
  char *Name              - Pointer to text buffer with name of this process.
  int TaskDefaultPriority - Priority for this task (PRIORITY_HIGH, PRIORITY_NORMAL, PRIORITY_LOW or other value in msec).

  Return value:
  ERR_TOO_MANY_TASKS      - Attempt was made to register > MAX_TASKS.
  OK                      - Succes
*/
uint8_t TISM_RegisterTask(uint8_t (*Function)(TISM_Task), char *Name, uint32_t TaskPriority)
{
  // Register the task-related data in the struct. Default values will be placed when initializing the System.
  // Check if not too many tasks are registered.
  if(System.NumberOfTasks>=MAX_TASKS)
  {
	  // We have reached our task limit.	
	  fprintf(STDERR, "TISM_RegisterTask: too many tasks to register (maximum: %d) while attempting to register %s.\n", MAX_TASKS, Name);
	  return(ERR_TOO_MANY_TASKS);
  }
	
  // Set task-related information.
  System.Task[System.NumberOfTasks].TaskID=System.NumberOfTasks;
  System.Task[System.NumberOfTasks].RunningOnCoreID=-1;
  snprintf(System.Task[System.NumberOfTasks].TaskName, MAX_TASK_NAME_LENGTH+1, "%s", Name);
  System.Task[System.NumberOfTasks].TaskFunction=Function;
  System.Task[System.NumberOfTasks].TaskState=INIT;
  System.Task[System.NumberOfTasks].TaskDebug=System.SystemDebug;
  System.Task[System.NumberOfTasks].TaskPriority=TaskPriority;

#ifndef TISM_DISABLE_SCHEDULER
  System.Task[System.NumberOfTasks].TaskWakeUpTimer=0;
#endif

  System.Task[System.NumberOfTasks].TaskSleeping=false;
  System.Task[System.NumberOfTasks].TaskDebug=DEBUG_NONE;

  // Initialize the inbound and outbound messaging queues for this task. Place a pointer to the corresponding queue in the task struct.
  System.Task[System.NumberOfTasks].InboundMessageQueue=TISM_PostmanBufferInit(MAX_MESSAGES,sizeof(TISM_Message));
  System.Task[System.NumberOfTasks].OutboundMessageQueue=NULL;          // Will be set by the scheduler, will point to the queue for a core.

  if(System.SystemDebug>=DEBUG_LOW) 
    fprintf (STDOUT, "TISM: Task %s registered as task ID %d with priority %d.\n", System.Task[System.NumberOfTasks].TaskName, System.NumberOfTasks, System.Task[System.NumberOfTasks].TaskPriority);
  System.NumberOfTasks++;

  return(OK);
}


/*
  Description
  Initialize the global System-struct by providing default values. Furthermore, register the standard TISM tasks.

  Parameters:
  None                    - This function works my modifying the global System and Task structs.

  Return value:
  ERR_INITIALIZING        - Error occured during initializing the system.
  OK                      - Succes
*/
uint8_t TISM_InitializeSystem()
{
  // Set the system wide debug level
  System.SystemDebug=SYSTEM_DEBUG_LEVEL;     // Set the system wide debug level in TISM.h. Use with caution!

  // Initialize the RP2040
  stdio_init_all();
  snprintf(System.Hostname, MAX_TASK_NAME_LENGTH+1, "%-4s%04u", HOSTNAME_PREFIX, (unsigned)HOSTID);  
  System.HostID=HOSTID;
  System.NetworkID=NETWORKID;
  System.RebootAfterShutdown=false;

  // Read the unique ID of the Pico and store it in a 64 bit variable.
  pico_unique_board_id_t PicoBoardID;
  pico_get_unique_board_id(&PicoBoardID);
  for (int counter=0; counter<PICO_UNIQUE_BOARD_ID_SIZE_BYTES; counter++)
    System.UniqueID=(System.UniqueID<<8) | PicoBoardID.id[counter];

#ifdef SYSTEM_READY_PORT  
  // Set the SYSTEM_READY_PORT to low to indicate that the system is not ready (yet). The TISM_Scheduler will set the port high.
  gpio_init(SYSTEM_READY_PORT);
  gpio_set_dir(SYSTEM_READY_PORT, GPIO_OUT);
  gpio_put(SYSTEM_READY_PORT, 0);
#endif

  sleep_ms(STARTUP_DELAY);    // Add some sleep to allow USB comms to initialize.

  // Initialize the TISM-system. Provide variables default values where possible and register the TISM system tasks.
  System.State=INIT;
#ifndef TISM_DISABLE_MULTICORE
  for(int counter=0;counter<MAX_CORES;counter++)
#else
  for(int counter=0;counter<1;counter++)
#endif
  {
    // Uneven core numbers start at 0 and run the queue upwards; even cores start at the last task and run downwards.
    System.RunPointer[counter]=255;            // 255 shows this pointer isn´t used yet; 0 is also a valid task number.
    System.RunPointerDirection[counter]=(counter%2==0?QUEUE_RUN_ASCENDING:QUEUE_RUN_DESCENDING);

    // Initialize the circular buffer for outbound messages generated by the running task
    System.OutboundMessageQueue[counter]=TISM_PostmanBufferInit(OUTBOUND_MAX_MESSAGES, sizeof(TISM_Message));
  }
  System.NumberOfTasks=0;
	                           
  // Now register the standard TISM_processes.
  if ((TISM_RegisterTask(NULL, "T_Schedl", PRIORITY_HIGH)+                           // Dummy entry for the scheduler itself
       TISM_RegisterTask(&TISM_EventLogger, "T_Eventl", PRIORITY_LOW)+               // EventLogger
       TISM_RegisterTask(&TISM_Postman, "T_Postmn", PRIORITY_LOW)+                   // Postman

  // Optional components
#ifndef TISM_DISABLE_WATCHDOG       
       TISM_RegisterTask(&TISM_Watchdog, "T_Wtchdg", PRIORITY_LOW)+                  // Watchdog
#endif       

#ifndef TISM_DISABLE_SCHEDULER            
       TISM_RegisterTask(&TISM_SoftwareTimer, "T_SofTim", PRIORITY_HIGH)+            // SoftwareTimer
#endif

#ifndef TISM_DISABLE_UARTMX
       TISM_RegisterTask(&TISM_UartMX, "T_UartMX", PRIORITY_HIGH)+                   // UartMX
       TISM_RegisterTask(&TISM_NetworkManager, "T_NetMgr", PRIORITY_LOW)+            // NetworkManager
#endif

       TISM_RegisterTask(&TISM_IRQHandler, "T_IRQHnd", PRIORITY_LOW)+                // IRQHandler
       TISM_RegisterTask(&TISM_Console, "T_Consol", PRIORITY_LOW))!=0)               // Console
  {
    // Some error during setting up ITSM system tasks
    return(ERR_INITIALIZING);
  }
  else
  {
    // Collect the Task IDs for the system tasks.
    System.TISM_PostmanTaskID=TISM_GetTaskID("T_Postmn");
    System.TISM_IRQHandlerTaskID=TISM_GetTaskID("T_IRQHnd");
    System.TISM_EventLoggerTaskID=TISM_GetTaskID("T_Eventl");

#ifndef TISM_DISABLE_WATCHDOG  
    System.TISM_WatchdogTaskID=TISM_GetTaskID("T_Wtchdg");
#endif

#ifndef  TISM_DISABLE_SCHEDULER            
    System.TISM_SoftwareTimerTaskID=TISM_GetTaskID("T_SofTim");
#endif

#ifndef TISM_DISABLE_UARTMX
    System.TISM_UartMXTaskID=TISM_GetTaskID("T_UartMX");
    System.TISM_NetworkManagerTaskID=TISM_GetTaskID("T_NetMgr");
#endif

    return(OK);
  }
}
    

