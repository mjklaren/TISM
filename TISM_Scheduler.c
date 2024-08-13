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
    and generates a fatal error.
  - When a task has run succesfully the outbound messagequeue for the specific instance of TISM_Scheduler is checked. If
    messages are waiting, TISM_Postman (delivery of messages) and TISM_Taskmanager (wake up tasks who have received 
    messages) are started.
  - Last, check if any interrupts where received. If so then start TISM_IRQHandler, followed by TISM_Postman 
    and TISM_Taskmanager.

  As this is non-preemptive/cooperative multitasking, this mechanism only works if each task briefly executes and 
  then exits, freeing up time for other tasks to run.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/


// Includes
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  Description
  Force the run of the process pointed to by the RunPointer in the global System struct for the specified core.
  No prior checks. To be used in single-core operation (INIT and STOP).

  Parameters:
  int ThisCoreID          - ID of the current core we're trying to run a task for.
  
  Return value:
  ERR_RUNNING_TASK        - Task returned an error when executing.
  OK                      - Succes; task executed or already executed by other core (='collision')
*/
uint8_t TISM_SchedulerRunTaskUnconditionally(uint8_t ThisCoreID)
{
   // Update the most relevant information in the task struct to be able to pass it as the parameter.
  System.Task[System.RunPointer[ThisCoreID]].OutboundMessageQueue=&(OutboundMessageQueue[ThisCoreID]);
  System.Task[System.RunPointer[ThisCoreID]].RunningOnCoreID=ThisCoreID;

  // Run the task the RunPointer is referring to.
  if((*System.Task[System.RunPointer[ThisCoreID]].TaskFunction)((System.Task[System.RunPointer[ThisCoreID]])))
    return(ERR_RUNNING_TASK);
  return(OK);
}


/*
  Description
  Run the process pointed to by the RunPointer in the global System struct for the specified core. If the system state
  changes to anything else than "RUN" execution of the tasks is skipped.

  Parameters:
  int ThisCoreID          - ID of the current core we're trying to run a task for.
  
  Return value:
  ERR_RUNNING_TASK        - Task returned an error when executing.
  OK                      - Succes; task executed or already executed by other core (='collision')
*/
uint8_t TISM_SchedulerRunTask(uint8_t ThisCoreID)
{
  // Update the most relevant information in the task struct to be able to pass it as the parameter.
  System.Task[System.RunPointer[ThisCoreID]].OutboundMessageQueue=&(OutboundMessageQueue[ThisCoreID]);
  System.Task[System.RunPointer[ThisCoreID]].RunningOnCoreID=ThisCoreID;

  // Check if the other core is running the same process; wait if this is the case. Add variable wait time to prevent lockups.
  uint8_t CurrentRunPointer=System.RunPointer[ThisCoreID];
  while(System.RunPointer[CORE0]==System.RunPointer[CORE1])
  {
    System.RunPointer[ThisCoreID]=255;
    busy_wait_us(5+(ThisCoreID*2));
    System.RunPointer[ThisCoreID]=CurrentRunPointer;
  }
  
  // Run the task the RunPointer is referring to. Check if we're still in RUN-state and the TaskWakeUpTimer hasn't changed in the meantime (task running on other core).
  if((System.State==RUN) && System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer<=time_us_64())
    if((*System.Task[System.RunPointer[ThisCoreID]].TaskFunction)((System.Task[System.RunPointer[ThisCoreID]])))
      return(ERR_RUNNING_TASK);
  return(OK);
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
  // The scheduler runs through 3 states; INIT, RUN and STOP.
  uint8_t PreviousRunPointer;
  uint32_t RunPriority;
  TISM_Task ThisTask;         // Dummy Task struct so we can use the EventLogger.
  while (System.State>DOWN)
  {
    switch(System.State)  // Unlike all other tasks; TISM_Scheduler states are determined by the system-state.
    {
        case INIT: // Init all tasks, stop everything in case of error; else go to RUN-state.
                   // Tasks might send messages, but we'll check these in RUN state.
                   // Populate the fields in the Task-struct so we can use it for the EventLogger.
                   ThisTask.TaskID=0;
                   ThisTask.RunningOnCoreID=ThisCoreID;
                   ThisTask.TaskState=RUN;
                   ThisTask.TaskDebug=DEBUG_NONE;
                   ThisTask.TaskFunction=NULL;
                   ThisTask.TaskPriority=PRIORITY_NORMAL;
                   ThisTask.TaskSleeping=true;
                   sprintf(ThisTask.TaskName, "TISM_Scheduler #%d", ThisCoreID);
                   ThisTask.InboundMessageQueue=NULL;
                   ThisTask.OutboundMessageQueue=&(OutboundMessageQueue[ThisCoreID]);
                   ThisTask.TaskWakeUpTimer=0; 

                   // We only run INIT on CORE0.                
                   if(ThisCoreID==CORE0)
                   {
                     if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #0 Initializing tasks.");

                     // Set the state of tasks to INIT and Let the tasks for this core initialize themselves.
                     for(uint8_t TaskCounter=1;TaskCounter<System.NumberOfTasks;TaskCounter++)       // Task ID 0 is the scheduler itself.
                     {
                       System.RunPointer[CORE0]=TaskCounter;
                       System.Task[TaskCounter].TaskState=INIT;
                       System.Task[TaskCounter].OutboundMessageQueue=&(OutboundMessageQueue[CORE0]);
                       if(TISM_SchedulerRunTaskUnconditionally(CORE0)!=OK)
                       {
                         // We've run into an error.
                         System.State=STOP;
                         TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Core #%d: Process %s failed to initialize correctly.", ThisCoreID, System.Task[TaskCounter].TaskName);
                       }
                       else
                       {
                         // Task initialized OK; set it's state to RUN.
                         System.Task[TaskCounter].TaskState=RUN;
                       }
                     }
                     
                     // Attempt to start Postmaster, Taskmanager and EventLogger to process any messages. Do not check for return values. 
                     System.RunPointer[CORE0]=System.TISM_PostmanTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);
                     System.RunPointer[CORE0]=System.TISM_TaskManagerTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);       
                     System.RunPointer[CORE0]=System.TISM_EventLoggerTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);

                     // No errors? Schedule the start of all tasks and move the system to RUN state.
                     if(System.State==INIT)
                     {
                       // Give all tasks an initial value for TaskWakeUpTimer; apply an offset so not everything starts at once.
                       // First count the number of PRIORITY_HIGH, PRIORITY_NORMAL and other tasks.
                       uint8_t PriorityHigh=0,PriorityNormal=0,PriorityOther=0;
                       for(uint8_t counter=1;counter<System.NumberOfTasks;counter++)     // Task ID 0 is the scheduler itself.
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
                      
                       if (System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: %d prio high, %d prio normal and %d other tasks.", ThisCoreID, PriorityHigh, PriorityNormal, PriorityOther);

                       // Calculate the offsite by dividing the available time by the number of tasks.
                       uint32_t PriorityHighOffset=(PriorityHigh>0?round(PRIORITY_HIGH/PriorityHigh):0);
                       uint32_t PriorityNormalOffset=(PriorityNormal>0?round(PRIORITY_NORMAL/PriorityNormal):0);
                       uint32_t PriorityOtherOffset=(PriorityOther>0?round(PRIORITY_LOW/PriorityOther):0);
                       uint64_t Start=time_us_64()+(STARTUP_DELAY*1000);
                       uint8_t PriorityHighOffsetCounter=0, PriorityNormalOffsetCounter=0, PriorityOtherOffsetCounter=0;

                       // Assign the offsets to the different tasks.
                       // To prevent the first tasks of all 3 priority-levels to start at the same moment we'll apply some shifting here as well.                       
                       for(uint8_t counter=1;counter<System.NumberOfTasks;counter++)     // Task ID 0 is the scheduler itself.
                       {
                         switch(System.Task[counter].TaskPriority)
                         {
                           case PRIORITY_HIGH:   System.Task[counter].TaskWakeUpTimer=Start+(PriorityHighOffsetCounter*PriorityHighOffset);
                                                 PriorityHighOffsetCounter++;
                                                 break;
                           case PRIORITY_NORMAL: System.Task[counter].TaskWakeUpTimer=Start+round(PriorityHighOffset/2)+(PriorityNormalOffsetCounter*PriorityNormalOffset);
                                                 PriorityNormalOffsetCounter++;
                                                 break;
                           default:              System.Task[counter].TaskWakeUpTimer=Start+round(PriorityNormalOffset/2)+(PriorityOtherOffsetCounter*PriorityOtherOffset);
                                                 PriorityOtherOffsetCounter++;
                                                 break;                            
                         }

                         if (System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Task %d (%s; priority %d) will start at %llu.", ThisCoreID, counter, System.Task[counter].TaskName, System.Task[counter].TaskPriority, System.Task[counter].TaskWakeUpTimer);
                       }
                       System.State=RUN;
                       if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: %d task(s) initialized.", ThisCoreID, System.NumberOfTasks);

                       // All tasks initialized and ready to go! Set the SYSTEM_READY_PORT to HIGH.
                       gpio_put(SYSTEM_READY_PORT, 1);

                       if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Set SYSTEM_READY_PORT %d to high.", ThisCoreID, SYSTEM_READY_PORT);
                     }
                     else  
                     {
                       // Failed to initialize correctly.
                       if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Core #%d: System failed to initalize correctly.", ThisCoreID);
                     }

                     // Attempt to start Postmaster, Taskmanager and EventLogger to process any messages. Do not check for return values. 
                     System.RunPointer[CORE0]=System.TISM_PostmanTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);
                     System.RunPointer[CORE0]=System.TISM_TaskManagerTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);       
                     System.RunPointer[CORE0]=System.TISM_EventLoggerTaskID;
                     TISM_SchedulerRunTaskUnconditionally(CORE0);
                   }
                   else
                   {
                     // We're on a different core; wait until system state has changed.
                     while(System.State==INIT)
                     {
                       if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Waiting....", ThisCoreID);
                       sleep_ms(500);
                     }
                   }
                   
                   // We start the first run with PRIORITY_HIGH tasks. 
                   RunPriority=PRIORITY_HIGH;
                   break;
        case RUN: // The actual loop that runs tasks.
                  if(System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Starting run loop, %s through tasklist.", ThisCoreID, (System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?"ascending":"descending"));

                  uint8_t LastProcessInRun, RunPointerBackup, ThisTaskID;
                  uint64_t RunTimestamp;
                  while (System.State==RUN)
                  {
                    // Run through all the tasks; check which need to start. 
                    // What is the criteria to stop a run? Do we ascend or descend? Where do we start in the queue?
                    // We check in each run in case tasks are created after initialization (and the queue grows).
                    
                    LastProcessInRun=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?(System.NumberOfTasks-1):1);                 // Task ID 0 is the scheduler itself.
                    System.RunPointer[ThisCoreID]=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?1:(System.NumberOfTasks-1));    // Task ID 0 is the scheduler itself.                    
                    do
                    {
                      // Are both cores looking at different processes to run?
                      // Is the tasks allowed to run, considering its priority?
                      // Is the task specified by RunPointer not sleeping and is the wake-up timer for this task expired?
                      RunTimestamp=time_us_64();   
                      if((System.RunPointer[CORE0]!=System.RunPointer[CORE1]) &&
                         (System.Task[System.RunPointer[ThisCoreID]].TaskPriority<=RunPriority) &&
                         (!System.Task[System.RunPointer[ThisCoreID]].TaskSleeping) &&
                         (System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer<=RunTimestamp))
                      { 
                        if(System.State==RUN && System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Starting Task ID %d (%s).", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName);
                                   
                        if(TISM_SchedulerRunTask(ThisCoreID)==OK)
                        {
                          // Is the system still in RUN-state?
                          if(System.State==RUN)
                          {
                            // Did the task generate any messages in the queue for this core? 
                            // If so, start the Postman and Taskmanager tasks (securely),
                            if(TISM_CircularBufferMessagesWaiting(&OutboundMessageQueue[ThisCoreID])>0)
                            {
                              // Messages waiting; start Postman and Taskmanager tasks. No checking for return values.
                              PreviousRunPointer=System.RunPointer[ThisCoreID];
                              System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                              TISM_SchedulerRunTask(ThisCoreID);
                              System.RunPointer[ThisCoreID]=System.TISM_TaskManagerTaskID;
                              TISM_SchedulerRunTask(ThisCoreID);

                              // Restore the RunPointer to it's original value so we can continue the cycle.
                              System.RunPointer[ThisCoreID]=PreviousRunPointer;
                            }

                            // Task ran succesfully; calculate the next wake-up time based on the task's priority, but only when needed.
                            // Also update the timestamp we're comparing with.
                            if(System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Task ID %d (%s) completed.", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName);
                          
                            // If the task hasn´t set a new value for TaskWakeUpTimer, set one based on the task's priority.
                            RunTimestamp=time_us_64();
                            while (System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer<RunTimestamp)
                            {
                              // Make sure the next WakeUpTimer-moment is beyond the current timestamp, in case we've missed earlier timeslots.
                              System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer+=System.Task[System.RunPointer[ThisCoreID]].TaskPriority;
                            }
                          
                            if(System.SystemDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Task ID %d (%s) wants to wake up at %llu (now running on core %d).", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName, System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer, ThisCoreID);
                          }
                        }
                        else
                        {
                          // Task execution returened an error. Stop the System.
                          TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Core #%d: task %s returned a fatal error. Stopping.", ThisCoreID, System.Task[System.RunPointer[ThisCoreID]].TaskName);
                          System.State=STOP;
                          break;
                        }
                      }

                      // Did we receive any interrupts? Check the inbound circular buffer and start the IRQ handler when needed.
                      if(TISM_CircularBufferMessagesWaiting(&IRQHandlerInboundQueue)>0)
                      {
                        // IRQ messages waiting; IRQ Hander, Postman and Taskmanager MUST run. No checking for return values.
                        PreviousRunPointer=System.RunPointer[ThisCoreID];
                        System.RunPointer[ThisCoreID]=System.TISM_IRQHandlerTaskID;
                        TISM_SchedulerRunTask(ThisCoreID);
                        System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                        TISM_SchedulerRunTask(ThisCoreID);
                        System.RunPointer[ThisCoreID]=System.TISM_TaskManagerTaskID;
                        TISM_SchedulerRunTask(ThisCoreID);

                        // Restore the RunPointer to it's original value so we can continue the cycle.
                        System.RunPointer[ThisCoreID]=PreviousRunPointer;
                      }
                      
                      // Move the RunPointer to the next in line. 
                      // Take care; RunPointer is uint8_t, so wraps to 255 when it descents to 0.
                      System.RunPointer[ThisCoreID]+=System.RunPointerDirection[ThisCoreID];
                    }
                    while ((System.RunPointerDirection[ThisCoreID]!=QUEUE_RUN_ASCENDING?(System.RunPointer[ThisCoreID]>=LastProcessInRun):(System.RunPointer[ThisCoreID]<=LastProcessInRun)) && (System.State==RUN));

                    // Run completed; set priority level for the next run.
                    switch(RunPriority)
                    {
                      case PRIORITY_HIGH   : RunPriority=PRIORITY_NORMAL;
                                             break;
                      case PRIORITY_NORMAL : RunPriority=PRIORITY_LOW;
                                             break;
                      default              : RunPriority=PRIORITY_HIGH;
                    }
                  }

                  // Loop has ended; most likely a state switch.
                  if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: run loop stopped, entering state %d.", ThisCoreID, System.State);

                  // Run Postman to make sure log entries are delivered to the EventLogger.
                  System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                  TISM_SchedulerRunTask(ThisCoreID);
                  break;
        default:  // STOP or illegal state. Execute all tasks once, requesting to stop.
                  System.State=STOP;  // Just to be sure.
                  
                  // We only run this on Core0.
                  if(ThisCoreID==CORE0)
                  {
                    // All tasks about to stop. Set the SYSTEM_READY_PORT to LOW.
                    gpio_put(SYSTEM_READY_PORT, 0);
                    
                    if (System.SystemDebug) 
                    {
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Set SYSTEM_READY_PORT %d to low.", ThisCoreID, SYSTEM_READY_PORT);
                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Stopping after state %d.", ThisCoreID, System.State);
                    }

                    // Stop all tasks; set each task state to STOP, run them once, give the opportunity to clean up.
                    // Do not check for return values, do not process any messages. Stop Postmand and EventLogger last.
                    uint8_t ReturnValue;
                    for(uint8_t TaskCounter=System.NumberOfTasks-1;TaskCounter>1;TaskCounter--)  // Task ID 0 is the scheduler itself.
                    {
                      if((TaskCounter!=System.TISM_EventLoggerTaskID) && (TaskCounter!=System.TISM_PostmanTaskID))
                      {
                        System.RunPointer[CORE0]=TaskCounter;
                        System.Task[TaskCounter].TaskState=STOP;
                        ReturnValue=TISM_SchedulerRunTaskUnconditionally(CORE0);

                        if (System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: Task ID %d (%s) stopped with return value %d.", ThisCoreID, TaskCounter, System.Task[TaskCounter].TaskName, ReturnValue);
                      }
                    }

                    if(System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Core #%d: All tasks stopped, system going down.", ThisCoreID);

                    // Run Postman and EventLogger to process last log entries, then tell it to stop.
                    System.RunPointer[ThisCoreID]=System.TISM_PostmanTaskID;
                    TISM_SchedulerRunTaskUnconditionally(CORE0);
                    System.Task[System.TISM_PostmanTaskID].TaskState=STOP;
                    TISM_SchedulerRunTaskUnconditionally(CORE0);
                    System.RunPointer[ThisCoreID]=System.TISM_EventLoggerTaskID;
                    TISM_SchedulerRunTaskUnconditionally(CORE0);
                    System.Task[System.TISM_EventLoggerTaskID].TaskState=STOP;
                    TISM_SchedulerRunTaskUnconditionally(CORE0);
                  }
                  else
                  {
                    // We're on a different core; wait until system state has changed.
                    // Set the RunPointer to the starting value.
                    System.RunPointer[ThisCoreID]=255;
                    while(System.State==STOP)
                      sleep_ms(500);
                  }
                  System.State=DOWN; 
                  break;
    }
  }
  
  //All done.
  if (System.SystemDebug) fprintf(STDOUT, "TISM: Core #%d: All done.", ThisCoreID);
  sleep_ms(STARTUP_DELAY);
  return(OK);
}

