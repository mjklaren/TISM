/*

  TISM_SoftwareTimer.c
  ====================
  Library for setting and triggering timers. This library defines 2 types of timers; virtual and software.
  Virtual timers are not 'real' timers but timer values are calculated and to be checked regularly for expiration.
  Software timers are registered and a message is sent to the requesting task once the timer has expired.
  As this is a software timer it isn´t very accurate; therefore timer values for software timers are specified in milliseconds.
  Despite the inaccuracy software timers are still very usefull for scheduling (repetitive) tasks.

  Warning: this library won't be included in compilation if "TISM_DISABLE_SCHEDULER" is enabled in TISM.h.
   
  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include <TISM.h>


#ifndef TISM_DISABLE_SCHEDULER   // In case the scheduler is disabled in TISM.h, to prevent compilation errors.


/*
  
  The internal structures containing all data for TISM_SoftwareTimer to run.

*/

// Structure of the linear list containing all software timers.
struct TISM_SoftwareTimerEntry
{
  uint8_t HostID, TaskID, TimerID;
  bool RepetitiveTimer;
  uint32_t TimerIntervalMsec, SequenceNr, Parameter;
  uint64_t NextTimerEventUsec;
  struct TISM_SoftwareTimerEntry *NextEvent;
};


// Static variables needed for this task to run.
static struct TISM_SoftwareTimerEntry *Entries;
static uint32_t SequenceCounter;
static uint64_t FirstTimerEventUsec;


// Internal function - cancel a timer specified by TaskID and TimerID. This could remove multiple entries!
static void TISM_SoftwareTimerCancelTimer(uint8_t TaskID, uint8_t TimerID)
{
  struct TISM_SoftwareTimerEntry *SearchPointer=Entries, *PreviousSearchPointer=SearchPointer;
  while(SearchPointer!=NULL)
  {
    // Search for the timer with the specified Task and timer ID and remove it from the linear list.
    if(SearchPointer->TaskID==TaskID && SearchPointer->TimerID==TimerID)
    {
      // Remove this entry.
      // Is this the first record in the linear list?
      if(SearchPointer==Entries)
      {
        // First record in the list.
        Entries=Entries->NextEvent;
        free(SearchPointer);
        SearchPointer=Entries;
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
static void TISM_SoftwareTimerCancelTimerBySequenceNr(uint32_t SequenceNr)
{
  struct TISM_SoftwareTimerEntry *SearchPointer=Entries, *PreviousSearchPointer=SearchPointer;
  while(SearchPointer!=NULL)
  {
    // Search for the timer with the specified Task and timer ID and remove it from the linear list.
    if(SearchPointer->SequenceNr==SequenceNr)
    {
      // Remove this entry.
      // Is this the first record in the linear list?
      if(SearchPointer==Entries)
      {
        // First record in the list.
        Entries=Entries->NextEvent;
        free(SearchPointer);
        SearchPointer=Entries;
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
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'. When the timer
  expires, the software timer will sent a message to the requesting task of message type 'TimerID'.
  This function allows the HostID and TaskID to be specified, so wake-up messagens can be send to "external"
  processes (other TaskIDs or tasks running on other HostIDs).

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t HostID             - HostID the task that needs to be notified is running on.
  uint8_t TaskID             - TaskID of the task that needs to be notified.
  uint8_t TimerID            - The identifier for this specific timer (unique for this Task ID).
  uint32_t Parameter         - Parameter to be sent to the task (Payload1) when the timer expires.
  bool RepetitiveTimer       - Repetitive timer (true/false)
  uint32_t TimerIntervalMsec - Time in milliseconds when timer should expire (measured from 'NOW').

  Return value:
  0                          - Failure to set timer (e.g. memory allocation failure)
  uint32_t SequenceNr        - Sequence number for this entry

  When the timer expires, TISM_SoftwareTimer will send a message to the specified HostID/TaskID with the following data:
  - MessageType = <TimerID> specified by user
  - Payload1    = <SequenceNr> assigned by TISM_SoftwareTimer
  - Payload2    = <Parameter> specified by user
*/
uint32_t TISM_SoftwareTimerSetExternal(TISM_Task ThisTask, uint8_t HostID, uint8_t TaskID, uint8_t TimerID, uint32_t Parameter, bool RepetitiveTimer, uint32_t TimerIntervalMsec)
{
  struct TISM_SoftwareTimerEntry *EntryPointer=malloc(sizeof(struct TISM_SoftwareTimerEntry));
  if(EntryPointer!=NULL)
  {
    SequenceCounter++; 
    if(SequenceCounter==0) 
      SequenceCounter=1;                                                                      // Skip zero, this is our error code.
    EntryPointer->HostID=(HostID==0?System.HostID:HostID);                                    // To prevent HostID is zero, unless in address-less mode.      
    EntryPointer->TaskID=((TaskID==0 || TaskID>=System.NumberOfTasks)?ThisTask.TaskID:TaskID); // To prevent that TaskID is zero/invalid.
    EntryPointer->SequenceNr=SequenceCounter;
    EntryPointer->TimerID=TimerID;
    EntryPointer->Parameter=Parameter;
    EntryPointer->RepetitiveTimer=RepetitiveTimer;
    EntryPointer->TimerIntervalMsec=TimerIntervalMsec;
    EntryPointer->NextTimerEventUsec=time_us_64()+(TimerIntervalMsec*1000);
    EntryPointer->NextEvent=NULL;
    if(!TISM_PostmanTaskWriteMessage(ThisTask, System.HostID, System.TISM_SoftwareTimerTaskID, TISM_SET_TIMER, (uint32_t)EntryPointer, 0))
    {
      free(EntryPointer);
      SequenceCounter--;  // Roll back sequence counter as this timer was not succesfully registered.
      return(0);  // Return 0 as sequence number to indicate failure.
    }
  }
  else
  {
    // Memory allocation failed
    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Failed to allocate memory for new software timer (TimerID %d) for TaskID %d.", TimerID, ThisTask.TaskID);
    return(0);  // Return 0 as sequence number to indicate failure.
  }
  return(SequenceCounter);
}


/*
  Description
  Create a SoftwareTimerEntry struct and send a message to TISM_SoftwareTimer in order to register a new timer.
  As the RP2040 doesn´t have a realtime clock the specified time is measured in usec from 'NOW'. When the timer
  expires, the software timer will sent a message to the requesting task of message type 'TimerID'.
  Note: we're reusing TISM_SoftwareTimerSetExternal here.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t TimerID            - The identifier for this specific timer (unique for this Task ID).
  uint32_t Parameter         - Parameter to be sent to the task (Payload1) when the timer expires.
  bool RepetitiveTimer       - Repetitive timer (true/false)
  uint32_t TimerIntervalMsec - Time in milliseconds when timer should expire (measured from 'NOW').

  Return value:
  0                          - Failure to set timer (e.g. memory allocation failure)
  uint32_t SequenceNr        - Sequence number for this entry

  When the timer expires, TISM_SoftwareTimer will send a message to the calling task (specified by ThisTask) 
  with the following data:
  - MessageType = <TimerID> specified by user
  - Payload1    = <SequenceNr> assigned by TISM_SoftwareTimer
  - Payload2    = <Parameter> specified by user
*/
uint32_t TISM_SoftwareTimerSet(TISM_Task ThisTask, uint8_t TimerID, uint32_t Parameter, bool RepetitiveTimer, uint32_t TimerIntervalMsec)
{
 return(TISM_SoftwareTimerSetExternal(ThisTask, System.HostID, ThisTask.TaskID, TimerID, Parameter, RepetitiveTimer, TimerIntervalMsec));
}


/*
  Description:
  Modify the destination HostID for a specific timer.

  Parameters:
  uint32_t SequenceNr  - Sequence number for this timer.
  uint8_t TargetHostID - The new HostID.

  Return value:
  True  - Success delivering request
  False - Failure delivering request
*/
bool TISM_SoftwareTimerModifyHostID(TISM_Task ThisTask, uint32_t SequenceNr, uint8_t TargetHostID)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask, System.HostID, System.TISM_SoftwareTimerTaskID, TISM_MODIFY_TIMER_HOSTID, SequenceNr, TargetHostID));
}


/*
  Description:
  Modify the destination TaskID for a specific timer.

  Parameters:
  uint32_t SequenceNr  - Sequence number for this timer.
  uint8_t TargetTaskID - The new TaskID.

  Return value:
  True  - Success delivering request
  False - Failure delivering request
*/
bool TISM_SoftwareTimerModifyTaskID(TISM_Task ThisTask, uint32_t SequenceNr, uint8_t TargetTaskID)
{
  return(TISM_PostmanTaskWriteMessage(ThisTask, System.HostID, System.TISM_SoftwareTimerTaskID, TISM_MODIFY_TIMER_TASKID, SequenceNr, TargetTaskID));
}


/*
  Description
  Cancel a specific timer - erase the entry from the linear list.
  
  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information.
  uint8_t TimerID         - Unique ID of the timer to cancel.

  Return value:
  True  - Success delivering request
  False - Failure delivering request
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
                Entries=NULL;
                FirstTimerEventUsec=0;
                SequenceCounter=0;

                // Go to sleep; initially we only wake after incoming messages. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process these.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %s-0x%02X from TaskID %d (HostID %d) received.", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_TEST:                // Test packet, no action to take. Just enter a log entry.
                                                   TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_TEST message received from TaskID %d (HostID %d). No action taken.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                   break;                    
                    case TISM_PING:                // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                                   TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                                   break;
                    case TISM_MODIFY_TIMER_HOSTID: {
                                                     // Modify the target HostID of a timer entry.
                                                     if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "HostID modification received for software timer with Sequence Nr %d from TaskID %d (HostID %d).", MessageToProcess->Payload0, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                      
                                                     struct TISM_SoftwareTimerEntry *SearchPointer=Entries, *PreviousSearchPointer=SearchPointer;
                                                     while(SearchPointer!=NULL)
                                                     {
                                                       // Search for the timer with the specified Task and timer ID and remove it from the linear list.
                                                       if(SearchPointer->SequenceNr==MessageToProcess->Payload0)
                                                       {
                                                         // Entry found, update the HostID.
                                                         SearchPointer->HostID=(uint8_t)MessageToProcess->Payload1;

                                                         if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Entry with Sequence Nr %d found and updated. New HostID is %d.", MessageToProcess->Payload0, MessageToProcess->Payload1);
                                                         break; // Entry found; we're done.
                                                       }
                                                       else
                                                       {
                                                         // Evaluate the next record
                                                         PreviousSearchPointer=SearchPointer;
                                                         SearchPointer=SearchPointer->NextEvent;
                                                       }

                                                       if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Entry with Sequence number %d not found.", MessageToProcess->Payload0);
                                                     }
                                                   }
                                                   break;                                                  
                    case TISM_MODIFY_TIMER_TASKID: {
                                                     // Modify the target TaskID of a timer entry.
                                                     if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID modification received for software timer with Sequence Nr %d from TaskID %d (HostID %d).", MessageToProcess->Payload0, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                     
                                                     struct TISM_SoftwareTimerEntry *SearchPointer=Entries, *PreviousSearchPointer=SearchPointer;
                                                     while(SearchPointer!=NULL)
                                                     {
                                                       // Search for the timer with the specified Task and timer ID and remove it from the linear list.
                                                       if(SearchPointer->SequenceNr==MessageToProcess->Payload0)
                                                       {
                                                         // Entry found, update the HostID.
                                                         SearchPointer->TaskID=(uint8_t)MessageToProcess->Payload1;

                                                         if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Entry with Sequence Nr %d found and updated. New TaskID is %d.", MessageToProcess->Payload0, MessageToProcess->Payload1);
                                                         break; // Entry found; we're done.
                                                       }
                                                       else
                                                       {
                                                         // Evaluate the next record
                                                         PreviousSearchPointer=SearchPointer;
                                                         SearchPointer=SearchPointer->NextEvent;
                                                       }

                                                       if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Entry with Sequence number %d not found.", MessageToProcess->Payload0);
                                                     } 
                                                   }                   
                                                   break;
                    case TISM_CANCEL_TIMER:        // Cancel an existing timer
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Cancellation received for software timer %d from TaskID %d (HostID %d).", MessageToProcess->Payload0, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                                   if(Entries!=NULL)
                                                   {
                                                     TISM_SoftwareTimerCancelTimer(MessageToProcess->SenderTaskID, MessageToProcess->Payload0);

                                                     if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer %d from TaskID %d removed.", MessageToProcess->Payload0, MessageToProcess->SenderTaskID);
                                                   }
                                                   else
                                                   {
                                                     // Cancel-timer message received, but list is empty.
                                                     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Cancellation received for Timer ID %D (TaskID %d, HostID %d) but no timers registered. Ignoring.", MessageToProcess->Payload0, MessageToProcess->Payload1,  MessageToProcess->SenderHostID);
                                                   }
                                                   break;
                    case TISM_CANCEL_TIMER_BY_NR:  // Cancel an existing timer, specified by sequence number.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Cancellation received for software timer with Sequence Nr %d, sent by TaskID %d (HostID %d).", MessageToProcess->Payload0, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                                   if(Entries!=NULL)
                                                   {
                                                     TISM_SoftwareTimerCancelTimerBySequenceNr(MessageToProcess->Payload0);

                                                     if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer with Sequence Nr %d removed.", MessageToProcess->Payload0);
                                                   }
                                                   else
                                                   {
                                                     // Cancel-timer message received, but list is empty.
                                                     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Cancellation received for Sequence Nr %D (TaskID %d, HostID %d) but no timers registered. Ignoring.", MessageToProcess->Payload0, MessageToProcess->Payload1,  MessageToProcess->SenderHostID);
                                                   }
                                                   break;                                             
                    case TISM_SET_TIMER:           // Set a new timer. Add the new entry at the beginning of the linear list. 
                                                   // Warning - no checking for duplicate entries!
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "New software timer entry received from task ID %d (HostID %d).", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                                                   struct TISM_SoftwareTimerEntry *SearchPointer=(struct TISM_SoftwareTimerEntry *)MessageToProcess->Payload0;
                                                   SearchPointer->NextEvent=Entries;
                                                   Entries=SearchPointer;
                                                   break;
                    case TISM_DISPLAY_TIMERS:      // Display active timers in the event log.
                                                   struct TISM_SoftwareTimerEntry *LogSearchPointer=Entries;
                                                   TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer entries:");
                                                   TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "=======================");                                      
                                                   while(LogSearchPointer!=NULL)
                                                   {
                                                     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d, Timer ID %d, Sequence Nr %d, %srepetitive, interval %d msec, next event %llu.", LogSearchPointer->TaskID, LogSearchPointer->TimerID, LogSearchPointer->SequenceNr, (LogSearchPointer->RepetitiveTimer?"":"non-"), LogSearchPointer->TimerIntervalMsec, LogSearchPointer->NextTimerEventUsec);
                                                     LogSearchPointer=LogSearchPointer->NextEvent;
                                                   }
                                                   TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "List complete. Next event: %llu.", FirstTimerEventUsec);
                                                   break;
                    default:                       // Unknown message type - ignore.
                                                   break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Work to do in this state.
                // Run through the list and check which timers expired and when the next timer expires. 
                // Set the WakeUpTimer accordingly. 
                if(Entries!=NULL)
                {
                  uint64_t RunTimestamp=time_us_64();
                  struct TISM_SoftwareTimerEntry *SearchPointer=Entries;
                  FirstTimerEventUsec=0xFFFFFFFFFFFFFFFF;
                  while(SearchPointer!=NULL)
                  {
                    // Check if the specific timer is expired.
                    if(SearchPointer->NextTimerEventUsec<RunTimestamp)
                    {
                      // Timer expired, send out notification. If it's not repetitive, remove the entry.
                      if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Timer with Sequence Nr %d expired for TaskID %d, sending message.", SearchPointer->SequenceNr, SearchPointer->TaskID);                      
                      
                      /*
                        When the timer expires, TISM_SoftwareTimer will send a message to the specified task with the following data:
                        - MessageType = <TimerID> specified by user
                        - Payload1    = <SequenceNr> assigned by TISM_SoftwareTimer
                        - Payload2    = <Parameter> specified by user
                      */
                      TISM_PostmanTaskWriteMessage(ThisTask, SearchPointer->HostID, SearchPointer->TaskID, SearchPointer->TimerID, SearchPointer->SequenceNr, SearchPointer->Parameter);
                      if(SearchPointer->RepetitiveTimer)
                      {
                        // Repetitive timer, reschedule for the next possible time in the future
                        do 
                          SearchPointer->NextTimerEventUsec+=(SearchPointer->TimerIntervalMsec*1000);
                        while(SearchPointer->NextTimerEventUsec<RunTimestamp);  
                        if(SearchPointer->NextTimerEventUsec<FirstTimerEventUsec)
                          FirstTimerEventUsec=SearchPointer->NextTimerEventUsec;

                        if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Repetitive timer, rescheduled to %llu.", SearchPointer->NextTimerEventUsec);
                      }
                      else
                      {
                        // Non-repetitive timer; delete it.
                        struct TISM_SoftwareTimerEntry *NextPointer=SearchPointer->NextEvent;
                        TISM_SoftwareTimerCancelTimerBySequenceNr(SearchPointer->SequenceNr);
                        SearchPointer=NextPointer;

                        if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Non-repetitive timer deleted.");

                        continue; // Skip the rest of the loop and evaluate the next entry, as the current entry is deleted.
                      }
                    }
                    else
                    {
                      // When is the next pending event? Search for the lowest (active) event timestamp.
                      if(SearchPointer->NextTimerEventUsec<FirstTimerEventUsec)
                        FirstTimerEventUsec=SearchPointer->NextTimerEventUsec;
                    }
                    SearchPointer=SearchPointer->NextEvent;
                  }
                  // Set the next wake-up timer according to the first (next) pending event.
                  TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_WAKEUPTIME, FirstTimerEventUsec);

                  if(ThisTask.TaskDebug) 
                  {
                    SearchPointer=Entries;
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Software timer entries:");
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "=======================");                                      
                    while(SearchPointer!=NULL)
                    {
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d, Timer ID %d, Sequence Nr %d, %srepetitive, interval %d msec, next event %llu.", SearchPointer->TaskID, SearchPointer->TimerID, SearchPointer->SequenceNr, (SearchPointer->RepetitiveTimer?"":"non-"), SearchPointer->TimerIntervalMsec, SearchPointer->NextTimerEventUsec);
                      SearchPointer=SearchPointer->NextEvent;
                    }
                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "List complete. Next event: %llu.", FirstTimerEventUsec);
                  }
                }

                // All done, wake up time is set according to the next pending timer event.
                //TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_SLEEP, true);
                break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping.

                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}


#endif