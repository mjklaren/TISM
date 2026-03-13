/*

  A template for a task running in the TISM-system.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <TISM.h>


/*
  All data for this task to run. These variables allow the task to remain its state as the stack 
  and heap are not saved between runs. We use static variables as an easy way to make sure variable
  names are unique in the whole system.

  YOUR CODE HERE.
*/
static int YourVariable1, YourVariable2;  


/*
  Description:
  This is the function that is registered in the TISM-system via the TISM_RegisterTask. A pointer to this function is used.
  For debugging purposes the TISM_EventLoggerLogEvent-function is used (not mandatory).

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t TaskTemplate (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  /*
    The scheduler maintains the state of the task and the system. Specify here the actions per state.
    Using the states (and the case-statements below) is optional.
    Task states INIT, RUN, STOP and DOWN are predefined.
    INIT is used during startup; when all tasks have initialized correctly the system state is set to RUN.
    Once the system is in this state the task can then switch to custom states. When the system stops all tasks are switched
    to the STOP-state. Remember to always check for incoming messages in custom states.
  */
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Give your variables an initial value and other work during initialization - YOUR CODE HERE.
                YourVariable1=11;
                YourVariable2=22;

                // For tasks that only respond to events (=messages) we can set the sleep attribute to ´true'.
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
                    case TISM_TEST: // Test packet, no action to take. Just enter a log entry.
                                    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_TEST message received from TaskID %d (HostID %d). No action taken.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                    break;
                    case TISM_PING: // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                    TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                    break;
 
                    // Other event types to process - YOUR CODE HERE.

                    default:        // Unknown message type - ignore.
                                    break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Other work to do in this state - YOUR CODE HERE.

				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // All activities to stop this task - YOUR CODE HERE.
                
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

