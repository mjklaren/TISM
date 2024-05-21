/* 
  
  TISM.c - "The Incredible State Machine" - functions to set up the system and some generic tools.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include <string.h>
#include <stdio.h>
#include "TISM.h"


// Get the TaskID for the task specified by name from the global Task structs.
int TISM_GetTaskID(char *TaskName)
{
  int TaskID=-1;
  for(int counter=0;counter<System.NumberOfTasks;counter++)
    if(strcmp(System.Task[counter].TaskName,TaskName)==0)
      TaskID=counter;
  return(TaskID);
}


// Check if the specified TaskID is valid (=in use)
bool TISM_IsValidTaskID(int TaskID)
{
  if((TaskID>=0) && (TaskID<System.NumberOfTasks))
    return(true);
  return(false);
}


//  Check if the specified Task is awake.
bool TISM_IsTaskAwake(int TaskID)
{
  if((TaskID>=0) && (TaskID<System.NumberOfTasks))
    return(!System.Task[TaskID].TaskSleeping);
  return(false);
}


// Check if the specified Task is a system task by checking first 5 characters for "TISM_".
bool TISM_IsSystemTask(int TaskID)
{
  return(strncmp(System.Task[TaskID].TaskName,"TISM_",5)==0?true:false);
}


// Register a new task in the global System struct.
int TISM_RegisterTask(uint8_t (*Function)(TISM_Task), char *Name, uint32_t TaskPriority)
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
  strncpy(System.Task[System.NumberOfTasks].TaskName, Name, MAX_TASK_NAME_LENGTH);
  System.Task[System.NumberOfTasks].TaskFunction=Function;
  System.Task[System.NumberOfTasks].TaskState=INIT;
  System.Task[System.NumberOfTasks].TaskDebug=System.SystemDebug;
  System.Task[System.NumberOfTasks].TaskPriority=TaskPriority;
  System.Task[System.NumberOfTasks].TaskWakeUpTimer=0;
  System.Task[System.NumberOfTasks].TaskSleeping=false;
  System.Task[System.NumberOfTasks].TaskDebug=DEBUG_NONE;

  // Initialize the inbound messaging queue for this task. Place a pointer to the corresponding queue in the task struct.
  System.Task[System.NumberOfTasks].InboundMessageQueue=&InboundMessageQueue[System.NumberOfTasks];
  TISM_CircularBufferInit(System.Task[System.NumberOfTasks].InboundMessageQueue); 
  System.Task[System.NumberOfTasks].OutboundMessageQueue=NULL;          // Will be provided by the scheduler.

  if(System.SystemDebug>=DEBUG_LOW) fprintf (STDOUT, "TISM: Task %s registered as task ID %d with priority %d.\n", System.Task[System.NumberOfTasks].TaskName, System.NumberOfTasks, System.Task[System.NumberOfTasks].TaskPriority);

  System.NumberOfTasks++;
  return(OK);
}


// Initialize the global System-struct by providing default values. Furthermore, register the standard TISM tasks.
int TISM_InitializeSystem()
{
  // Initialize the RP2040
  stdio_init_all();
  
  // Set the SYSTEM_READY_PORT to low to indicate that the system is not ready (yet). The TISM_Scheduler will set the port high.
  gpio_init(SYSTEM_READY_PORT);
  gpio_set_dir(SYSTEM_READY_PORT, GPIO_OUT);
  gpio_put(SYSTEM_READY_PORT, 0);
  sleep_ms(STARTUP_DELAY);    // Add some sleep to allow USB comms to initialize.

  // Initialize the TISM-system. Provide variables default values where possible and register the TISM system tasks.
  System.State=INIT;
  for(int counter=0;counter<MAX_CORES;counter++)
  {
    // Uneven core numbers start at 0 and run the queue upwards; even cores start at the last task and run downwards.
    System.RunPointer[counter]=255;            // 255 shows this pointer isn´t used yet; 0 is also a valid task number.
    System.RunPointerDirection[counter]=(counter%2==0?QUEUE_RUN_ASCENDING:QUEUE_RUN_DESCENDING);
    TISM_CircularBufferInit (&(OutboundMessageQueue[counter]));
  }
  System.NumberOfTasks=0;
  TISM_CircularBufferInit (&IRQHandlerInboundQueue); 
	                           
  // Now register the standard TISM_processes.
  if ((TISM_RegisterTask(&TISM_Postman, "TISM_Postman", PRIORITY_LOW)+
       TISM_RegisterTask(&TISM_IRQHandler, "TISM_IRQHandler", PRIORITY_LOW)+
       TISM_RegisterTask(&TISM_Watchdog, "TISM_Watchdog", PRIORITY_LOW)+
       TISM_RegisterTask(&TISM_TaskManager, "TISM_TaskManager", PRIORITY_LOW)+
       TISM_RegisterTask(&TISM_SoftwareTimer, "TISM_SoftwareTimer", PRIORITY_HIGH))!=0)
  {
    // Some error during setting up ITSM system tasks
    return(ERR_INITIALIZING);
  }
  else
  {
    // Collect the Task IDs for the system tasks.
    System.TISM_PostmanTaskID=TISM_GetTaskID("TISM_Postman");
    System.TISM_IRQHandlerTaskID=TISM_GetTaskID("TISM_IRQHandler");
    System.TISM_TaskManagerTaskID=TISM_GetTaskID("TISM_TaskManager");
    System.TISM_WatchdogTaskID=TISM_GetTaskID("TISM_Watchdog");
    System.TISM_SoftwareTimerTaskID=TISM_GetTaskID("TISM_SoftwareTimer");
    return(OK);
  }
}
    

