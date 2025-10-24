
/*
  
  TISM_EventLogger.c
  ==================
  A uniform and thread-safe method for handling of log entries. This task runs as a 'regular' task in 
  the TISM-system. This task will be the only 'regular' task that writes to STDIN/STDOUT (TISM_Scheduler also writes to
  STDOUT/STDERR directly). Log entries are first stored in the outbound-messagequeue for the core, then sent to EventLogger
  for processing.
  As both cores run independently in some occasions the entries are not always logged in the correct order (when looking at 
  the timestamp) between both cores. Entries for each individual core are always logged correctly.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "TISM.h"


/*
  Description:
  Function to handle events to be logged in the outbound circular buffer, for handling by the EventLogger.

  Parameters:
  TISM_Task ThisTask      - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  uint8_t LogEntryType    - Type of event (notification or error; see TISM.h).
  const char *format, ... - Composition of a string with the event to handle (use the same formatting as with printf, eg "%s-%d").
  
  Return value:
  false                   - Event could not be delivered (unable to store in the outbound circular buffer).
  true                    - Event logged succesfully.
*/
bool TISM_EventLoggerLogEvent(TISM_Task ThisTask,uint8_t LogEntryType,const char *format, ...)
{
  char *ReturnBuffer=malloc(sizeof(char)*EVENT_LOG_ENTRY_LENGTH);
  if(ReturnBuffer==NULL)
    return(false);
  va_list args;
  va_start(args,format);
  vsnprintf(ReturnBuffer,EVENT_LOG_ENTRY_LENGTH,format,args);
  va_end(args);
  bool Result=TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue,System.HostID,ThisTask.TaskID,System.HostID,System.TISM_EventLoggerTaskID,LogEntryType,(uint32_t)ReturnBuffer,0,time_us_64());
  if(!Result)
    free(ReturnBuffer);  // Sending message failed; clean up memory.
  return(Result);
}


/*
  Description:
  The EventLogger of the TISM-system. The EventLogger is a 'regular task' called by TISM_Scheduler.


  Parameters:
  TISM_Task ThisTask      - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.                    - Run succesfully completed.
*/
uint8_t TISM_EventLogger (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%llu %s (ID %d): Run starting.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);
  
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                // Write the first log entry
                fprintf(STDOUT, "%llu %s (TaskID %d, HostID %d): Logging started.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, System.HostID);

                if (ThisTask.TaskDebug) fprintf(STDOUT, "%llu %s (TaskID %d, HostID %d): Initializing with priority %d.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, System.HostID, ThisTask.TaskPriority);
				        
                // EventLogger requires a larger incoming messagebuffer.
                if(!(TISM_PostmanBufferResize(System.Task[ThisTask.TaskID].InboundMessageQueue,EVENTLOGGER_MAX_MESSAGES,sizeof(TISM_Message))))
                  return(ERR_INITIALIZING);

                // As the EventLogger only responds to events, go to sleep.
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) 
                {
                  fprintf(STDOUT, "%llu %s (TaskID %d): Doing work with priority %d on core %d.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
                  fflush(STDOUT);
                }

                // First check for incoming messages and process them.
                uint16_t MessageCounter=0;    // Queue of eventlogger can be >255
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<EVENTLOGGER_MAX_MESSAGES))
                {        
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);
                  if (ThisTask.TaskDebug)
                  {
                    fprintf(STDOUT, "%llu %s (TaskID %d, HostID %d): Message '%ld' type %d from TaskID %d (HostID %d) received.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, System.HostID, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                    fflush(STDOUT);
                  }

                  // Process the message and delete it - but only if these messages come from this host. We cannot log for other hosts.
                  if(MessageToProcess->SenderHostID==System.HostID)
                  {
                    switch(MessageToProcess->MessageType)
                    {
                      case TISM_PING:             // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                                  TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                                  break;
                      case TISM_LOG_EVENT_NOTIFY: // Log event message; write the message to STDOUT.
                                                  fprintf(STDOUT, "%llu %s (TaskID %d, HostID %d): %s\n", MessageToProcess->MessageTimestamp, System.Task[MessageToProcess->SenderTaskID].TaskName, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, (char *)MessageToProcess->Message);
                                                  free((char *)MessageToProcess->Message);
                                                  fflush(STDOUT); 
                                                  break;
                      case TISM_LOG_EVENT_ERROR:  // Log error message; write the message to STDERR.
                                                  fprintf(STDERR, "%llu %s (TaskID %d, HostID %d) ERROR: %s\n", MessageToProcess->MessageTimestamp, System.Task[MessageToProcess->SenderTaskID].TaskName, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, (char *)MessageToProcess->Message);
                                                  free((char *)MessageToProcess->Message);
                                                  fflush(STDOUT); 
                                                  break;
                      default:                    // Unknown message type - ignore.
                                                  fprintf(STDERR, "%llu %s (Task ID %d, HostID %d) ERROR: Unknown message type %d received from TaskID %d (HostID %d). Ignoring.\n", time_us_64(), System.Task[ThisTask.TaskID].TaskName, ThisTask.TaskID, System.HostID, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                  break;
                    }
                  }
                  else
                  {
                    // Receiving messages from other hosts is nog allowed - log messages cannot contain a pointer with text.
                    fprintf(STDERR, "%llu %s (Task ID %d, HostID %d) ERROR: Message received from HostID %d. Ignoring.\n", time_us_64(), System.Task[ThisTask.TaskID].TaskName, System.HostID, MessageToProcess->SenderHostID);
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }
                
                // Logs handled; return to sleep.
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case STOP:  // Task required to stop this task, including the last log entry.
                fprintf(STDOUT, "%llu %s (ID %d): Logging stopped.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);
                fflush(STDOUT);

                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%llu %s (ID %d): Run completed.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);

  return (OK);
}

