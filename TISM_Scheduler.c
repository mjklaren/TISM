/*
  
  TISM_Scheduler.c
  ================
  The scheduler of the TISM-system (non-preemptive/cooperative multitasking).

  The scheduling process:
  - When each task is registered a priority is specified (PRIORITY_HIGH, PRIORITY_NORMAL and PRIORITY_LOW). This is
    actually a value describing the number of microseconds between each run of the specified task ("wake up timer").
  - The TaskIDs of all tasks are collected in a list; CORE0 cycles through the list from bottom to top, 
    CORE1 in the other direction, both via a separate instance of TISM_Scheduler.
  - When running through the list the priorities of tasks are considered via a cycle; first tasks with PRIORITY_HIGH
    are evaluated, then PRIORITY_NORMAL and higher, and last PRIORITY_LOW and higher. This means that tasks with
    PRIORITY_HIGH are executed more frequently and get the most CPU-time; PRIORITY_NORMAL a bit less etc.
  - For each "run through the list" the scheduler makes the following evaluations:
    - Are both cores (TISM_Scheduler instances) looking at the same TaskID ("collision")? If so, skip.
    - Is the task sleeping? If so, skip.
    - Is the wake-up timer for this task expired? If not, skip.
    If all these conditions do not apply the task is executed. When any other value than OK (0) is returned, the scheduler stops
    and generates a fatal error, stopping the system.
  - When a task has run succesfully the outbound messagequeue for the specific instance of TISM_Scheduler is checked. At the end
    of a run through the task list, if messages are waiting, TISM_Postman (delivery of messages) is started.
  - Last, check if any interrupts where received. If so then start TISM_IRQHandler, followed by TISM_Postman.

  As this is non-preemptive/cooperative multitasking, this mechanism only works if each task briefly executes and 
  then exits, freeing up time for other tasks to run.

  This library also contains functions to directly manipulate task properties and system states in a thread-safe manner, using critical sections.
  Thread safety is guaranteed by a single critical_section_t that disables interrupts on the calling core and acquires a hardware spinlock,
  preventing concurrent access from both the second core and any IRQ handler.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "TISM.h"



// Dummy Task struct so we can use the EventLogger.
static TISM_Task ThisTask;

// Guards TaskSleeping/State/Timer/Priority data
static critical_section_t TaskStateLock; 


/*
  Description:
  Check if the specified task is sleeping, in a thread-safe manner using a critical section.

  Parameters:
  uint8_t TaskID - ID of the task to check.
  Return value:
  true            - Task is sleeping.
  false           - Task is not sleeping or TaskID is invalid.
*/
bool TISM_SchedulerIsTaskSleeping(uint8_t TaskID)
{
  critical_section_enter_blocking(&TaskStateLock);
  bool ReturnValue=System.Task[TaskID].TaskSleeping;
  critical_section_exit(&TaskStateLock);
  return ReturnValue;
}


/*
  TISM_SchedulerSetTaskAttribute
 
  Directly set the specified attribute of TargetTaskID in a thread-safe manner using a critical section. Protection checks are performed inline,
  matching the original message-based validation rules.

  IMPORTANT compatibility note - TISM_DEDICATE_TO_TASK:
  In the original implementation, the task to dedicate to was passed via 'Setting' (Payload0). In this implementation it is taken from
  TargetTaskID, which is semantically correct. Update existing callers: 
   
      TISM_SchedulerSetTaskAttribute(ThisTask, TaskToDedicateTo, TISM_DEDICATE_TO_TASK, 0);

  Parameters
  TISM_Task  ThisTask          - Struct containing all task related info.
  uint8_t    TargetTaskID      - TaskID of the task to change.
  uint8_t    AttributeToChange - Attribute to change (see below).
  uint32_t   Setting           - New setting (see below).

  Return value
  ERR_TASK_NOT_FOUND           - Specified TargetTaskID is not valid.
  ERR_INVALID_OPERATION        - Non-system task trying to change a system task.
  OK                           - Success.

  AttributeToChange / Setting
  TISM_SET_TASK_STATE       Setting: DOWN, STOP, RUN, INIT or custom uint8_t
  TISM_SET_TASK_PRIORITY    Setting: PRIORITY_LOW / PRIORITY_NORMAL / PRIORITY_HIGH
  TISM_SET_TASK_SLEEP       Setting: true (sleep) or false (wake)
  TISM_SET_TASK_WAKEUP_TIME Setting: wake-up time in usec (absolute time, not relative)
  TISM_SET_TASK_DEBUG       Setting: DEBUG_NONE / DEBUG_NORMAL / DEBUG_HIGH
  TISM_WAKE_ALL_TASKS       Setting: 0  (TargetTaskID is ignored)
  TISM_DEDICATE_TO_TASK     Setting: 0  (task to dedicate to = TargetTaskID)
*/
uint8_t TISM_SchedulerSetTaskAttribute(TISM_Task ThisTask, uint8_t TargetTaskID, uint8_t AttributeToChange, uint64_t Setting)
{
  // TISM_WAKE_ALL_TASKS has no specific target; skip the ID check.
  if(AttributeToChange != TISM_WAKE_ALL_TASKS)
    if(!TISM_IsValidTaskID(TargetTaskID))
      return ERR_TASK_NOT_FOUND;

#ifndef TISM_DISABLE_PROTECTIONS
  // Only system tasks are allowed to change certain attributes of system tasks.
  if(TISM_IsSystemTask((uint8_t)TargetTaskID)      && 
      (AttributeToChange==TISM_SET_TASK_SLEEP       ||
       AttributeToChange==TISM_SET_TASK_WAKEUPTIME  ||
       AttributeToChange==TISM_SET_TASK_PRIORITY    ||
       AttributeToChange==TISM_DEDICATE_TO_TASK     ||
       AttributeToChange==TISM_SET_TASK_STATE       ||
       AttributeToChange==TISM_SET_TASK_DEBUG)      &&
      !TISM_IsSystemTask(ThisTask.TaskID))
  {
      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Attempt to change attribute %d of system task '%s' (ID %d) by non-system task '%s' (ID %d) is not allowed.", AttributeToChange, System.Task[TargetTaskID].TaskName, TargetTaskID, System.Task[ThisTask.TaskID].TaskName, ThisTask.TaskID);
      return ERR_INVALID_OPERATION;
  }
#endif

    if(ThisTask.TaskDebug==DEBUG_HIGH)
    {
      // Do not log events related to TISM_EventLogger itself to avoid infinite loops.
      if(TargetTaskID!=System.TISM_EventLoggerTaskID)
        TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Setting attribute %s (%d) for task ID %d '%s' to %ld.", TISM_MessageTypeToString(AttributeToChange), AttributeToChange, TargetTaskID, System.Task[TargetTaskID].TaskName, (long)Setting);
    }

    // Apply the change. Logging that reads state after the write is done outside the critical section to keep the lock duration minimal.
    switch (AttributeToChange)
    {
      case TISM_SET_TASK_SLEEP:       {        
                                        critical_section_enter_blocking(&TaskStateLock);
                                        if((bool)Setting==false)
                                        {
                                          // Wake the task only if it was actually sleeping.
                                          if(System.Task[TargetTaskID].TaskSleeping==true)
                                          {
                                              System.Task[TargetTaskID].TaskSleeping=false;
#ifndef TISM_DISABLE_SCHEDULER
                                              System.Task[TargetTaskID].TaskWakeUpTimer=time_us_64();
#endif
                                          }
                                        }
                                        else
                                        {
                                          // Go to sleep. Is a wake-up timer set in the past? If so, then set it to 0, so TISM_Scheduler knows there is no wake-up time.
                                          if(System.Task[TargetTaskID].TaskWakeUpTimer<time_us_64())
                                            System.Task[TargetTaskID].TaskWakeUpTimer=0;
                                          System.Task[TargetTaskID].TaskSleeping=true;
                                        }
                                        critical_section_exit(&TaskStateLock);
                                        break;
                                     }

#ifndef TISM_DISABLE_SCHEDULER
      case TISM_SET_TASK_WAKEUPTIME:  {
                                        critical_section_enter_blocking(&TaskStateLock);
                                        System.Task[TargetTaskID].TaskWakeUpTimer=Setting;
                                        critical_section_exit(&TaskStateLock);
                                        break;
                                      }
#endif

      case TISM_SET_TASK_STATE:       {
                                        critical_section_enter_blocking(&TaskStateLock);
                                        System.Task[TargetTaskID].TaskState=(uint8_t)Setting;
                                        critical_section_exit(&TaskStateLock);
                                        break;
                                      }
      case TISM_SET_TASK_PRIORITY:    {
                                        critical_section_enter_blocking(&TaskStateLock);
                                        System.Task[TargetTaskID].TaskPriority=(uint8_t)Setting;
                                        critical_section_exit(&TaskStateLock);
                                        break;
                                      }
      case TISM_SET_TASK_DEBUG:       {
                                        critical_section_enter_blocking(&TaskStateLock);
                                        System.Task[TargetTaskID].TaskDebug=(uint8_t)Setting;
                                        critical_section_exit(&TaskStateLock);
                                        break;
                                      }
      case TISM_WAKE_ALL_TASKS:       {
                                        critical_section_enter_blocking(&TaskStateLock);
                                        for (uint8_t TaskCounter=0; TaskCounter < System.NumberOfTasks; TaskCounter++)
                                        {
                                          if(System.Task[TaskCounter].TaskSleeping==true)
                                          {
#ifndef TISM_DISABLE_SCHEDULER
                                            System.Task[TaskCounter].TaskWakeUpTimer=time_us_64();
#endif
                                            System.Task[TaskCounter].TaskSleeping=false;
                                          }
                                        }
                                        critical_section_exit(&TaskStateLock);

                                        if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "All tasks have been woken up.");
                                        break;
                                      }

      case TISM_DEDICATE_TO_TASK:     {
                                        // Read and write are both inside the lock; logging happens after.
                                        bool TargetIsSleeping;
                                        critical_section_enter_blocking(&TaskStateLock);
                                        TargetIsSleeping=System.Task[TargetTaskID].TaskSleeping;
                                        if(!TargetIsSleeping)
                                        {
                                          for (uint8_t TaskIDCounter=0; TaskIDCounter<System.NumberOfTasks; TaskIDCounter++)
                                          {
                                            if(TaskIDCounter!=TargetTaskID && !TISM_IsSystemTask(TaskIDCounter))
                                              System.Task[TaskIDCounter].TaskSleeping=true;
                                          }
                                        }
                                        critical_section_exit(&TaskStateLock);
                                        if(!TargetIsSleeping)
                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - system now dedicated to task ID %d '%s'.", (int)TargetTaskID, System.Task[TargetTaskID].TaskName);
                                        else
                                          TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Task to dedicate to '%s', ID %d is sleeping. Aborting.", System.Task[TargetTaskID].TaskName, (int)TargetTaskID);
                                        break;
                                      }
      default:                        // Unknown attribute - ignore.
                                      break;
  }
  return OK;
}


/*
  Description
  Helper function; set the specified attribute for the requesting task itself by sending a message to TISM_TaskManager.
  Validity of the request is done by TISM_TaskManager; invalid requests are ignored.

  Parameters
  TISM_Task  ThisTask          - Struct containing all task related info.
  uint8_t    AttributeToChange - Attribute to change (see below).
  uint32_t   Setting           - New setting (see below).

  Return value
  ERR_TASK_NOT_FOUND           - Specified TargetTaskID is not valid.
  ERR_INVALID_OPERATION        - Non-system task trying to change a system task.
  OK                           - Success.

  AttributeToChange / Setting
  TISM_SET_TASK_STATE       Setting: DOWN, STOP, RUN, INIT or custom uint8_t
  TISM_SET_TASK_PRIORITY    Setting: PRIORITY_LOW / PRIORITY_NORMAL / PRIORITY_HIGH
  TISM_SET_TASK_SLEEP       Setting: true (sleep) or false (wake)
  TISM_SET_TASK_WAKEUPTIME  Setting: offset in usec, added to time_us_64()
  TISM_SET_TASK_DEBUG       Setting: DEBUG_NONE / DEBUG_NORMAL / DEBUG_HIGH
  TISM_WAKE_ALL_TASKS       Setting: 0  (TargetTaskID is ignored)
  TISM_DEDICATE_TO_TASK     Setting: 0  (task to dedicate to = TargetTaskID)
*/
uint8_t TISM_SchedulerSetMyTaskAttribute(TISM_Task ThisTask, uint8_t AttributeToChange, uint64_t Setting)
{
  return TISM_SchedulerSetTaskAttribute(ThisTask, ThisTask.TaskID, AttributeToChange, Setting);
}


/*
  Description
  Set the state of the entire TISM system. Any task can alter the system state.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t SystemState        - System state (see above)

  Return value:
  None
*/
void TISM_SchedulerSetSystemState(TISM_Task ThisTask, uint8_t SystemState)
{
    if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Setting system state to %d.", (int)SystemState);

    critical_section_enter_blocking(&TaskStateLock);
    System.State=(uint8_t)SystemState;
    critical_section_exit(&TaskStateLock);

    if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "System state changed to %d.", System.State);
}


/*
  Description
  Run the process pointed to by the RunPointer in the global System struct for the specified core. If the system state
  changes to anything else than "RUN" execution of the tasks is skipped. When not in single-core operation, use 
  CorePointers and a mutex to check which tasks are running (or about to) to prevent conflicts between cores.

  Parameters:
  int ThisCoreID          - ID of the current core we're trying to run a task for.
  bool Forced             - When 'true' run the task without cheching with the other core.
  
  Return value:
  ERR_RUNNING_TASK        - Task returned an error when executing.
  OK                      - Succes; task executed or already being executed by other core.
*/
uint8_t TISM_SchedulerRunTask(uint8_t ThisCoreID, bool Forced)
{
    // Sync the task struct fields that may have changed since last run.
    System.Task[System.RunPointer[ThisCoreID]].OutboundMessageQueue=System.OutboundMessageQueue[ThisCoreID];
    System.Task[System.RunPointer[ThisCoreID]].RunningOnCoreID=ThisCoreID;

#ifndef TISM_DISABLE_DUAL_CORE
    if(!Forced)
    {
      // Only one mutex acquire per task dispatch.
      // CorePointer reset after the run is done with a DMB + direct write; no second mutex needed because only this core writes its own slot.
      mutex_enter_blocking(&System.RunningTaskMutex);
      System.CorePointer[ThisCoreID]=System.RunPointer[ThisCoreID];
      if(System.CorePointer[CORE0]==System.CorePointer[CORE1])
      {
        // Collision: the other core is already running this task.
        System.CorePointer[ThisCoreID]=0;
        mutex_exit(&System.RunningTaskMutex);
        return OK;
      }
      mutex_exit(&System.RunningTaskMutex);
    }
#endif

#ifdef RUN_STEP_BY_STEP
    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "System state %d: starting task '%s', TaskID %d, task state %d on core %d.", System.State, System.Task[System.RunPointer[ThisCoreID]].TaskName, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskState, ThisCoreID);
    sleep_ms(RUN_STEP_BY_STEP_DELAY);
#endif

    // Execute the task.
    if(System.Task[System.RunPointer[ThisCoreID]].TaskFunction(System.Task[System.RunPointer[ThisCoreID]])!=OK)
      return ERR_RUNNING_TASK;

#ifdef RUN_STEP_BY_STEP
    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "System state %d: task '%s', TaskID %d on core %d, task state %d completed successfully.", System.State, System.Task[System.RunPointer[ThisCoreID]].TaskName, System.RunPointer[ThisCoreID], ThisCoreID, System.Task[System.RunPointer[ThisCoreID]].TaskState);
    sleep_ms(RUN_STEP_BY_STEP_DELAY);
#endif

#ifndef TISM_DISABLE_DUAL_CORE
    // Reset CorePointer without a second mutex acquire.
    // __dmb() ensures the write is visible to the other core before it next reads CorePointer under the mutex.
    __dmb();
    System.CorePointer[ThisCoreID]=0;
#endif

  return OK;
}


/*
  Description:
  The scheduler of TISM. Schedulers running on both cores run through the same task list; when wake-up timer is expired
  the task is started. The even core runs the queue ascending; uneven core descending.

  Parameters:
  int ThisCoreID          - ID of the current core we're trying to run tasks for.
  
  Return value:
  ERR_RUNNING_TASK        - One of the Tasks returned an error when executing.
  OK                      - System stopped without any errors.
*/
uint8_t TISM_Scheduler(uint8_t ThisCoreID)
{
  uint8_t PreviousRunPointer;

#ifndef TISM_DISABLE_PRIORITIES
  uint32_t RunPriority=PRIORITY_HIGH;
#endif

  while(System.State!=DOWN)
  {
    switch(System.State)
    {
      case INIT: { // Populate the dummy task struct for EventLogger use.
                    ThisTask.TaskID=0;
                    ThisTask.RunningOnCoreID=ThisCoreID;
                    ThisTask.TaskState=RUN;
                    ThisTask.TaskDebug=DEBUG_NONE;
                    ThisTask.TaskFunction=NULL;
                    ThisTask.TaskPriority=PRIORITY_NORMAL;
                    ThisTask.TaskSleeping=true;
                    sprintf(ThisTask.TaskName, "TISM_Scheduler %d", ThisCoreID);
                    ThisTask.InboundMessageQueue=NULL;
                    ThisTask.OutboundMessageQueue=System.OutboundMessageQueue[ThisCoreID];

#ifndef TISM_DISABLE_SCHEDULER
                    ThisTask.TaskWakeUpTimer=0;
#endif

                    // INIT is only executed on CORE0.
                    if(ThisCoreID==CORE0)
                    {
                      if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core 0: Initializing tasks.");

                      // Initialise the critical section that guards all task-state fields. Must happen before any task runs or any helper function (SetTaskAttribute etc.) is called.
                      critical_section_init(&TaskStateLock);

#ifndef TISM_DISABLE_DUAL_CORE
                      mutex_init(&System.RunningTaskMutex);
                      System.CorePointer[CORE0]=0;
                      System.CorePointer[CORE1]=0;
#endif

                      // Bring system tasks to sleep.
                      System.Task[System.TISM_PostmanTaskID].TaskSleeping=true;
                      System.Task[System.TISM_IRQHandlerTaskID].TaskSleeping=true;
                      System.Task[System.TISM_EventLoggerTaskID].TaskSleeping=true;

#ifndef TISM_DISABLE_UARTMX
                      System.Task[System.TISM_UartMXTaskID].TaskSleeping=true;
                      System.Task[System.TISM_NetworkManagerTaskID].TaskSleeping=true;
#endif

                      // Initialize each registered task (Task ID 0=scheduler).
                      for(uint8_t TaskCounter=1; TaskCounter<System.NumberOfTasks; TaskCounter++)
                      {
                        System.RunPointer[CORE0]=TaskCounter;
                        System.Task[TaskCounter].TaskState=INIT;
                        System.Task[TaskCounter].OutboundMessageQueue=System.OutboundMessageQueue[CORE0];

                        if(TISM_SchedulerRunTask(CORE0, true)!=OK)
                        {
                          System.State=STOP;
                          TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Core %d: Process '%s' failed to initialize correctly.", ThisCoreID, System.Task[TaskCounter].TaskName);
                        }
                        else
                        {
                          System.Task[TaskCounter].TaskState=RUN;

                          // Flush any messages the initialising task produced.
                          System.RunPointer[CORE0]=System.TISM_PostmanTaskID;
                          TISM_SchedulerRunTask(CORE0, true);
                          System.RunPointer[CORE0]=System.TISM_EventLoggerTaskID;
                          TISM_SchedulerRunTask(CORE0, true);
                        }
                      }

                      // All tasks initialised without error? Then schedule and transition to RUN.
                      if(System.State==INIT)
                      {
                        // Count tasks per priority level for staggered start.
                        uint8_t PriorityHigh=0, PriorityNormal=0, PriorityOther=0;
                        for(uint8_t counter=1; counter<System.NumberOfTasks; counter++)
                        {
                          switch(System.Task[counter].TaskPriority)
                          {
                            case PRIORITY_HIGH:   PriorityHigh++;   
                                                  break;
                            case PRIORITY_NORMAL: PriorityNormal++; 
                                                  break;
                            default:              PriorityOther++;  
                                                  break;
                          }
                        }

                        if(System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: %d prio-high, %d prio-normal and %d other tasks.", ThisCoreID, PriorityHigh, PriorityNormal, PriorityOther);

#ifndef TISM_DISABLE_SCHEDULER
                        // Calculate per-priority stagger offsets so no two tasks start at the exact same moment.
                        uint32_t PriorityHighOffset=PriorityHigh>0?(uint32_t)round(PRIORITY_HIGH/PriorityHigh):0;
                        uint32_t PriorityNormalOffset=PriorityNormal>0?(uint32_t)round(PRIORITY_NORMAL/PriorityNormal):0;
                        uint32_t PriorityOtherOffset=PriorityOther >0?(uint32_t)round(PRIORITY_LOW/PriorityOther):0;
                        uint64_t Start=time_us_64()+STARTUP_DELAY*1000;
                        uint8_t PriorityHighOffsetCounter=0, PriorityNormalOffsetCounter=0, PriorityOtherOffsetCounter=0;
                        for(uint8_t counter=1;counter<System.NumberOfTasks; counter++)
                        {
                          switch(System.Task[counter].TaskPriority)
                          {
                            case PRIORITY_HIGH:   System.Task[counter].TaskWakeUpTimer=Start + PriorityHighOffsetCounter * PriorityHighOffset;
                                                  PriorityHighOffsetCounter++;
                                                  break;
                            case PRIORITY_NORMAL: System.Task[counter].TaskWakeUpTimer=Start + (uint32_t)round(PriorityHighOffset/2) + PriorityNormalOffsetCounter * PriorityNormalOffset;
                                                  PriorityNormalOffsetCounter++;
                                                  break;
                            default:              System.Task[counter].TaskWakeUpTimer=Start + (uint32_t)round(PriorityNormalOffset/2) + PriorityOtherOffsetCounter * PriorityOtherOffset;
                                                  PriorityOtherOffsetCounter++;
                                                  break;
                          }

                          if(System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Task %d '%s' priority %d will start at %llu.", ThisCoreID, counter, System.Task[counter].TaskName, System.Task[counter].TaskPriority, System.Task[counter].TaskWakeUpTimer);
                        }
#endif

                        System.State=RUN;
                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: %d tasks initialized.", ThisCoreID, System.NumberOfTasks);

#ifdef SYSTEM_READY_PORT
                        gpio_put(SYSTEM_READY_PORT, 1);
                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Set SYSTEM_READY_PORT %d to high.", ThisCoreID, SYSTEM_READY_PORT);
#endif

                      }
                      else
                      {
                        // Initialisation failed; flush any remaining messages.
                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Core %d: System failed to initialize correctly.", ThisCoreID);

                        System.RunPointer[CORE0]=System.TISM_PostmanTaskID;
                        TISM_SchedulerRunTask(CORE0, true);
                        System.RunPointer[CORE0]=System.TISM_EventLoggerTaskID;
                        TISM_SchedulerRunTask(CORE0, true);
                      }
                    }
                    else
                    {
                      // CORE1: wait until CORE0 finishes initialization.
                      while(System.State==INIT)
                      {
                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Waiting....", ThisCoreID);
                        sleep_ms(500);
                      }
                    }

#ifndef TISM_DISABLE_PRIORITIES
                    RunPriority=PRIORITY_HIGH;
#endif

                    break;
                  } // case INIT
      case RUN: {
                  if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Starting run loop, %s through tasklist.", ThisCoreID, System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING? "ascending" : "descending");

                  // Cache traversal parameters locally. Only refreshed when NumberOfTasks changes (dynamic task registration).
                  uint8_t CachedNumberOfTasks=System.NumberOfTasks;
                  bool CachedDirAscending=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING);
                  uint8_t LastProcessInRun=CachedDirAscending ? CachedNumberOfTasks - 1 : 1;
                  uint64_t RunTimestamp;
                  bool ProcessMessages=false;
                  while(System.State==RUN)
                  {
                    // Refresh only on task-list growth.
                    if(System.NumberOfTasks!=CachedNumberOfTasks)
                    {
                      CachedNumberOfTasks=System.NumberOfTasks;
                      CachedDirAscending=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING);
                      LastProcessInRun=CachedDirAscending ? CachedNumberOfTasks - 1 : 1;
                    }

                    // Set RunPointer to the start of the traversal direction.
                    System.RunPointer[ThisCoreID]=CachedDirAscending ? 1 : CachedNumberOfTasks - 1;

                    // One timestamp per full pass; refreshed after each task that actually runs.
                    RunTimestamp=time_us_64();
                    ProcessMessages=false;
                    do
                    {
                      // Read all volatile scheduling fields under a single critical section. The wake-up timer update
                      // (after a task run) uses its own short lock below, keeping this read lock as brief as possible.
                      uint32_t TaskPriority;
                      uint64_t TaskWakeUpTimer;
                      bool TaskSleeping;

                      // Do we have a TaskWakeUpTimer in the past (not zero)? If so, then the task is ready to run. 
                      critical_section_enter_blocking(&TaskStateLock);
                      TaskPriority=System.Task[System.RunPointer[ThisCoreID]].TaskPriority;  
                      TaskWakeUpTimer=System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer;
                      if(TaskWakeUpTimer>0 && TaskWakeUpTimer<=RunTimestamp)
                        System.Task[System.RunPointer[ThisCoreID]].TaskSleeping=false;
                      TaskSleeping=System.Task[System.RunPointer[ThisCoreID]].TaskSleeping;
                      critical_section_exit(&TaskStateLock);

                      // Is this task eligible to run?
                      if(

#ifndef TISM_DISABLE_PRIORITIES
                          TaskPriority<=RunPriority &&
#endif

#ifndef TISM_DISABLE_SCHEDULER
                          TaskWakeUpTimer<=RunTimestamp &&
#endif

                          !TaskSleeping)
                      {
                          if(System.State==RUN && System.SystemDebug==DEBUG_HIGH)
                            TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Starting Task ID %d '%s'.", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName);

                          if(TISM_SchedulerRunTask(ThisCoreID, false)==OK)
                          {
                            if(System.State==RUN)
                            {
                              // Refresh timestamp only now that a task has actually consumed CPU time.
                              RunTimestamp=time_us_64();

                              // Flag messages for deferred Postman invocation at end of iteration.
                              if(TISM_PostmanMessagesWaiting(System.OutboundMessageQueue[ThisCoreID])>0)
                                ProcessMessages=true;

#ifndef TISM_DISABLE_SCHEDULER
                              // Update wake-up timer in a single, short critical section immediately after the task run while RunTimestamp is fresh.
                              critical_section_enter_blocking(&TaskStateLock);
                              while(System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer<= RunTimestamp)
                                System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer+=System.Task[System.RunPointer[ThisCoreID]].TaskPriority;
                              critical_section_exit(&TaskStateLock);
#endif

                              if(System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Task ID %d '%s' completed, next wakeup at %llu.", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName, System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer);
                            }
                          }
                          else
                          {
                            // Fatal task error — stop the system.
                            TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Core %d: task '%s' returned a fatal error. Stopping.", ThisCoreID, System.Task[System.RunPointer[ThisCoreID]].TaskName);
                            System.State=STOP;
                            break;
                          }
                      } 

                      // Check inbound circular buffers every iteration. Accumulate into ProcessMessages; Postman is invoked once at the bottom of the iteration.

#ifndef TISM_DISABLE_UARTMX
                      if(TISM_UartMXPacketsWaiting() || TISM_PostmanMessagesWaiting(System.Task[System.TISM_UartMXTaskID].InboundMessageQueue)>0)
                      {
                        PreviousRunPointer=System.RunPointer[ThisCoreID];
                        System.RunPointer[ThisCoreID]=System.TISM_UartMXTaskID;
                        TISM_SchedulerRunTask(ThisCoreID, false);
                        System.RunPointer[ThisCoreID]=PreviousRunPointer;
                        ProcessMessages=true;
                      }
#endif

                      if(TISM_PostmanMessagesWaiting(System.IRQHandlerInboundQueue)>0)
                      {
                        PreviousRunPointer=System.RunPointer[ThisCoreID];
                        System.RunPointer[ThisCoreID]=System.TISM_IRQHandlerTaskID;
                        TISM_SchedulerRunTask(ThisCoreID, false);
                        System.RunPointer[ThisCoreID]=PreviousRunPointer;
                        ProcessMessages=true;
                      }

                      // Single Postman invocation per iteration.
                      if(ProcessMessages)
                      {
                        PreviousRunPointer=System.RunPointer[ThisCoreID];
                        System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                        TISM_SchedulerRunTask(ThisCoreID, false);
                        System.RunPointer[ThisCoreID]=PreviousRunPointer;
                        ProcessMessages=false;
                      }

                      // Advance the RunPointer; uint8_t wraps to 255 on descending underflow, which terminates the do-while.
                      System.RunPointer[ThisCoreID]+=System.RunPointerDirection[ThisCoreID];
                    } 
                    while((CachedDirAscending?System.RunPointer[ThisCoreID]<=LastProcessInRun:System.RunPointer[ThisCoreID]>=LastProcessInRun) && System.State==RUN);

#ifndef TISM_DISABLE_PRIORITIES
                    // Rotate priority level for the next pass.
                    switch(RunPriority)
                    {
                        case PRIORITY_HIGH:   RunPriority=PRIORITY_NORMAL; 
                                              break;
                        case PRIORITY_NORMAL: RunPriority=PRIORITY_LOW;    
                                              break;
                        default:              RunPriority=PRIORITY_HIGH;   
                                              break;
                    }
#endif

                  } 

                  if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: run loop stopped, entering state %d.", ThisCoreID, System.State);

                  // Flush any remaining messages before leaving RUN state.
                  System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                  TISM_SchedulerRunTask(ThisCoreID, false);
                }
                break;
      case REBOOT: System.RebootAfterShutdown=true;
                   System.State=STOP;
                   // Fall through to STOP/default.
      default:    // STOP or illegal state.      
                  {
                    System.State=STOP; // Normalise any illegal state.
                    if(ThisCoreID==CORE0)
                    {

#ifdef SYSTEM_READY_PORT
                        gpio_put(SYSTEM_READY_PORT, 0);
                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Set SYSTEM_READY_PORT %d to low.", ThisCoreID, SYSTEM_READY_PORT);
#endif

                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Stopping after state %d.", ThisCoreID, System.State);

                        // Stop all user tasks first (descending order), then Postman and EventLogger last.
                        uint8_t ReturnValue;
                        for(uint8_t TaskCounter=System.NumberOfTasks-1; TaskCounter>=1; TaskCounter--)
                        {
                          if(TaskCounter!=System.TISM_EventLoggerTaskID && TaskCounter!=System.TISM_PostmanTaskID)
                          {
                            System.RunPointer[CORE0]=TaskCounter;
                            System.Task[TaskCounter].TaskState=STOP;
                            ReturnValue=TISM_SchedulerRunTask(CORE0, true);
                            if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: Task ID %d '%s' stopped with return value %d.", ThisCoreID, TaskCounter, System.Task[TaskCounter].TaskName, ReturnValue);
                          }
                        }

                        if(System.SystemDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Core %d: All tasks stopped, system going down.", ThisCoreID);

                        // Flush final log entries through Postman, then stop both.
                        System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                        TISM_SchedulerRunTask(CORE0, true);
                        System.Task[System.TISM_PostmanTaskID].TaskState=STOP;
                        TISM_SchedulerRunTask(CORE0, true);
                        System.RunPointer[ThisCoreID]=System.TISM_EventLoggerTaskID;
                        TISM_SchedulerRunTask(CORE0, true);
                        System.Task[System.TISM_EventLoggerTaskID].TaskState=STOP;
                        TISM_SchedulerRunTask(CORE0, true);
                        System.State=DOWN;
                    }
                    else
                    {
                        // CORE1: spin until CORE0 finishes shutdown.
                        System.RunPointer[ThisCoreID]=255;
                        while(System.State==STOP)
                            sleep_ms(500);
                    }
                  }
                  break; 
    } 
  } 

  if(System.SystemDebug) fprintf(STDOUT, "TISM: Core %d: All done.\n", ThisCoreID);
  sleep_ms(STARTUP_DELAY);
  return OK;
}
