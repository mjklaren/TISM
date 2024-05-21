/*

  A template for a task in the TISM-system.

*/

#include "TISM.h"


/*
  The structure containing all data for this task to run. These variables allow the task to remain its state as the stack 
  and heap are not saved between runs. We use a struct, this is not mandatory (just an easy way to make usre variable
  names are unique in the whole system).
*/
struct TaskTemplateData
{
  int YourVariable1, YourVariable2;  
} TaskTemplateData;


/*
  Description:
  This is the function that is registered in the TISM-system via the TISM_RegisterTask. A pointer to this function is used.
  For debugging purposes three fprintf-statements are added (not mandatory).

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t TaskTemplate (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) printf("%s: Run starting.\n", ThisTask.TaskName);
  
  /*
    The scheduler maintains the state of the task and the system. Specify here the actions per state.
    Using the states (and the case-statements below) is optional.
    Task states INIT, RUN, STOP and DOWN are predefined.
    INIT is used during startup; when all tasks have initialized correctly the system state is set to RUN.
    Once the system is in this state the task can then switch to custom states. When the system stops all tasks are switched
    to the STOP-state. Remember to always check for incoming messages in custom states.
  */
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);
				        
                // Give your variables an initial value.
                TaskTemplateData.YourVariable1=11;
                TaskTemplateData.YourVariable2=22;

                // For tasks that only respond to events (=messages) we could set the sleep attribute to ´true'.
                // TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task %d doing work at %llu with priority %d on core %d.\n", ThisTask.TaskName, ThisTask.TaskID, time_us_64(), ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
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

                // Other work to do in this state.

				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Stopping.\n", ThisTask.TaskName);
		          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run completed.\n", ThisTask.TaskName);

  return (OK);
}

