/*

  Example task 4; a task that creates artifical load on the system (basically introducing waits) and limits the amount of runs
  before shutting down the system. This can be useful for debugging purposes. 
  
  This code uses the taskManager.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include "TISM.h"

#define EXAMPLETASK4_EMULATELOAD   250  // Delay in msec; it will cause watchdog warnings when > WATCHDOG_TASK_TIMEOUT (in usec).
#define EXAMPLETASK4_MAXTASKSTARTS 250  // Number of runs before stopping.


// The structure containing all data for this task to run.
struct ExampleTask4Data
{
  int EmulateLoad, MaxNumberTaskStarts, TaskStarts;
} ExampleTask4Data;


/*
  Description:
  Example task 4; a task that creates artifical load on the system (basically introducing waits) and limits the amount of runs.

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t ExampleTask4 (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  // The scheduler maintains the state of the task and the system.
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Set the load we need to emulate and the maximum number of runs.
                ExampleTask4Data.EmulateLoad=EXAMPLETASK4_EMULATELOAD;
                ExampleTask4Data.MaxNumberTaskStarts=EXAMPLETASK4_MAXTASKSTARTS;
                ExampleTask4Data.TaskStarts=0;

                if(ExampleTask4Data.EmulateLoad>0) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - we're emulating load of %dms.", ExampleTask4Data.EmulateLoad);
                if(ExampleTask4Data.MaxNumberTaskStarts>0) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - system will stop after %d runs.", ExampleTask4Data.MaxNumberTaskStarts);                

                // For tasks that only respond to events (=messages) we could set the sleep attribute to ´true'.
                // TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (%s) received.", MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                    TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                    break;
                    default:        // Unknown message type - ignore.
                                    break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Other work to do in this state.
                // Do we need to emulate a load?
                if(ExampleTask4Data.EmulateLoad>0)
                {
                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Emulating load of %dms for task %s.", ExampleTask4Data.EmulateLoad, ThisTask.TaskName);
                  sleep_ms (ExampleTask4Data.EmulateLoad);    
                }

                // Do we have a limit on task starts?
                if(ExampleTask4Data.MaxNumberTaskStarts>0)
                {
                  ExampleTask4Data.TaskStarts++;
                  if(ExampleTask4Data.TaskStarts>ExampleTask4Data.MaxNumberTaskStarts)
                  {
                    // Maximum reached - send a message to taskmanager to stop the system.
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Maximum number of runs (%d) reached; stopping.", ExampleTask4Data.MaxNumberTaskStarts);
                    
                    // Stop the system by requesting the TaskManager.
                    TISM_TaskManagerSetSystemState(ThisTask, STOP);
                  }
                  else
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of runs: %d.", ExampleTask4Data.TaskStarts);
                }
				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

