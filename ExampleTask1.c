/*

  Example task 1; a task that responds to a press of a button connected to one of the GPIOs.
  This code uses the TaskManager, IRQHandler and messaging functions.

  Note: messages are stored in an outbound queue and only delivered AFTER a task completes a run.

*/

#include "TISM.h"

#define EXAMPLE1GPIO       15
#define EXAMPLE1ANTIBOUNCE 0  // in usec


// The structure containing all data for this task to run.
struct ExampleTask1Data
{
  uint8_t ButtonPressCounter, ExampleTask2ID, ExampleTask3ID;
} ExampleTask1Data;


/*
  Description:
  Example task 1; a task that responds to a press of a button connected to one of the GPIOs and counts the number of events.
  The GPIO is configured with the internal resistor as pull-up; the button connected to ground.
  This task will listen for EDGE_FALL and EDGE_RAISE events; messages are sent to ExampleTask2 and ExampleTask3.
  Some logging is added to support debugging.

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t ExampleTask1 (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) printf("%s: Run starting.\n", ThisTask.TaskName);
  
  // The scheduler maintains the state of the task and the system. 
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);
				        
                // Store the IDs of the tasks we will be sending messages to.
                ExampleTask1Data.ExampleTask2ID=TISM_GetTaskID("ExampleTask2");
                ExampleTask1Data.ExampleTask3ID=TISM_GetTaskID("ExampleTask3");

                /*
                  Subscribe to events on gpio EXAMPLE1GPIO; listen for GPIO_IRQ_EDGE_FALL and GPIO_IRQ_EDGE_FALL, 
                  set the internal resistor of the RP2040 to pull-up (pull-down is 'false'). To prevent processing of bouncing of the 
                  switch set the antibounce timer at EXAMPLE1ANTIBOUNCE.
                */
                ExampleTask1Data.ButtonPressCounter=0;
                TISM_IRQHandlerSubscribe(ThisTask,EXAMPLE1GPIO,GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL,false,EXAMPLE1ANTIBOUNCE);

                // Let the task go to sleep. When an event occurs (a message is received) the task will be woken up. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
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
                    case TISM_PING:    // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                       TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                       break;
                    case EXAMPLE1GPIO: /* 
                                         Message received from the IRQhandler - the button is pressed or released. 
                                         Actions based on button being pressed or released.
                                       */
                                       switch(MessageToProcess->Message)
                                       {
                                         case GPIO_IRQ_EDGE_FALL: // Button is pressed.
                                                                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: The button is pressed!\n", ThisTask.TaskName);  

                                                                  TISM_PostmanWriteMessage(ThisTask,ExampleTask1Data.ExampleTask2ID,MessageToProcess->Message,0,0);
                                                                  TISM_PostmanWriteMessage(ThisTask,ExampleTask1Data.ExampleTask3ID,MessageToProcess->Message,0,0);
                                         
                                                                  break;
                                         case GPIO_IRQ_EDGE_RISE: // Button is released.
                                                                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: The button is released!\n", ThisTask.TaskName);

                                                                  TISM_PostmanWriteMessage(ThisTask,ExampleTask1Data.ExampleTask3ID,MessageToProcess->Message,0,0);                                                                  
                                                                  break;
                                       }

                                       // Increase the counter - will wrap around after 255 events.
                                       ExampleTask1Data.ButtonPressCounter++;
                                       if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Number of events: %d\n", ThisTask.TaskName,ExampleTask1Data.ButtonPressCounter);
                                       
                                       break;
                    default:           // Unknown message type - ignore.
                                       break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // All events are processed; go back to sleep.
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
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

