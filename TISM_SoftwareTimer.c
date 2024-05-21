/*

  TISM_SoftwareTimer.c - Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  
  The internal structures containing all data for TISM_SoftwareTimer to run.

*/

// Structure of the linear list containing all software timers.
struct TISM_SoftwareTimerEntry
{
  uint8_t TaskID, TimerID;
  bool RepetitiveTimer;
  uint32_t TimerIntervalMsec;
  uint64_t NextTimerEventUsec;
  struct TISM_SoftwareTimerEntry *NextEvent;
};


// All internal data for the TISM_SoftwareTimer task
struct TISM_SoftwareTimerData
{
  struct TISM_SoftwareTimerEntry *Entries;
  uint64_t FirstTimerEventUsec;
} TISM_SoftwareTimerData;


// Cancel a specific timer - erase the entry from the linear list.
void TISM_SoftwareTimerCancelTimer(uint8_t TaskID, uint8_t TimerID)
{
  struct TISM_SoftwareTimerEntry *SearchPointer=TISM_SoftwareTimerData.Entries, *PreviousSearchPointer=SearchPointer;
  while(SearchPointer!=NULL)
  {
    // Search for the timer with the specified Task and timer ID and remove it from the linear list.
    if(SearchPointer->TaskID==TaskID && SearchPointer->TimerID==TimerID)
    {
      // Remove this entry.
      // Is this the first record in the linear list?
      if(SearchPointer==TISM_SoftwareTimerData.Entries)
      {
        // First record in the list.
        TISM_SoftwareTimerData.Entries=TISM_SoftwareTimerData.Entries->NextEvent;
        free(SearchPointer);
        SearchPointer=TISM_SoftwareTimerData.Entries;
        PreviousSearchPointer=SearchPointer;
      }
      else
      {
        // Not the first record.
        PreviousSearchPointer->NextEvent=SearchPointer->NextEvent;
        free(SearchPointer);
        SearchPointer=PreviousSearchPointer->NextEvent;
      }
    }
    else
    {
      // Evaluate the next record
      PreviousSearchPointer=SearchPointer;
      SearchPointer=SearchPointer->NextEvent;
    }
  }
}


// Create a virtual software timer. No events, just calculate the value of future timer and return the value.
uint64_t TISM_SoftwareTimerSetVirtual(uint64_t TimerUsec)
{
  return(time_us_64()+TimerUsec);
}


// Check the status of the virtual software timer - is it expired? If so, return true.
bool TISM_SoftwareTimerVirtualExpired(uint64_t TimerUsec)
{
  return(time_us_64()>TimerUsec?true:false);
}


// Create a SoftwareTimerEntry struct and send a message to TISM_SoftwareTimer in order to register a new timer.
bool TISM_SoftwareTimerSet(TISM_Task ThisTask,uint8_t TimerID, bool RepetitiveTimer, uint32_t TimerIntervalMsec)
{
  struct TISM_SoftwareTimerEntry *EntryPointer=malloc(sizeof(struct TISM_SoftwareTimerEntry));
  EntryPointer->TaskID=ThisTask.TaskID;
  EntryPointer->TimerID=(uint8_t)TimerID;
  EntryPointer->RepetitiveTimer=RepetitiveTimer;
  EntryPointer->TimerIntervalMsec=TimerIntervalMsec;
  EntryPointer->NextTimerEventUsec=time_us_64()+(TimerIntervalMsec*1000);
  EntryPointer->NextEvent=NULL;
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_SoftwareTimerTaskID,TISM_SET_TIMER,(uint32_t)EntryPointer,0));
}


// Send a message to TISM_SoftwareTImerID to cancel the specified timer.
bool TISM_SoftwareTimerCancel(TISM_Task ThisTask, uint8_t TimerID)
{
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_SoftwareTimerTaskID,TISM_CANCEL_TIMER,(uint32_t)TimerID,0));
}


// This is the function that is registered in the TISM-system.
uint8_t TISM_SoftwareTimer (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) printf("%s: Run starting.\n", ThisTask.TaskName);
  
  switch(ThisTask.TaskState)   // Unknown states are ignored
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);
				        
                // Initialize variables
                TISM_SoftwareTimerData.Entries=NULL;
                TISM_SoftwareTimerData.FirstTimerEventUsec=0;

                // Go to sleep; we only wake after incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task %d doing work at %llu with priority %d on core %d.\n", ThisTask.TaskName, ThisTask.TaskID, time_us_64(), ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process these.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Message '%ld' type %d from TaskID %d (%s) received.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:         // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                            TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                            break;
                    case TISM_CANCEL_TIMER: // Cancel an existing timer
                                            if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: Cancellation received for software timer %d from task ID %d (%s).\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                            if(TISM_SoftwareTimerData.Entries!=NULL)
                                            {
                                              TISM_SoftwareTimerCancelTimer((uint8_t) MessageToProcess->SenderTaskID, (uint8_t) MessageToProcess->Message);

                                              if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: Software timer %d from task ID %d removed.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID);
                                            }
                                            else
                                            {
                                              // Cancel-timer message received, but list is empty.
                                              fprintf(STDERR, "%s: Warning - Cancellation received for Timer ID %D (Task ID %d, %s) but no timers registered. Ignoring.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->Specification,  System.Task[MessageToProcess->SenderTaskID].TaskName);
                                            }
                                            break;
                    case TISM_SET_TIMER:    // Set a new timer. Add the new entry at the beginning of the linear list. 
                                            // Warning - no checking for duplicate entries!
                                            if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: New software timer entry received from task ID %d (%s).\n", ThisTask.TaskName, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                            struct TISM_SoftwareTimerEntry *SearchPointer=(struct TISM_SoftwareTimerEntry *)MessageToProcess->Message;
                                            SearchPointer->NextEvent=TISM_SoftwareTimerData.Entries;
                                            TISM_SoftwareTimerData.Entries=SearchPointer;
                                            break;
                    default:                // Unknown message type - ignore.
                                            break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Work to do in this state.
                // Run through the list and check which timers expired and when the next timer expires. 
                // Set the WakeUpTimer accordingly. Then put the task back to sleep.
                if(TISM_SoftwareTimerData.Entries!=NULL)
                {
                  uint64_t RunTimestamp=time_us_64();
                  struct TISM_SoftwareTimerEntry *SearchPointer=TISM_SoftwareTimerData.Entries;
                  TISM_SoftwareTimerData.FirstTimerEventUsec=0xFFFFFFFFFFFFFFFF;
                  while(SearchPointer!=NULL)
                  {
                    // Check if the specific timer is expired.
                    if(SearchPointer->NextTimerEventUsec<RunTimestamp)
                    {
                      // Timer expired, send out notification. If it's not repetitive, remove the entry.
                      if(ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Timer %d expired for task %d, sending message.\n", ThisTask.TaskName, SearchPointer->TimerID, SearchPointer->TaskID);                      
                      
                      TISM_PostmanWriteMessage(ThisTask,SearchPointer->TaskID,SearchPointer->TimerID,0,0);
                      if(SearchPointer->RepetitiveTimer)
                      {
                        // Repetitive timer, reschedule.
                        SearchPointer->NextTimerEventUsec+=(SearchPointer->TimerIntervalMsec*1000);
                        if(SearchPointer->NextTimerEventUsec<TISM_SoftwareTimerData.FirstTimerEventUsec)
                          TISM_SoftwareTimerData.FirstTimerEventUsec=SearchPointer->NextTimerEventUsec;

                        if(ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Repetitive timer, rescheduled to %llu.\n", ThisTask.TaskName, SearchPointer->NextTimerEventUsec);
                      }
                      else
                      {
                        // Non-repetitive timer; delete it.
                        TISM_SoftwareTimerCancelTimer(SearchPointer->TaskID, SearchPointer->TimerID);

                        if(ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Non-repetitive timer, requested to be deleted.\n", ThisTask.TaskName);
                      }
                    }
                    else
                    {
                      // When is the next pending event? Search for the lowest (active) event timestamp.
                      if(SearchPointer->NextTimerEventUsec<TISM_SoftwareTimerData.FirstTimerEventUsec)
                        TISM_SoftwareTimerData.FirstTimerEventUsec=SearchPointer->NextTimerEventUsec;
                    }
                    SearchPointer=SearchPointer->NextEvent;
                  }
                  // Set the next wake-up timer according to the first (next) pending event.
                  System.Task[ThisTask.TaskID].TaskWakeUpTimer=TISM_SoftwareTimerData.FirstTimerEventUsec;

                  if(ThisTask.TaskDebug) 
                  {
                    SearchPointer=TISM_SoftwareTimerData.Entries;
                    fprintf(STDOUT, "%s: Software timer entries:\n", ThisTask.TaskName);
                    fprintf(STDOUT, "%s: =======================\n", ThisTask.TaskName);                                      
                    while(SearchPointer!=NULL)
                    {
                      fprintf(STDOUT, "%s: Task ID: %d (%s), Timer ID %d, %srepetitive, interval %d msec, next event %ld.\n", ThisTask.TaskName, SearchPointer->TaskID, System.Task[SearchPointer->TaskID].TaskName, SearchPointer->TimerID, (SearchPointer->RepetitiveTimer?"":"non-"), SearchPointer->TimerIntervalMsec, SearchPointer->NextTimerEventUsec);
                      SearchPointer=SearchPointer->NextEvent;
                    }
                    fprintf(STDOUT, "%s: List complete. Next event: %llu.\n", ThisTask.TaskName, TISM_SoftwareTimerData.FirstTimerEventUsec);
                  }
                }
                else
                {
                  // No entries - go to sleep.
                  TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);

                  if(ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: No timers set, returning to sleep.\n", ThisTask.TaskName);
                }
                break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Stopping.\n", ThisTask.TaskName);
		        
				        // Tasks for stopping.
			          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run completed.\n", ThisTask.TaskName);

  return (OK);
}

