/*
  TISM_Watchdoc.c
  ===============
  Task to check if other tasks are still alive. Generate warnings to STDthe EventLogger in case of timeouts.
  This task runs as a 'regular' task in the TISM-system.

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


// Internal data for this task
struct TISM_WatchdogData
{
  uint64_t TimeRequestSent[MAX_TASKS], ResponseDelay, NextPingRound;
  int DataRequestSent[MAX_TASKS];
  int PingMessageCounter;
} TISM_WatchdogData;


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
				        
                TISM_WatchdogData.PingMessageCounter=0;
                for(int counter=0; counter<MAX_TASKS; counter++)
                {
                  TISM_WatchdogData.TimeRequestSent[counter]=0;
                  TISM_WatchdogData.DataRequestSent[counter]=-1;
                }
                TISM_WatchdogData.PingMessageCounter=0;
                TISM_WatchdogData.NextPingRound=0;
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
             
                // First check for incoming messages.
                int MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (%s) received.", MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: // Reply to PING request.
                                    TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                    break;
                    case TISM_TEST: // This is mostly used for debugging purposes. Print a text to STDOUT.
                                    if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Test message received from TaskID %d (%s).", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);
                                    break;
                    case TISM_ECHO: // Echo reply to our ping request
                                    // Is this the reply to the last message we've sent to this task?
                                    if(MessageToProcess->Message==TISM_WatchdogData.DataRequestSent[MessageToProcess->SenderTaskID])
                                    {
                                      // Correct response received; calculate the delay. Did the response exceed the maximum?
                                      TISM_WatchdogData.ResponseDelay=time_us_64()-TISM_WatchdogData.TimeRequestSent[MessageToProcess->SenderTaskID];

                                      if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Valid ECHO response received from %d (%s), delay %ld.", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName, TISM_WatchdogData.ResponseDelay);

                                      if(TISM_WatchdogData.ResponseDelay>WATCHDOG_TASK_TIMEOUT)
                                      {
                                        // For now, only generate a warning.
                                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "ECHO response on PING request from %d (%s) exceeded maximum delay (%d).", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName, WATCHDOG_TASK_TIMEOUT);
                                      }
                                    }
                                    else
                                    {
                                      if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Invalid ECHO response received on PING request from %d (%s); expected %ld, received %ld.", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName, TISM_WatchdogData.DataRequestSent[MessageToProcess->SenderTaskID], MessageToProcess->Message);
                                    }
                                    break;
                    default:        // Unknown message type - ignore.
                                    break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Now it's time to send out PING requests to tasks. Did we woke up early (message received)?
                // If so, then wait - we don't want to flood the system.
                if(time_us_64()>=TISM_WatchdogData.NextPingRound)
                {
                  // Send out a PING request to all processes that do not sleep
                  for(MessageCounter=0;MessageCounter<System.NumberOfTasks;MessageCounter++)
                  {
                    if((!System.Task[MessageCounter].TaskSleeping) && (System.Task[MessageCounter].TaskID!=ThisTask.TaskID))
                    {
                      // Send the PING message; store the time of sending and message, so we can check when we get a reply.
                      TISM_PostmanWriteMessage(ThisTask,MessageCounter,TISM_PING,TISM_WatchdogData.PingMessageCounter,0);

                      if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Sent PING request to %d.", MessageCounter);
  
                      TISM_WatchdogData.TimeRequestSent[MessageCounter]=time_us_64();
                      TISM_WatchdogData.DataRequestSent[MessageCounter]=TISM_WatchdogData.PingMessageCounter;
                      TISM_WatchdogData.PingMessageCounter++;
                      if(TISM_WatchdogData.PingMessageCounter>=WATCHDOG_MAX_COUNTER)
                        TISM_WatchdogData.PingMessageCounter=0;
                    }
                  }
                  // Now calculate when we want to start the next round.
                  TISM_WatchdogData.NextPingRound=time_us_64()+WATCHDOG_CHECK_INTERVAL;
                }
				        break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}
