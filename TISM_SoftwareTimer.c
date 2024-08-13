/*

  TISM_SoftwareTimer.c
  ====================
  Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.
  Virtual timers are not 'real' timers but timer values are calculated and to be checked regularly for expiration.
  Software timers are registered and a message is sent to the requesting task once the timer has expired.
  As this is a software timer it isn´t very accurate; therefore timer values for software timers are specified in milliseconds.
  Despite the inaccuracy software timers are still very usefull for scheduling (repetitive) tasks.

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


// Internal function - cancel a specific timer - erase the entry from the linear list.
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


/*
  Description
  Create a virtual software timer. No events, just calculate the value of future timer and return the value.
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'.
  
  Parameters:
  uint64_t TimerUsec - Time in usec when timer should expired.

  Return value:
  uint64_t - Timestamp; 'NOW' + when the timer should expire.
*/
uint64_t TISM_SoftwareTimerSetVirtual(uint64_t TimerUsec)
{
  return(time_us_64()+TimerUsec);
}


/*
  Description
  Check the status of the virtual software timer - is it expired? If so, return true.
  
  Parameters:
  uint64_t TimerUsec - Timer value ('NOW' + timer in usec).

  Return value:
  true  - Timer is expired.
  false - Timer hasn´t expired.
*/
bool TISM_SoftwareTimerVirtualExpired(uint64_t TimerUsec)
{
  return(time_us_64()>TimerUsec?true:false);
}


/*
  Description
  Create a SoftwareTimerEntry struct and send a message to TISM_SoftwareTimer in order to register a new timer.
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t TimerID            - The identifier for this specific timer (unique for this Task ID).
  bool RepetitiveTimer       - Repetitive timer (true/false)
  uint32_t TimerIntervalMsec - Time in milliseconds when timer should expired (measured from 'NOW').

  Return value:
  true                       - Timer set 
  false                      - Error setting the timer
*/
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


/*
  Description
  Cancel a specific timer - erase the entry from the linear list.
  
  Parameters:
  uint8_t TaskID  - TaskID for which the timer was set.
  uint8_t TimerID - ID of the timer to cancel

  Return value:
  none
*/
bool TISM_SoftwareTimerCancel(TISM_Task ThisTask, uint8_t TimerID)
{
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_SoftwareTimerTaskID,TISM_CANCEL_TIMER,(uint32_t)TimerID,0));
}


/*
  Description:
  This is the task that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  
  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_SoftwareTimer (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  switch(ThisTask.TaskState)   // Unknown states are ignored
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Initialize variables
                TISM_SoftwareTimerData.Entries=NULL;
                TISM_SoftwareTimerData.FirstTimerEventUsec=0;

                // Go to sleep; we only wake after incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process these.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (%s) received.", MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:         // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                            TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                            break;
                    case TISM_CANCEL_TIMER: // Cancel an existing timer
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Cancellation received for software timer %d from task ID %d (%s).", MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                            if(TISM_SoftwareTimerData.Entries!=NULL)
                                            {
                                              TISM_SoftwareTimerCancelTimer((uint8_t) MessageToProcess->SenderTaskID, (uint8_t) MessageToProcess->Message);

                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer %d from task ID %d removed.", MessageToProcess->Message, MessageToProcess->SenderTaskID);
                                            }
                                            else
                                            {
                                              // Cancel-timer message received, but list is empty.
                                              TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Cancellation received for Timer ID %D (Task ID %d, %s) but no timers registered. Ignoring.", MessageToProcess->Message, MessageToProcess->Specification,  System.Task[MessageToProcess->SenderTaskID].TaskName);
                                            }
                                            break;
                    case TISM_SET_TIMER:    // Set a new timer. Add the new entry at the beginning of the linear list. 
                                            // Warning - no checking for duplicate entries!
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "New software timer entry received from task ID %d (%s).", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

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
                      if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Timer %d expired for task %d, sending message.", SearchPointer->TimerID, SearchPointer->TaskID);                      
                      
                      TISM_PostmanWriteMessage(ThisTask,SearchPointer->TaskID,SearchPointer->TimerID,0,0);
                      if(SearchPointer->RepetitiveTimer)
                      {
                        // Repetitive timer, reschedule.
                        SearchPointer->NextTimerEventUsec+=(SearchPointer->TimerIntervalMsec*1000);
                        if(SearchPointer->NextTimerEventUsec<TISM_SoftwareTimerData.FirstTimerEventUsec)
                          TISM_SoftwareTimerData.FirstTimerEventUsec=SearchPointer->NextTimerEventUsec;

                        if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Repetitive timer, rescheduled to %llu.", SearchPointer->NextTimerEventUsec);
                      }
                      else
                      {
                        // Non-repetitive timer; delete it.
                        TISM_SoftwareTimerCancelTimer(SearchPointer->TaskID, SearchPointer->TimerID);

                        if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Non-repetitive timer, requested to be deleted.");
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
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer entries:");
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "=======================");                                      
                    while(SearchPointer!=NULL)
                    {
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Task ID: %d (%s), Timer ID %d, %srepetitive, interval %d msec, next event %ld.", SearchPointer->TaskID, System.Task[SearchPointer->TaskID].TaskName, SearchPointer->TimerID, (SearchPointer->RepetitiveTimer?"":"non-"), SearchPointer->TimerIntervalMsec, SearchPointer->NextTimerEventUsec);
                      SearchPointer=SearchPointer->NextEvent;
                    }
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "List complete. Next event: %llu.", TISM_SoftwareTimerData.FirstTimerEventUsec);
                  }
                }
                else
                {
                  // No entries - go to sleep.
                  TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);

                  if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "No timers set, returning to sleep.");
                }
                break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping.
			          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

