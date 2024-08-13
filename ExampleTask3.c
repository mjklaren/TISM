/*
  
  Example task 3; a task that logs a counter every few seconds (specified by TIMERINTERVAL, triggered by the software timer). 
  The counter is used to count the number of runs within each interval. The priority of this task is changed when 
  messages are received from ExampleTask1. So the amount of succesful runs within the specified timeframe will be different;
  holding the button down will keep the process' priority high.

  This code uses the taskManager, software timer and messaging functions.

  Note: requests to change task attributes are done via the messaging system. Messages are processed AFTER a task completes a run.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include "TISM.h"

#define EXAMPLETASK3_TIMERINTERVAL 2500  // in msec
#define EVENTID                    222   // Unique ID for the repetitive timer


// The structure containing all data for this task to run. 
struct ExampleTask3Data
{
  uint32_t NumberOfRunsCounter;
} ExampleTask3Data;


/*
  Description:
  Example task 3; a task that write characters to STDOUT, but the speed is changed by changing the task's priority.
  This change is triggered by button pressed and released events, as received via messages from ExampleTask1. 
  To make the effects of the change more visible we will apply a multiplier to the task's priority, which will increase
  the time between runs (basically slowing things down).

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t ExampleTask3 (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  // The scheduler maintains the state of the task and the system.
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Set a repetitive timer to wake up this task
                TISM_SoftwareTimerSet(ThisTask,EVENTID,true,EXAMPLETASK3_TIMERINTERVAL);
                ExampleTask3Data.NumberOfRunsCounter=0;

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
                    case TISM_PING:          // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                             TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                             break;
                    case EVENTID:            // Timer expired; write the number of runs in the interval to the log.
                                             TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of runs in this cycle: %d.", ExampleTask3Data.NumberOfRunsCounter);
                                             ExampleTask3Data.NumberOfRunsCounter=0;
                                             break;
                    case GPIO_IRQ_EDGE_FALL: // Button is pressed (message from ExampleTask1). Increase this task's priority.
                            		             if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message received; button pressed.");

                                             TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_PRIORITY,PRIORITY_HIGH);
                                             break;   
                    case GPIO_IRQ_EDGE_RISE: // Button is released (message from ExampleTask1). Set this task's priority to normal.
                                             if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message received; button released.");

                                             TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_PRIORITY,PRIORITY_NORMAL);
                                             break;
                    default:                 // Unknown message type - ignore.
                                            break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }
				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Increase the counter of total runs in this interval with 1.
  ExampleTask3Data.NumberOfRunsCounter++;
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

