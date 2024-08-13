/*
  TISM_TaskManager.c
  ==================
  Library with functions to manipulate task properties and system states, when requested via messages.

  All manipulation of system and task states are handled via TISM_Taskmanager to prevent two tasks from changing
  the state of things at the same time, with unexpected results. As only one instance of TISM_Taskmanager can run at a
  time, thread safety is achieved. 
  
  Note: Still we're not completely thread-safe as TISM_Taskmanager can write to these variables, but 
        TISM_Scheduler running on another core can attempt to read the same variable at the same time.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  Description
  Set the specified attribute of a task (see attibutes above).

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t TargetTaskID       - TaskID of the task to change
  uint8_t AttributeToChange  - Attribute to change (see below)
  uint32_t Setting           - New setting (see below)

  Return value:
  ERR_INVALID_OPERATION      - Invalid request
  ERR_TASK_NOT_FOUND         - Specified task not found
  OK                         - Succes

  AttributeToChange and corresponding Setting values:
  TISM_SET_TASK_STATE        - Change the state of a task.
                               Setting: Custom value or predefined (recommended): DOWN, STOP, RUN or INIT
  TISM_SET_TASK_PRIORITY     - Set the priority of a specific task to PRIORITY
                               Setting: PRIORITY_LOW, PRIORITY_NORMAL or PRIORITY_HIGH
  TISM_SET_TASK_SLEEP        - Set the sleep state of a specific state 
                               Setting: true or false
  TISM_SET_TASK_WAKEUPTIME   - Set the timestamp of the next wake up (in usec); interpreted as "NOW"+timestamp
                               Setting: timestamp in usec
  TISM_SET_TASK_DEBUG        - Set the debug level of a specific task
                               Setting: DEBUG_NONE, DEBUG_NORMAL or DEBUG_HIGH  
  TISM_WAKE_ALL_TASKS        - Wake all tasks.
                               Setting: 0
  TISM_DEDICATE_TO_TASK      - Dedicate the whole system to a specific task (use with caution)
                               Setting: 0
*/
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
                                         TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Attempt to change priority, wakeup time or sleep state of system task by non-system task, which is not allowed.");
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
                                       TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Task dedication requested for a system task, which is not allowed.");
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
                                     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Unknown attribute change (%d) requested.", AttributeToChange);
                                     return(ERR_INVALID_OPERATION);
                                     break;
    }
  }
  else
    return(ERR_TASK_NOT_FOUND);
  return(OK);
}


/*
  Description
  Wrapper for TISM_TaskManagerSetTaskAttribute; set the specified attribute for the requesting task itself.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t AttributeToChange  - Attribute to change (see below)
  uint32_t Setting           - New setting (see below)

  Return value:
  ERR_INVALID_OPERATION      - Invalid request
  ERR_TASK_NOT_FOUND         - Specified task not found
  OK                         - Succes

  AttributeToChange and corresponding Setting values:
  TISM_SET_TASK_STATE        - Change the state of a task.
                               Setting: Custom value or predefined (recommended): DOWN, STOP, RUN or INIT
  TISM_SET_TASK_PRIORITY     - Set the priority of a specific task to PRIORITY
                               Setting: PRIORITY_LOW, PRIORITY_NORMAL or PRIORITY_HIGH
  TISM_SET_TASK_SLEEP        - Set the sleep state of a specific state 
                               Setting: true or false
  TISM_SET_TASK_WAKEUPTIME   - Set the timestamp of the next wake up (in usec); interpreted as "NOW"+timestamp
                               Setting: timestamp in usec
  TISM_SET_TASK_DEBUG        - Set the debug level of a specific task
                               Setting: DEBUG_NONE, DEBUG_NORMAL or DEBUG_HIGH  
  TISM_WAKE_ALL_TASKS        - Wake all tasks.
                               Setting: 0
  TISM_DEDICATE_TO_TASK      - Dedicate the whole system to a specific task (use with caution)
                               Setting: 0
*/
uint8_t TISM_TaskManagerSetMyTaskAttribute(struct TISM_Task ThisTask, uint8_t AttributeToChange, uint32_t Setting)
{
  return(TISM_TaskManagerSetTaskAttribute(ThisTask,ThisTask.TaskID,AttributeToChange,Setting)); 
}


/*
  Description
  Set the state of the entire TISM system. Any task can alter the system state.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint8_t SystemState        - System state (see above)

  Return value:
  non-zero value             - Error sending the request
  OK                         - Succes
*/
uint8_t TISM_TaskManagerSetSystemState(struct TISM_Task ThisTask, uint8_t SystemState)
{
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_TaskManagerTaskID,TISM_SET_SYS_STATE,SystemState,0));
}


/*
  Description:
  This is the TaskManager-function that is registered in the TISM-system.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask      - Struct containing all task related information. 

  Return value:
  <non zero value>        - Task returned an error when executing.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_TaskManager (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");

  switch(ThisTask.TaskState)   // Unknown states are ignored
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);

                // Bring tasks TaskManager, Postman, IRQHandler and Watchdog to sleep.
                System.Task[System.TISM_TaskManagerTaskID].TaskSleeping=true;
                System.Task[System.TISM_PostmanTaskID].TaskSleeping=true;
                System.Task[System.TISM_IRQHandlerTaskID].TaskSleeping=true;
				        break;
	  case RUN:   // Do the work
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);
				
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
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "AttributeToChange %d (TISM_SET_TASK_SLEEP) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).", MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

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
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "AttributeToChange %d (TISM_SET_WAKEUP_TIME) for TargetTaskID %d (%s) with setting utime + %ld received from TaskID %d (%s).", MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskWakeUpTimer=time_us_64()+MessageToProcess->Message;
                                                   break;
                    case TISM_SET_SYS_STATE:       // Change the state of the whole system (aka runlevel).
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Set system state (TISM_SET_SYS_STATE) to %d received from TaskID %d (%s).", MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.State=(uint8_t)MessageToProcess->Message;

                                                   if(System.SystemDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "System state changed to %d.", System.State);
                                                 
                                                   break;
                    case TISM_SET_TASK_STATE:      // Change the state of the specified task. These can be custom values.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "AttributeToChange %d (TISM_SET_TASK_STATE) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).", MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskState=(uint8_t)MessageToProcess->Message;
                                                   break;
                    case TISM_SET_TASK_PRIORITY:   // Set the priority of a specific task to the specified priority level.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "AttributeToChange %d (TISM_SET_TASK_PRIORITY) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).", MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   System.Task[(uint8_t)MessageToProcess->Specification].TaskPriority=MessageToProcess->Message;
                                                   break;
                    case TISM_WAKE_ALL_TASKS:      // Wake all tasks.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Wake all tasks (TISM_WAKE_ALL_TASKS) received from TaskID %d (%s).", MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   for(uint8_t TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
                                                   {
                                                     // Only wake tasks that are currently sleeping; do not interfere with existing time schedules.
                                                     if(System.Task[TaskCounter].TaskSleeping==true)
                                                     {
                                                       System.Task[TaskCounter].TaskWakeUpTimer=time_us_64();
                                                       System.Task[TaskCounter].TaskSleeping=false;
                                                     }
                                                   }
                                                 
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "All tasks have been woken up.");
                                                        
                                                   break;  
                    case TISM_DEDICATE_TO_TASK:    // Dedicate the whole system to a specific task - use with caution.
                                                   // Check first if the target task is not sleeping.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Dedicate to task (TISM_DEDICATE_TO_TASK) requested for %d (%s) by TaskID %d (%s).", (int)MessageToProcess->Message, System.Task[(int)MessageToProcess->Message].TaskName, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                                                   if(!System.Task[(uint8_t)MessageToProcess->Message].TaskSleeping)
                                                   {
                                                     // Yes, put all other tasks to sleep.
                                                     for(uint8_t TaskIDCounter=0;TaskIDCounter<System.NumberOfTasks;TaskIDCounter++)
                                                     {
                                                       // Only non-system tasks are put to sleep.
                                                       if((TaskIDCounter!=MessageToProcess->Message) && (!TISM_IsSystemTask(TaskIDCounter)))
                                                         System.Task[TaskIDCounter].TaskSleeping=true;
                                                     }
                                      
                                                     if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning - system now dedicated to task ID %d (%s).", (int)MessageToProcess->Message, System.Task[(int)MessageToProcess->Message].TaskName);
                                    
                                                   }
                                                   else
                                                     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Task to dedicate to (%s,ID %d) is sleeping. Aborting.", System.Task[(int)MessageToProcess->Message].TaskName, (int)MessageToProcess->Message);
                                                   break;
                    case TISM_SET_TASK_DEBUG:      // Set the debug level for a task to the specified value.
                                                   if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "AttributeToChange %d (TISM_SET_TASK_DEBUG) for TargetTaskID %d (%s) with setting %ld received from TaskID %d (%s).", MessageToProcess->MessageType, MessageToProcess->Specification, System.Task[MessageToProcess->Specification].TaskName, MessageToProcess->Message, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

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
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN (only TaskManager can do this directly).
                System.Task[ThisTask.TaskID].TaskState=DOWN;
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

