/*

  TISM_SoftwareTimer.c
  ====================
  Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.
  Virtual timers are not 'real' timers but timer values are calculated and to be checked regularly for expiration.
  Software timers are registered and a message is sent to the requesting task once the timer has expired.
  As this is a software timer it isn´t very accurate; therefore timer values for software timers are specified in milliseconds.
  Despite the inaccuracy software timers are still very usefull for scheduling (repetitive) tasks.

  Warning: this library won't compile if "TISM_DISABLE_SCHEDULER" is enabled in TISM.h.
   
  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "TISM.h"


#ifndef TISM_DISABLE_SCHEDULER   // In case the scheduler is disabled in TISM.h, to prevent compilation errors.


/*
  
  The internal structures containing all data for TISM_SoftwareTimer to run.

*/

// Structure of the linear list containing all software timers.
struct TISM_SoftwareTimerEntry
{
  uint8_t TaskID, TimerID;
  bool RepetitiveTimer;
  uint32_t TimerIntervalMsec, SequenceNr;
  uint64_t NextTimerEventUsec;
  struct TISM_SoftwareTimerEntry *NextEvent;
};


// All internal data for the TISM_SoftwareTimer task
struct TISM_SoftwareTimerData
{
  struct TISM_SoftwareTimerEntry *Entries;
  uint32_t SequenceCounter;
  uint64_t FirstTimerEventUsec;
} TISM_SoftwareTimerData;


// Internal function - cancel a timer specified by TaskID and TimerID. This could remove multiple entries!
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


// Internal function - cancel a timer specified by SequenceNr.
void TISM_SoftwareTimerCancelTimerBySequenceNr(uint32_t SequenceNr)
{
  struct TISM_SoftwareTimerEntry *SearchPointer=TISM_SoftwareTimerData.Entries, *PreviousSearchPointer=SearchPointer;
  while(SearchPointer!=NULL)
  {
    // Search for the timer with the specified Task and timer ID and remove it from the linear list.
    if(SearchPointer->SequenceNr==SequenceNr)
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
      return;  // Record found; we're done.
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
  uint64_t TimerUsec - Time in usec when timer should expire.

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
  uint8_t MessageLabel       - The MessageType (=label) that will be used when a message is sent
  uint8_t TimerID            - The identifier for this specific timer (unique for this Task ID).
  bool RepetitiveTimer       - Repetitive timer (true/false)
  uint32_t TimerIntervalMsec - Time in milliseconds when timer should expire (measured from 'NOW').

  Return value:
  uint32_t SequenceNr        - Sequence number for this entry
*/
uint32_t TISM_SoftwareTimerSet(TISM_Task ThisTask, uint8_t TimerID, bool RepetitiveTimer, uint32_t TimerIntervalMsec)
{
  struct TISM_SoftwareTimerEntry *EntryPointer=malloc(sizeof(struct TISM_SoftwareTimerEntry));
  TISM_SoftwareTimerData.SequenceCounter++;
  EntryPointer->TaskID=ThisTask.TaskID;
  EntryPointer->SequenceNr=TISM_SoftwareTimerData.SequenceCounter;
  EntryPointer->TimerID=TimerID;
  EntryPointer->RepetitiveTimer=RepetitiveTimer;
  EntryPointer->TimerIntervalMsec=TimerIntervalMsec;
  EntryPointer->NextTimerEventUsec=time_us_64()+(TimerIntervalMsec*1000);
  EntryPointer->NextEvent=NULL;
  TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_SoftwareTimerTaskID,TISM_SET_TIMER,(uint32_t)EntryPointer,0);
  return(TISM_SoftwareTimerData.SequenceCounter);
}


/*
  Description
  Cancel a specific timer - erase the entry from the linear list.
  
  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  uint8_t TimerID         - Unique ID of the timer to cancel.

  Return value:
  none
*/
bool TISM_SoftwareTimerCancel(TISM_Task ThisTask, uint8_t TimerID)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_SoftwareTimerTaskID,TISM_CANCEL_TIMER,(uint32_t)TimerID,0));
}


/*
  Description
  Cancel a specific timer specified by the sequence number - erase the entry from the linear list.
  
  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  uint32_t SequenceNr     - Sequence number for this timer.

  Return value:
  none
*/
bool TISM_SoftwareTimerCancelBySequenceNr(TISM_Task ThisTask, uint32_t SequenceNr)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_SoftwareTimerTaskID,TISM_CANCEL_TIMER_BY_NR,SequenceNr,0));
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
                TISM_SoftwareTimerData.SequenceCounter=0;

                // Go to sleep; we only wake after incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process these.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (HostID %d) received.", MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderTaskID);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:         // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                            TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                            break;
                    case TISM_CANCEL_TIMER: // Cancel an existing timer
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Cancellation received for software timer %d from TaskID %d (HostID %d).", MessageToProcess->Message, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                            if(TISM_SoftwareTimerData.Entries!=NULL)
                                            {
                                              TISM_SoftwareTimerCancelTimer(MessageToProcess->SenderTaskID, MessageToProcess->Message);

                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer %d from TaskID %d removed.", MessageToProcess->Message, MessageToProcess->SenderTaskID);
                                            }
                                            else
                                            {
                                              // Cancel-timer message received, but list is empty.
                                              TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Cancellation received for Timer ID %D (TaskID %d, HostID %d) but no timers registered. Ignoring.", MessageToProcess->Message, MessageToProcess->Specification,  MessageToProcess->SenderHostID);
                                            }
                                            break;
                    case TISM_CANCEL_TIMER_BY_NR: // Cancel an existing timer, specified by sequence number.
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Cancellation received for software timer with Sequence Nr %d, sent by TaskID %d (HostID %d).", MessageToProcess->Message, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                            if(TISM_SoftwareTimerData.Entries!=NULL)
                                            {
                                              TISM_SoftwareTimerCancelTimerBySequenceNr(MessageToProcess->Message);

                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer with Sequence Nr %d removed.", MessageToProcess->Message);
                                            }
                                            else
                                            {
                                              // Cancel-timer message received, but list is empty.
                                              TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Cancellation received for Sequence Nr %D (TaskID %d, HostID %d) but no timers registered. Ignoring.", MessageToProcess->Message, MessageToProcess->Specification,  MessageToProcess->SenderHostID);
                                            }
                                            break;                                             
                    case TISM_SET_TIMER:    // Set a new timer. Add the new entry at the beginning of the linear list. 
                                            // Warning - no checking for duplicate entries!
                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "New software timer entry received from task ID %d (HostID %d).", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                            struct TISM_SoftwareTimerEntry *SearchPointer=(struct TISM_SoftwareTimerEntry *)MessageToProcess->Message;
                                            SearchPointer->NextEvent=TISM_SoftwareTimerData.Entries;
                                            TISM_SoftwareTimerData.Entries=SearchPointer;
                                            break;
                    default:                // Unknown message type - ignore.
                                            break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
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
                      if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Timer with Sequence Nr %d expired for TaskID %d, sending message.", SearchPointer->SequenceNr, SearchPointer->TaskID);                      
                      
                      TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,SearchPointer->TaskID,SearchPointer->TimerID,SearchPointer->SequenceNr,0);
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
                        TISM_SoftwareTimerCancelTimerBySequenceNr(SearchPointer->SequenceNr);

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
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d, Timer ID %d, Sequence Nr %d, %srepetitive, interval %d msec, next event %ld.", SearchPointer->TaskID, SearchPointer->TimerID, SearchPointer->SequenceNr, (SearchPointer->RepetitiveTimer?"":"non-"), SearchPointer->TimerIntervalMsec, SearchPointer->NextTimerEventUsec);
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


#endif