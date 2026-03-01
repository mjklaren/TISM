/*

  Example task 5; a task that reads incoming packets from the uarts and writes them to the eventlog. 
  
  This code uses the UART Message Exchange and event logger.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include "TISM.h"

#define EXAMPLETASK5_UART  
#define EXAMPLETASK5_TXPIN
#define EXAMPLETASK5_RXPIN


// Static variables needed for this task to run.
static int EmulateLoad, MaxNumberTaskStarts, TaskStarts;


//todo


/*
  Description:
  Example task 4; a task that creates artifical load on the system (basically introducing waits) and limits the amount of runs.

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t ExampleTask5 (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  // The scheduler maintains the state of the task and the system.
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				       
        /*
                // Set the load we need to emulate and the maximum number of runs.
                EmulateLoad=ExampleTask5_EMULATELOAD;
                MaxNumberTaskStarts=ExampleTask5_MAXTASKSTARTS;
                TaskStarts=0;

                if(EmulateLoad>0) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - we're emulating load of %dms.", EmulateLoad);
                if(MaxNumberTaskStarts>0) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - system will stop after %d runs.", MaxNumberTaskStarts);                
        */
                // For tasks that only respond to events (=messages) we could set the sleep attribute to ´true'.
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %s-0x%02X from TaskID %d (HostID %d) received.", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                    TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                    break;
                    default:        // Unknown message type - ignore.
                                    break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Other work to do in this state.
      /*
                // Do we need to emulate a load?
                if(EmulateLoad>0)
                {
                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Emulating load of %dms for task %s.", EmulateLoad, ThisTask.TaskName);
                  sleep_ms (EmulateLoad);    
                }

                // Do we have a limit on task starts?
                if(MaxNumberTaskStarts>0)
                {
                  TaskStarts++;
                  if(TaskStarts>MaxNumberTaskStarts)
                  {
                    // Maximum reached - send a message to taskmanager to stop the system.
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Maximum number of runs (%d) reached; stopping.", MaxNumberTaskStarts);
                    
                    // Stop the system by requesting the TaskManager.
                    TISM_SchedulerSetSystemState(ThisTask, STOP);
                  }
                  else
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of runs: %d.", TaskStarts);
                }
       */
				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

