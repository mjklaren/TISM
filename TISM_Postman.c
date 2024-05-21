/*

  TISM_Postman.c - Tools for managing the postboxes and delivery of messages between tasks.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "TISM.h"


// Wrapper for TISM_CircularBufferMessagesWaiting; allows tasks to easer check if a message is waiting in their inbound queue.
uint16_t TISM_PostmanMessagesWaiting(TISM_Task ThisTask)
{
  return(TISM_CircularBufferMessagesWaiting(ThisTask.InboundMessageQueue));
}


//  Wrapper for TISM_CircularBufferWrite; allows tasks to easier write messages to the outbound queue.
bool TISM_PostmanWriteMessage(TISM_Task ThisTask, uint8_t RecipientTaskID, uint8_t MessageType, uint32_t Message, uint32_t Specification)
{
  return(TISM_CircularBufferWrite(ThisTask.OutboundMessageQueue, ThisTask.TaskID, RecipientTaskID, MessageType, Message, Specification));  
}


// Wrapper for TISM_CircularBufferRead; allows tasks to easier read messages from the inbound queue.
struct TISM_Message *TISM_PostmanReadMessage(TISM_Task ThisTask)
{
  return(TISM_CircularBufferRead(ThisTask.InboundMessageQueue));
}


// Wrapper for TISM_CircularBufferDelete; allows tasks to easier delete the first message from their inbound queue.
void TISM_PostmanDeleteMessage(TISM_Task ThisTask)
{
  TISM_CircularBufferDelete(ThisTask.InboundMessageQueue);
}


/*
  
  The structure containing all data for TISM_Postman to run.

*/
struct TISM_PostmanData
{
  bool TaskReceivedMessage[MAX_TASKS];  
} TISM_PostmanData;


// The main task for the Postman of TISM. Handles the distribution of messages between tasks.
uint8_t TISM_Postman (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) printf("%s: Run starting.\n", ThisTask.TaskName);

  switch(ThisTask.TaskState)
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);

                // Empty the register we use to track which tasks we need to send a wake-up request for.
                for(uint8_t counter=0;counter<MAX_TASKS;counter++)
                  TISM_PostmanData.TaskReceivedMessage[counter]=false;
				        break;
	  case RUN:   // Do the work		
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task %d doing work at %llu with priority %d on core %d.\n", ThisTask.TaskName, ThisTask.TaskID, time_us_64(), ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
				
                // We put a limit on message processed in each run, to prevent tasks claiming all of the system.
                uint16_t MessageCounter=0;
                TISM_Message *MessageToProcess;	

                // First check for pending messages.
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Message '%ld' type %d from TaskID %d (%s) received.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

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

				        // Check the outbound queues for both cores for messages.
                for(uint8_t CoreCounter=0;CoreCounter<MAX_CORES;CoreCounter++)
                {
                  while((TISM_CircularBufferMessagesWaiting(&OutboundMessageQueue[CoreCounter])>0) && (MessageCounter<MAX_MESSAGES))
                  {
                    MessageToProcess=TISM_CircularBufferRead(&OutboundMessageQueue[CoreCounter]);

                    if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Processing message '%ld' from the queue of core %d type %d from TaskID %d (%s) to %d (%s).\n", ThisTask.TaskName, MessageToProcess->Message, CoreCounter, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName, MessageToProcess->RecipientTaskID, System.Task[MessageToProcess->RecipientTaskID].TaskName);

                    // Write this message to the inbound queue of SenderTaskID. Check validity of the recipient ID.
                    if((MessageToProcess->RecipientTaskID>=0) && 
                       (MessageToProcess->RecipientTaskID<System.NumberOfTasks) &&
                       (!TISM_CircularBufferWrite (&InboundMessageQueue[MessageToProcess->RecipientTaskID], MessageToProcess->SenderTaskID, MessageToProcess->RecipientTaskID, MessageToProcess->MessageType, MessageToProcess->Message, MessageToProcess->Specification)))
                    {
                      // Failure in delivery - buffer full? Give warning.
                      fprintf(STDERR, "%s: Warning - message '%ld' type %d from TaskID %d to %d could not be delivered.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->RecipientTaskID);
                    }
                    else
                    {
                      // Note that we need to ask TaskManager to wake the recipient.
                      // Further note, we do not have to ask TaskManager and IRQHandler to wake itself.
                      if(MessageToProcess->RecipientTaskID!=System.TISM_TaskManagerTaskID)
                        TISM_PostmanData.TaskReceivedMessage[MessageToProcess->RecipientTaskID]=true;
                    }
                   
                    // Processed the message; delete it.
                    TISM_CircularBufferDelete(&OutboundMessageQueue[CoreCounter]);
                    MessageCounter++;
                  }
                }

                // Now send messages to TaskManager to wake all processes who have received a message.
                for(uint8_t counter=0;counter<System.NumberOfTasks;counter++)
                {
                  if(TISM_PostmanData.TaskReceivedMessage[counter])
                  {
                    TISM_CircularBufferWrite(&InboundMessageQueue[System.TISM_TaskManagerTaskID],ThisTask.TaskID,System.TISM_TaskManagerTaskID,TISM_SET_TASK_SLEEP,false,counter); 
                    TISM_PostmanData.TaskReceivedMessage[counter]=false;
                  }
                }
                // Go to sleep; we only wake on incoming messages. 
                // We do it directly here to prevent circulair dependencies with TISM_TaskManager.
                System.Task[System.TISM_PostmanTaskID].TaskSleeping=true;
                // All done.				
				        break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Stopping.\n", ThisTask.TaskName);
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN. We do it directly here to prevent circulair dependencies with TISM_TaskManager.
                System.Task[System.TISM_PostmanTaskID].TaskState=DOWN;
		            break;
    default:    // All other states (e.g. SLEEP) are ignored/no action.
                break;					
  }	
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run completed.\n", ThisTask.TaskName);
  return (OK);
}
