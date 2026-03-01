/*
  
  Example task 2; a task that blinks the onboard light of the RP2040. Frequency of blinking changes if a message is
  received that a button is pressed (from ExampleTask1). Furthermore, a software timer is set; when this expires the
  blink frequency is also changed.
  This code uses the software timer, virtual software timer and messaging functions.

  Warning: this example task won't work if "TISM_DISABLE_SCHEDULER" is enabled in TISM.h, as the software timer
           is then disabled.

  Note: outgoing messages are stored in an outbound queue and only delivered AFTER a task completes a run.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license
  
*/

#include "TISM.h"

#define EXAMPLETASK2_TIMEOUT_USEC 1000000
#define LED_PIN                   25
#define EXAMPLETASK2_TIMERID      1
#define EXAMPLETASK2_TIMER        20000 // in msec


// Static variables needed for this task to run.
static uint8_t ToggleTimeDivison;
static bool LightIson;
static uint64_t ToggleTime;


/*
  Description:
  Example task 2; a task that blinks the onboard light of the RP2040. Regularly check if the specified timeout hass expired; 
  toggle the light on and off. We do it here by polling virtual software timers. 

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t ExampleTask2 (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  // The scheduler maintains the state of the task and the system. 
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Initialize the LED port.
                gpio_init(LED_PIN);
                gpio_set_dir(LED_PIN, GPIO_OUT);

                // When starting, light is off. Define next toggle period using virtual software timer.
                LightIson=false;
                gpio_put(LED_PIN, 0);
                ToggleTimeDivison=1;
                ToggleTime=TISM_SoftwareTimerSetVirtual(EXAMPLETASK2_TIMEOUT_USEC);

                // Also, set an 'real' software timer to trigger a change in blinking frequency.
                TISM_SoftwareTimerSet(ThisTask,EXAMPLETASK2_TIMERID,true,EXAMPLETASK2_TIMER);

                // For tasks that only respond to events (=messages) we could set the sleep attribute to ´true'.
                // TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %s-0x%02X from TaskID %d (HostID %d) received.", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                  // Processed the message; delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:            // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                               TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Payload0,0);
                                               break;
                    case EXAMPLETASK2_TIMERID: // Software timer expired; Change the ToggleTimeDivision so that
                                               // the frequency of the blinking light will change.
                    case GPIO_IRQ_EDGE_FALL  : // Button is pressed (message from ExampleTask1). Change the ToggleTimeDivision so that
                                               // the frequency of the blinking light will change.
                                               if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Changing frequency of the blinker.");

                                               ToggleTimeDivison=(ToggleTimeDivison==1?4:1);
                                               break;
                    default:                   // Unknown message type - ignore.
                                               break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Other work to do in this state.
                // Did the timeout period expire? If so, toggle the light.
                if(TISM_SoftwareTimerVirtualExpired(ToggleTime))
                {
                  // Timer is expired. Toggle the LED, calculate the next timeout.
                  if(LightIson)
                  {
                    gpio_put(LED_PIN, 0);
                    LightIson=false;
                  }
                  else
                  {
                    gpio_put(LED_PIN, 1);
                    LightIson=true;
                  }
                  ToggleTime=TISM_SoftwareTimerSetVirtual(EXAMPLETASK2_TIMEOUT_USEC/ToggleTimeDivison);
                }
				        break;
	  case STOP:  // Task required to stop this task.
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		          
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

