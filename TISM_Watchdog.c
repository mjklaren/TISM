/*
  
  TISM_Watchdoc.c
  ===============
  Task to check if other tasks are still alive. Generate warnings to STDthe EventLogger in case of timeouts.
  This task runs as a 'regular', low priority task in the TISM-system. Usefull for debugging.
  Can be enabled by removing "TISM_DISABLE_WATCHDOG" in TISM.h.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  
  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "TISM.h"


// Static variables needed for this task to run.
static uint64_t TimeRequestSent[MAX_TASKS], ResponseDelay, NextPingRound;
static int DataRequestSent[MAX_TASKS];
static int PingMessageCounter;


/*
  Description:
  This is the function that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run.
  
  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/	
uint8_t TISM_Watchdog (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");

  switch(ThisTask.TaskState)   // Unknown states are ignored
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                PingMessageCounter=0;
                for(int counter=0; counter<MAX_TASKS; counter++)
                {
                  TimeRequestSent[counter]=0;
                  DataRequestSent[counter]=-1;
                }
                PingMessageCounter=0;
                NextPingRound=0;
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
             
                // First check for incoming messages.
                int MessageCounter=0;
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
                    case TISM_PING: // Reply to PING request.
                                    TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                    break;
                    case TISM_ECHO: // Echo reply to our ping request
                                    // Is this the reply to the last message we've sent to this task?
                                    if(MessageToProcess->Payload0==DataRequestSent[MessageToProcess->SenderTaskID])
                                    {
                                      // Correct response received; calculate the delay. Did the response exceed the maximum?
                                      ResponseDelay=time_us_64()-TimeRequestSent[MessageToProcess->SenderTaskID];

                                      if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Valid ECHO response received from %d (HostID %d), delay %ld.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, ResponseDelay);

                                      if(ResponseDelay>WATCHDOG_TASK_TIMEOUT)
                                      {
                                        // For now, only generate a warning.
                                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "ECHO response on PING request from %d (HostID %d) exceeded maximum delay (%d).", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, WATCHDOG_TASK_TIMEOUT);
                                      }
                                    }
                                    else
                                    {
                                      if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Invalid ECHO response received on PING request from %d (HostID %d); expected %ld, received %ld.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, DataRequestSent[MessageToProcess->SenderTaskID], MessageToProcess->Payload0);
                                    }
                                    break;
                    default:        // Unknown message type - ignore.
                                    break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Now it's time to send out PING requests to tasks. Did we woke up early (message received)?
                // If so, then wait - we don't want to flood the system.
                if(time_us_64()>=NextPingRound)
                {
                  // Send out a PING request to all processes that do not sleep
                  for(MessageCounter=0;MessageCounter<System.NumberOfTasks;MessageCounter++)
                  {
                    if((!System.Task[MessageCounter].TaskSleeping) && (System.Task[MessageCounter].TaskID!=ThisTask.TaskID))
                    {
                      // Send the PING message; store the time of sending and message, so we can check when we get a reply.
                      TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,MessageCounter,TISM_PING,PingMessageCounter,0);

                      if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Sent PING request to %d.", MessageCounter);
  
                      TimeRequestSent[MessageCounter]=time_us_64();
                      DataRequestSent[MessageCounter]=PingMessageCounter;
                      PingMessageCounter++;
                      if(PingMessageCounter>=WATCHDOG_MAX_COUNTER)
                        PingMessageCounter=0;
                    }
                  }
                  // Now calculate when we want to start the next round.
                  NextPingRound=time_us_64()+WATCHDOG_CHECK_INTERVAL;
                }
				        break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}
