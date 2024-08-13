
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
bool TISM_EventLoggerLogEvent (TISM_Task ThisTask, uint8_t LogEntryType, const char *format, ...)
{
  char *ReturnBuffer=malloc(sizeof(char)*EVENT_LOG_ENTRY_LENGTH);
  va_list args;
  va_start(args,format);
  va_end(args);
  vsnprintf(ReturnBuffer,EVENT_LOG_ENTRY_LENGTH,format,args);
  return(TISM_CircularBufferWriteWithTimestamp(ThisTask.OutboundMessageQueue, ThisTask.TaskID, System.TISM_EventLoggerTaskID, LogEntryType, (uint32_t)ReturnBuffer, 0, time_us_64()));
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
                fprintf(STDOUT, "%llu %s (ID %d): Logging started.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);

                if (ThisTask.TaskDebug) fprintf(STDOUT, "%llu %s (ID %d): Initializing with priority %d.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);
				        
                // As the EventLogger only responds to events, go to sleep.
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%llu %s (ID %d): Doing work with priority %d on core %d.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%llu %s (ID %d): Message '%ld' type %d from TaskID %d (%s) received.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:             // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                                TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                                break;
                    case TISM_LOG_EVENT_NOTIFY: // Log event message; write the message to STDOUT.
                                                fprintf(STDOUT, "%llu %s (ID %d): %s\n", MessageToProcess->MessageTimestamp, System.Task[MessageToProcess->SenderTaskID].TaskName, MessageToProcess->SenderTaskID, (char *)MessageToProcess->Message);
                                                free((char *)MessageToProcess->Message); 
                                                break;
                    case TISM_LOG_EVENT_ERROR:  // Log error message; write the message to STDERR.
                                                fprintf(STDERR, "%llu %s (ID %d) ERROR: %s\n", MessageToProcess->MessageTimestamp, System.Task[MessageToProcess->SenderTaskID].TaskName, MessageToProcess->SenderTaskID, (char *)MessageToProcess->Message);
                                                free((char *)MessageToProcess->Message); 
                                                break;
                    default:                    // Unknown message type - ignore.
                    printf("FOUT: message type %d\n",MessageToProcess->MessageType);
                                                break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Logs handled; return to sleep.
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case STOP:  // Task required to stop this task, including the last log entry.
                fprintf(STDOUT, "%llu %s (ID %d): Logging stopped.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);

                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%llu %s (ID %d): Run completed.\n", time_us_64(), ThisTask.TaskName, ThisTask.TaskID);

  return (OK);
}

