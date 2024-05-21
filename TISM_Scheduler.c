/*

  TISM_Scheduler.c - The scheduler of the TISM-system (non-preemptive/cooperative multitasking).

*/


// Includes
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  Description
  Run the process pointed to by the RunPointer in the global System struct for the specified core.

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
  
  // Run the task the RunPointer is referring to.
  if((*System.Task[System.RunPointer[ThisCoreID]].TaskFunction)((System.Task[System.RunPointer[ThisCoreID]])))
  {
    // We've run into a fatal error. Stop.
    fprintf(STDERR, "TISM_Scheduler #%d: task %s returned a fatal error on core %d. Stopping.\n", ThisCoreID, System.Task[System.RunPointer[ThisCoreID]].TaskName, ThisCoreID);
    return(ERR_RUNNING_TASK);
  }
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
  while (System.State>DOWN)
  {
    switch(System.State)  // Unlike all other tasks; TISM_Scheduler states are determined by the system-state.
    {
        case INIT: // Init all tasks, stop everything in case of error; else go to RUN-state.
                   // Tasks might send messages, but we'll check these in RUN state.
                   // We only run INIT on CORE0.
                   if(ThisCoreID==CORE0)
                   {
                     if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Initializing tasks.\n", ThisCoreID);

                     // Set the state of tasks to INIT and Let the tasks for this core initialize themselves.
                     for(uint8_t TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
                     {
                       System.RunPointer[CORE0]=TaskCounter;
                       System.Task[TaskCounter].TaskState=INIT;
                       System.Task[TaskCounter].OutboundMessageQueue=&(OutboundMessageQueue[CORE0]);
                       if(TISM_SchedulerRunTask(CORE0)!=OK)
                       {
                         // We've run into an error.
                         System.State=STOP;
                         fprintf(STDERR, "TISM_Scheduler #%d: Process %s failed to initialize correctly.\n", ThisCoreID, System.Task[TaskCounter].TaskName);
                       }
                       else
                       {
                         // Task initialized OK; set it's state to RUN.
                         System.Task[TaskCounter].TaskState=RUN;
                       }
                     }

                     // Give all tasks an initial value for TaskWakeUpTimer; apply an offset so not everything starts at once.
                     // First count the number of PRIORITY_HIGH, PRIORITY_NORMAL and other tasks.
                     uint8_t PriorityHigh=0,PriorityNormal=0,PriorityOther=0;
                     for(uint8_t counter=0;counter<System.NumberOfTasks;counter++)
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
                      
                     if (System.SystemDebug==DEBUG_HIGH) fprintf(STDOUT, "TISM_Scheduler #%d: %d prio high, %d prio normal and %d other tasks.\n", ThisCoreID, PriorityHigh, PriorityNormal, PriorityOther);

                     // Calculate the offsite by dividing the available time by the number of tasks.
                     uint32_t PriorityHighOffset=(PriorityHigh>0?round(PRIORITY_HIGH/PriorityHigh):0);
                     uint32_t PriorityNormalOffset=(PriorityNormal>0?round(PRIORITY_NORMAL/PriorityNormal):0);
                     uint32_t PriorityOtherOffset=(PriorityOther>0?round(PRIORITY_LOW/PriorityOther):0);
                     uint64_t Start=time_us_64()+(STARTUP_DELAY*1000);
                     uint8_t PriorityHighOffsetCounter=0, PriorityNormalOffsetCounter=0, PriorityOtherOffsetCounter=0;

                     // Assign the offsets to the different tasks.
                     // To prevent the first tasks of all 3 priority-levels to start at the same moment we'll apply some shifting here as well.                       
                     for(uint8_t counter=0;counter<System.NumberOfTasks;counter++)
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

                       if (System.SystemDebug==DEBUG_HIGH) fprintf(STDOUT, "TISM_Scheduler #%d: Task %d (%s; priority %d) will start at %llu.\n", ThisCoreID, counter, System.Task[counter].TaskName, System.Task[counter].TaskPriority, System.Task[counter].TaskWakeUpTimer);
                     }

                     // No errors? Start Postmaster and Taskmanager to process any messages. Do not check for return values. 
                     // Move the system to RUN state.
                     if(System.State==INIT)
                     {
                       System.RunPointer[CORE0]=System.TISM_PostmanTaskID;
                       TISM_SchedulerRunTask(ThisCoreID);
                       System.RunPointer[CORE0]=System.TISM_TaskManagerTaskID;
                       TISM_SchedulerRunTask(ThisCoreID);       
                       System.State=RUN;

                       if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: %d task(s) initialized.\n", ThisCoreID, System.NumberOfTasks);

                       // All tasks initialized and ready to go! Set the SYSTEM_READY_PORT to HIGH.
                       gpio_put(SYSTEM_READY_PORT, 1);

                       if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Set SYSTEM_READY_PORT %d to high.\n", ThisCoreID, SYSTEM_READY_PORT);
                     }     
                   }
                   else
                   {
                     // We're on a different core; wait until system state has changed.
                     while(System.State==INIT)
                     {
                       if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Waiting....\n", ThisCoreID);
                       sleep_ms(500);
                     }
                   }

                   // We start the first run with PRIORITY_HIGH tasks. 
                   RunPriority=PRIORITY_HIGH;
                   break;
        case RUN: // The actual loop to run tasks
                  if(System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Starting run loop, %s through tasklist.\n", ThisCoreID, (System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?"ascending":"descending"));

                  uint8_t LastProcessInRun, RunPointerBackup, ThisTaskID;
                  uint64_t RunTimestamp;
                  while (System.State==RUN)
                  {
                    // Run through all the tasks; check which need to start. 
                    // What is the criteria to stop a run? Do we ascend or descend? Where do we start in the queue?
                    // We check in each run in case tasks are created after initialization (and the queue grows).
                    LastProcessInRun=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?(System.NumberOfTasks-1):0);
                    System.RunPointer[ThisCoreID]=(System.RunPointerDirection[ThisCoreID]==QUEUE_RUN_ASCENDING?0:(System.NumberOfTasks-1));
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
                        if(TISM_SchedulerRunTask(ThisCoreID)==OK)
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
                          if(System.SystemDebug==DEBUG_HIGH)  fprintf(STDOUT, "TISM_Scheduler #%d: Task ID %d (%s) completed at %llu.\n", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName, time_us_64());
                          
                          // If the task hasn´t set a new value for TaskWakeUpTimer, set one based on the task's priority.
                          RunTimestamp=time_us_64();
                          while (System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer<RunTimestamp)
                          {
                            // Make sure the next WakeUpTimer-moment is beyond the current timestamp, in case we've missed earlier timeslots.
                            System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer+=System.Task[System.RunPointer[ThisCoreID]].TaskPriority;
                          }

                          if(System.SystemDebug==DEBUG_HIGH) fprintf(STDOUT, "TISM_Scheduler #%d: Task ID %d (%s) wants to wake up at %llu (now running on core %d).\n", ThisCoreID, System.RunPointer[ThisCoreID], System.Task[System.RunPointer[ThisCoreID]].TaskName, System.Task[System.RunPointer[ThisCoreID]].TaskWakeUpTimer, ThisCoreID);
                        }
                        else
                        {
                          // Task execution returened an error. Stop the System.
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
                    while ((System.RunPointerDirection[ThisCoreID]!=QUEUE_RUN_ASCENDING?(System.RunPointer[ThisCoreID]<=(System.NumberOfTasks-1)):(System.RunPointer[ThisCoreID]<=LastProcessInRun)) && (System.State==RUN));

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
                  if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: run loop stopped, entering state %d.\n", ThisCoreID, System.State);

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
                      fprintf(STDOUT, "TISM_Scheduler #%d: Set SYSTEM_READY_PORT %d to low.\n", ThisCoreID, SYSTEM_READY_PORT);
                      fprintf(STDOUT, "TISM_Scheduler #%d: Stopping after state %d.\n", ThisCoreID, System.State);
                    }

                    // Stop all tasks; set each task state to STOP, run them once, give the opportunity to clean up.
                    // Do not check for return values, do not process any messages.
                    uint8_t ReturnValue;
                    for(uint8_t TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
                    {
                      System.RunPointer[CORE0]=TaskCounter;
                      System.Task[TaskCounter].TaskState=STOP;
                      ReturnValue=TISM_SchedulerRunTask(CORE0);
                      if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Task ID %d (%s) stopped with return value %d.\n", ThisCoreID, TaskCounter, System.Task[TaskCounter].TaskName, ReturnValue);
                    }

                    if(System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: All tasks stopped, system going down.\n", ThisCoreID);
                  }
                  else
                  {
                    // We're on a different core; wait until system state has changed.
                    // Set the RunPointer to the starting value.
                    System.RunPointer[CORE1]=255;
                    while(System.State==STOP)
                    {
                      if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: Waiting....\n", ThisCoreID);
                      sleep_ms(500);
                    }
                  }
                  System.State=DOWN; 
                  break;
    }
  }
  
  //All done
  if (System.SystemDebug) fprintf(STDOUT, "TISM_Scheduler #%d: All done.\n", ThisCoreID);
  return(OK);
}

