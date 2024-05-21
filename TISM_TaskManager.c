/*

  TISM_TaskManager.c - Code to manipulate task and system states, when requested via messages.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "TISM.h"


// Set the specified attribute of a task.
uint8_t TISM_TaskManagerSetTaskAttribute(struct TISM_Task ThisTask, uint8_t TargetTaskID, uint8_t AttributeToChange, uint32_t Setting)
{
  // Check if the specified Task ID is valid and if it's not a TISM-system task.
  if(TISM_IsValidTaskID(TargetTaskID))
  {
    // Some attributes for system-tasks can only be changed by other system tasks. Other tasks are not allowed for system tasks.
    switch(AttributeToChange)
    {
      case TISM_SET_TASK_WAKEUPTIME: // Set the task wakeup timer at utime + the specified usec.
      case TISM_SET_TASK_PRIORITY  :
      case TISM_SET_TASK_SLEEP     : // When system tasks; only allowed when requested by other system tasks.
                                     if(TISM_IsSystemTask(TargetTaskID))
                                     {
                                       if(TISM_IsSystemTask(ThisTask.TaskID))
                                       {
                                         // Compose a message to Task Manager to adjust the attributes.
                                         TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,AttributeToChange,Setting,TargetTaskID);
                                       }
                                       else
                                       {
                                         // Attempt to change priority or sleep state of a system task by a non-system task. 
                                         fprintf(STDERR, "%s: Attempt to change priority, wakeup time or sleep state of system task by non-system task, which is not allowed.\n", ThisTask.TaskName);
                                         return(ERR_INVALID_OPERATION);
                                       }
                                     }
                                     else
                                     {
                                       // Target is not a system task. Forward the requested operation.
                                       TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,AttributeToChange,Setting,TargetTaskID);
                                     }
                                     break;
      case TISM_DEDICATE_TO_TASK   : // Not allowed for system tasks
                                     if(TISM_IsSystemTask(TargetTaskID))
                                     {
                                       fprintf(STDERR, "%s: Task dedication requested for a system task, which is not allowed.\n", ThisTask.TaskName);
                                       return(ERR_INVALID_OPERATION);
                                     }
                                     else
                                     {
                                       // Compose a message to Task Manager to adjust the attributes.
                                       TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,AttributeToChange,Setting,TargetTaskID);
                                     }
                                     break;
      case TISM_WAKE_ALL_TASKS     :
      case TISM_SET_TASK_STATE     :
      case TISM_SET_TASK_DEBUG     : // No checking here.
                                     TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,AttributeToChange,Setting,TargetTaskID);
                                     break;
      default                      : // Unknown action requested; generate error message.
                                     fprintf(STDERR, "%s: Unknown attribute change (%d) requested.\n", ThisTask.TaskName,AttributeToChange);
                                     return(ERR_INVALID_OPERATION);
                                     break;
    }
  }
  else
    return(ERR_TASK_NOT_FOUND);
  return(OK);
}


// Wrapper for TISM_TaskManagerSetTaskAttribute; set the specified attribute for the requesting task itself.
uint8_t TISM_TaskManagerSetMyTaskAttribute(struct TISM_Task ThisTask, uint8_t AttributeToChange, uint32_t Setting)
{
  return(TISM_TaskManagerSetTaskAttribute(ThisTask,ThisTask.TaskID,AttributeToChange,Setting)); 
}


//  Set the state of the entire TISM system.
uint8_t TISM_TaskManagerSetSystemState(struct TISM_Task ThisTask, uint8_t SystemState)
{
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,TISM_SET_SYS_STATE,SystemState,0));
}


/*
  This is the task that is registered in the TISM-system. This function is called by TISM_Scheduler.
  Note: No checking for valid task IDs and requested operations; this is handled by TISM_TaskManagerSetTaskAttribute.
*/
uint8_t TISM_TaskManager (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) printf("%s: Run starting.\n", ThisTask.TaskName);

  switch(ThisTask.TaskState)   // Unknown states are ignored
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);

                // Bring tasks TaskManager, Postman, IRQHandler and Watchdog to sleep.
                System.Task[System.TISM_TaskManagerTaskID].TaskSleeping=true;
                System.Task[System.TISM_PostmanTaskID].TaskSleeping=true;
                System.Task[System.TISM_IRQHandlerTaskID].TaskSleeping=true;
				        break;
	  case RUN:   // Do the work
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task %d doing work at %llu with priority %d on core %d.\n", ThisTask.TaskName, ThisTask.TaskID, time_us_64(), ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
				
				        /*
                  Mapping between messaging structure and TaskManager fields:
                  MessageToProcess->MessageType  = AttributeToChange
                  MessageToProcess->Message      = Setting
                  MessageToProcess->TargetTaskID = Specification
                */
                int MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  // We received a message; figure out what we need to do.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:                // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                                   TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                                   break;
                    case TISM_SET_TASK_SLEEP:      // Change the sleep state of the specified task.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: AttributeToChange %d (TISM_SET_TASK_SLEEP) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   if(MessageToProcess->Message==0)
                                                   {
                                                     // Task needs to be awake. Check if this is not already the case.
                                                     if(System.Task[(uint8_t)MessageToProcess->Specification].TaskSleeping==true)
                                                     {
                                                       System.Task[(uint8_t)MessageToProcess->Specification].TaskSleeping=false;
                                                       System.Task[(uint8_t)MessageToProcess->Specification].TaskWakeUpTimer=time_us_64();
                                                     }
                                                   }
                                                   else
                                                     System.Task[(uint8_t)MessageToProcess->Specification].TaskSleeping=true;
                                                   break;
                    case TISM_SET_TASK_WAKEUPTIME: // Change the wake up time for the specified task, in "Now¨ + specified usec.                                                 
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: AttributeToChange %d (TISM_SET_WAKEUP_TIME) for TargetTaskID %d (%s) with setting utime + %ld received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskWakeUpTimer=time_us_64()+MessageToProcess->Message;
                                                   break;
                    case TISM_SET_SYS_STATE:       // Change the state of the whole system (aka runlevel).
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: Set system state (TISM_SET_SYS_STATE) to %d received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.State=(uint8_t)MessageToProcess->Message;

                                                   if(System.SystemDebug) fprintf(STDOUT, "%s: System state changed to %d.\n", ThisTask.TaskName, System.State);
                                                 
                                                   break;
                    case TISM_SET_TASK_STATE:      // Change the state of the specified task. These can be custom values.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: AttributeToChange %d (TISM_SET_TASK_STATE) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskState=(uint8_t)MessageToProcess->Message;
                                                   break;
                    case TISM_SET_TASK_PRIORITY:   // Set the priority of a specific task to the specified priority level.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: AttributeToChange %d (TISM_SET_TASK_PRIORITY) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskPriority=MessageToProcess->Message;
                                                   break;
                    case TISM_WAKE_ALL_TASKS:      // Wake all tasks.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: Wake all tasks (TISM_WAKE_ALL_TASKS) received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   for(uint8_t TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
                                                   {
                                                     // Only wake tasks that are currently sleeping; do not interfere with existing time schedules.
                                                     if(System.Task[TaskCounter].TaskSleeping==true)
                                                     {
                                                       System.Task[TaskCounter].TaskWakeUpTimer=time_us_64();
                                                       System.Task[TaskCounter].TaskSleeping=false;
                                                     }
                                                   }
                                                 
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT,"%s: All tasks have been woken up.\n", ThisTask.TaskName);
                                                        
                                                   break;  
                    case TISM_DEDICATE_TO_TASK:    // Dedicate the whole system to a specific task - use with caution.
                                                   // Check first if the target task is not sleeping.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: Dedicate to task (TISM_DEDICATE_TO_TASK) requested for %d (%s) by TaskID %d (%s).\n", ThisTask.TaskName, (int)MessageToProcess->Message, System.Task[(int)MessageToProcess->Message].TaskName, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   if(!System.Task[(uint8_t)MessageToProcess->Message].TaskSleeping)
                                                   {
                                                     // Yes, put all other tasks to sleep.
                                                     for(uint8_t TaskIDCounter=0;TaskIDCounter<System.NumberOfTasks;TaskIDCounter++)
                                                     {
                                                       // Only non-system tasks are put to sleep.
                                                       if((TaskIDCounter!=MessageToProcess->Message) && (!TISM_IsSystemTask(TaskIDCounter)))
                                                         System.Task[TaskIDCounter].TaskSleeping=true;
                                                     }
                                      
                                                     if(ThisTask.TaskDebug) fprintf(STDOUT,"%s: Warning - system now dedicated to task ID %d (%s).\n", ThisTask.TaskName, (int)MessageToProcess->Message, System.Task[(int)MessageToProcess->Message].TaskName);
                                    
                                                   }
                                                   else
                                                     fprintf(STDERR, "TISM_TaskManager: Task to dedicate to (%s,ID %d) is sleeping. Aborting.\n", System.Task[(int)MessageToProcess->Message].TaskName, (int)MessageToProcess->Message);
                                                   break;
                    case TISM_SET_TASK_DEBUG:      // Set the debug level for a task to the specified value.
                                                   if(ThisTask.TaskDebug) fprintf(STDOUT, "%s: AttributeToChange %d (TISM_SET_TASK_DEBUG) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskDebug=(uint8_t)MessageToProcess->Message;
                                                   break;
                    default:                       // Invalid or not yet implemented message type - ignore.
                                                   break;
                  }

                  // Processed the message; delete it.
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }
                // Go to sleep; we only wake on incoming messages (only TaskManager can do this directly).
                System.Task[ThisTask.TaskID].TaskSleeping=true;
				        break;
	  case STOP:  // Tasks required to stop
		            if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Stopping.\n", ThisTask.TaskName);
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN (only TaskManager can do this directly).
                System.Task[ThisTask.TaskID].TaskState=DOWN;
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run completed.\n", ThisTask.TaskName);
  return (OK);
}

