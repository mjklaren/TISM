/*
  
  IRQHandler.c
  ============
  Routines to process external interrupts (IRQs). Other functions can 'subscribe' to these events, after which IRQHandler will
  send messages when events occur. This task uses the global circular buffer IRQHandlerInboundQueue defined in TISM_Definitions.h

  The IRQ handler process:
  - Task subscribe thenselves to events on GPIOs by using TISM_IRQHandlerSubscribe-function, which sends a 
    subscription-message to TISM_IRQHandler (type=GPIO number, message=interrupt events).
  - TISM_IRQHander registers the subscriptions (using linear list) and the generic eventhandler to the specified GPIO.
  - When an interrupt is received TISM_IRQHandler (TISM_IRQHandlerCallback function) writes a message to IRQHandlerInboundQueue.
  - TISM_Scheduler continuously checks for the availability of new messages in this queue. If a message is waiting, 
    TISM_IRQHandler is started.
  - Based on the linear list TISM_IRQHandler sends messages to the corresponding tasks via the regular messaging method.
  - The subscribed tasks will receive a message coming from TISM_IRQHandler, GPIO number as message type and Event number as 
    message for further processing.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "TISM.h"


/*
  The internal structures for the IRQ handler.
*/

// Structure of the linear list containing all subscriptions to interrupts; one list per GPIO.
struct TISM_IRQHandlerSubscription
{
  uint8_t TaskID, HostID;
  uint32_t Events;
  uint32_t AntiBounceTimeout;
  uint64_t LastSuccessfullInterrupt;
  struct TISM_IRQHandlerSubscription *NextSubscription;
};


// Structure containing relevevant information for management of the GPIO.
struct TISM_IRQHandlerDataGPIO
{
  bool Initialized, GPIOPullDown;
  uint32_t EventMask;
  struct TISM_IRQHandlerSubscription *Subscriptions;
};


// The structure containing all data for TISM_IRQHandler to run.
struct TISM_IRQHandlerData
{
  struct TISM_IRQHandlerDataGPIO GPIO[NUMBER_OF_GPIO_PORTS];
} TISM_IRQHandlerData;



/*
  Description 
  Subscribe a task to a specific event occuring on the specified GPIO by sending a request message
  to TISM_IRQHandler. No error checking on the specified GPIOs or Event types. WHen the specified event occurs,
  TISM_IRQHandler sends a message (message type is the GPIO number).

  Note: when a subscription is handled for the first time for a specific GPIO, the port function is set to
        GPIO_FUNC_SIO, direction is set to input and treated as 'pull down' port.

  Parameters:
  TISM_Task ThisTask         - Struct containing all task related information.
  uint GPIO                  - GPIO subscription to modify.
  uint32_t Events            - Events to subscribe to (one or more, by applying bitwise OR '|'):
                               GPIO_IRQ_LEVEL_LOW
                               GPIO_IRQ_LEVEL_HIGH
                               GPIO_IRQ_EDGE_FALL
                               GPIO_IRQ_EDGE_RISE                                                                                             
                               IRQ_UNSUBSCRIBE     - Unsubscribe from the specified GPIO interrupts
  bool GPIOPullDown          - Initialize GPIO with pull-down resistor when ´true´. The first registration determines the actual setting.
  uint32_t AntiBounceTimeout - Timeout period (in usec) that needs to expire before next similar interrupt is forwarded 
                               (anti-bounce measure). Max. timeout period is 16777215 usec (24 bits); we use the remaining bits
                               to pass other parameters through the message.

  Events: GPIO_IRQ_LEVEL_LOW = 0x1u , GPIO_IRQ_LEVEL_HIGH = 0x2u , GPIO_IRQ_EDGE_FALL = 0x4u , GPIO_IRQ_EDGE_RISE = 0x8u

  Return value:
  false - Message delivery failed.
  true  - Request sent.
*/
bool TISM_IRQHandlerSubscribe(TISM_Task ThisTask, uint8_t GPIO, uint32_t Events, bool GPIOPullDown, uint32_t AntiBounceTimeout)
{
  // Use the Specification-field in the message to capture the AntiBounceTimeout and the GPIOPullDown.
  uint32_t CombinedValue=(0xFFFFFF & AntiBounceTimeout)+(GPIOPullDown==true?0x01000000:0);
  return(TISM_PostmanTaskWriteMessage(ThisTask,System.HostID,System.TISM_IRQHandlerTaskID,GPIO,Events,CombinedValue));
}


// Calculate the event mask for all registered subscriptions for a specific GPIO by calculating the OR-value of all events.
uint32_t TISM_IRQHandlerCalculateEventsMask(struct TISM_IRQHandlerSubscription *GPIO)
{
  uint32_t EventMask=0;
  struct TISM_IRQHandlerSubscription *NextGPIO;

  // Is the first value not null (no subscription registered for this GPIO)?
  if(GPIO!=NULL)
  {
    // Run through all the registered subscription events and calculate the events mask (OR-function of all registered events).
    NextGPIO=GPIO;
    do
    {
      EventMask=EventMask|NextGPIO->Events;
      NextGPIO=NextGPIO->NextSubscription;
    }
    while(NextGPIO!=NULL);
  }
  else
  {
    // Current GPIO register is empty; return 0.
    return(0);
  }
  return(EventMask);
}


//  The generic interrupt handler; this function is registered for handling of all interrupts. When an interrupt occurs
//  the details of the event are written to a circular buffer (IRQHandlerInboundQueue) for future processing.
void TISM_IRQHandlerCallback(uint8_t GPIO,uint32_t Events)
{
  // Interrupt received; write the interrupt to the circular buffer IRQHandlerInboundQueue for later processing.
  TISM_PostmanWriteMessage(System.IRQHandlerInboundQueue,System.HostID,System.TISM_IRQHandlerTaskID,System.HostID,System.TISM_IRQHandlerTaskID,GPIO,Events,0,time_us_64());
  gpio_acknowledge_irq(GPIO,Events);
}


/*
  Description
  The main task for the IRQ handler. Handles the initialization and registration of subscriptions to GPIO interrupts.
  This function is called by TISM_Scheduler.

  Parameters:
  TISM_Task ThisTask - Struct containing all task related information.

  Return value:
  OK              - Task run completed succesfully.
  None-zero value - Error
*/
uint8_t TISM_IRQHandler (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Task required to initialize                
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // Init the circular buffer to receive interrupts and clear the subscription-list.
                // This is a 2nd circular buffer, next to the 'regular' inbound queue for inter-task messaging.
                if(!(System.IRQHandlerInboundQueue=TISM_PostmanBufferInit(IRQHANDLER_MAX_MESSAGES,sizeof(TISM_Message))))
                  return(ERR_INITIALIZING);
              
                // Initalize the register of registered subscriptions.
                for(uint8_t counter=0;counter<NUMBER_OF_GPIO_PORTS;counter++)
                {
                  TISM_IRQHandlerData.GPIO[counter].Initialized=false;
                  TISM_IRQHandlerData.GPIO[counter].GPIOPullDown=true;                  
                  TISM_IRQHandlerData.GPIO[counter].EventMask=0;
                  TISM_IRQHandlerData.GPIO[counter].Subscriptions=NULL;
                }

                // Go to sleep; we only wake on incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   // Do the work						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // Are there any interrupts waiting in the circular buffer we need to process?
                uint16_t MessageCounter=0;
                TISM_Message *MessageToProcess;	
                while((TISM_PostmanMessagesWaiting(System.IRQHandlerInboundQueue)>0) && (MessageCounter<IRQHANDLER_MAX_MESSAGES))
                {
                  // Read the next IRQ message from the queue and check which tasks have subscribed to it.
                  MessageToProcess=TISM_PostmanReadMessage(System.IRQHandlerInboundQueue);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Processing interrupt message '%ld' with type %d from the IRQ handler queue.", MessageToProcess->Message, MessageToProcess->MessageType);

                  if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Initialized)
                  {                
                    struct TISM_IRQHandlerSubscription *SearchPointer=TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions;
                    while(SearchPointer!=NULL)
                    {
                      if(MessageToProcess->Message & SearchPointer->Events)
                      {
                        // Is an anti bounce value specified? If so, did we receive a message too soon?
                        if((SearchPointer->AntiBounceTimeout==0) || 
                          (MessageToProcess->MessageTimestamp>(SearchPointer->LastSuccessfullInterrupt+SearchPointer->AntiBounceTimeout)))
                        {
                          TISM_PostmanTaskWriteMessage(ThisTask,SearchPointer->HostID,SearchPointer->TaskID,MessageToProcess->MessageType,MessageToProcess->Message,TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown);                          
                          SearchPointer->LastSuccessfullInterrupt=MessageToProcess->MessageTimestamp;
                        }
                        else
                        {
                          // Message blocked as it is received within the anti bounce timeout period.
                          if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Interrupt from GPIO %d blocked for task ID %d, within anti bounce timeout (%d).", MessageToProcess->MessageType, SearchPointer->TaskID, SearchPointer->AntiBounceTimeout);
                        }
                      }
                      SearchPointer=SearchPointer->NextSubscription;
                    }
                  }
          
                  // Processed the message; delete it.
                  TISM_PostmanDeleteMessage(System.IRQHandlerInboundQueue);
                  MessageCounter++;
                }

                // Now check for other pending messages from other tasks.
                MessageCounter=0;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (HostID %d) received.", MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                  // Processed the message.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                    TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                    break;
                    case GPIO_0:    // GPIO number received as message type - we need to create or adjust a IRQ subscription.
                    case GPIO_1:
                    case GPIO_2:
                    case GPIO_3:
                    case GPIO_4:
                    case GPIO_5:
                    case GPIO_6:
                    case GPIO_7:
                    case GPIO_8:
                    case GPIO_9:
                    case GPIO_10:
                    case GPIO_11:
                    case GPIO_12:
                    case GPIO_13:
                    case GPIO_14:
                    case GPIO_15:
                    case GPIO_16:
                    case GPIO_17:
                    case GPIO_18:
                    case GPIO_19:
                    case GPIO_20:
                    case GPIO_21:
                    case GPIO_22:
                    case GPIO_26:
                    case GPIO_27:
                    case GPIO_28:   // Subscription request received; register or update.
                                    // Is this GPIO already initialized? If not, then this is our first subscription.
                                    if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Processing GPIO request.");
                                    
                                    struct TISM_IRQHandlerSubscription *SearchPointer, *PreviousSearchPointer;
                                    if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Initialized==false)
                                    {
                                      // Bug fix; when we receive an IRQ_UNSUBSCRIBE-request to an uninitialized port.
                                      if(MessageToProcess->Message==IRQ_UNSUBSCRIBE)
                                      {
                                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Warning - Unsubscribe request received from %d (HostID %d) for an uninitialized GPIO (%d); ignoring.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->MessageType);
                                        break;
                                      }

                                      // Initialize the GPIO port for inbound, with the internal pull-down resistor set.
                                      gpio_set_function(MessageToProcess->MessageType, GPIO_FUNC_SIO);
                                      gpio_set_dir(MessageToProcess->MessageType, false);

                                      // Is the 'pull down' bit set in the Specification-field?
                                      if((MessageToProcess->Specification & 0x01000000)==0x01000000)
                                      {
                                        gpio_pull_down(MessageToProcess->MessageType);
                                        TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown=true;
                                      }
                                      else
                                      {
                                        gpio_pull_up(MessageToProcess->MessageType);
                                        TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown=false;
                                      }

                                      if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "First subscription, GPIO %d initialized (request from TaskID %d, HostID %d, event %d, internal resistor pull-%s).", MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->Message, (TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown==false?"up":"down"));

                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Initialized=true;

                                      // Now register the first subscription in the linear list.
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions=(struct TISM_IRQHandlerSubscription *) malloc(sizeof(struct TISM_IRQHandlerSubscription));
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->HostID=MessageToProcess->SenderHostID;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->TaskID=MessageToProcess->SenderTaskID;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->Events=MessageToProcess->Message;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->AntiBounceTimeout=(MessageToProcess->Specification & 0xFFFFFF);
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->LastSuccessfullInterrupt=0;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->NextSubscription=NULL;
                                    }
                                    else
                                    {
                                      // There are already tasks subscribed to this GPIO. 
                                      if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Subscription to GPIO %d being added or modified (TaskID %d, HostID %d, event %d).", MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->Message);

                                      // Find the entry for this TaskID in the linear list, or create a new one.
                                      SearchPointer=TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions;
                                      PreviousSearchPointer=SearchPointer;
                                      while((SearchPointer!=NULL) && (SearchPointer->HostID!=MessageToProcess->SenderHostID) && (SearchPointer->TaskID!=MessageToProcess->SenderTaskID))
                                      {
                                        // Not the one weŕe looking for; move to the next entry.
                                        PreviousSearchPointer=SearchPointer;
                                        SearchPointer=SearchPointer->NextSubscription;
                                      }

                                      // Weŕe out of the loop; the pointer is either pointing to an existing record, or at the end of the list.
                                      if(SearchPointer!=NULL)
                                      {
                                        // Delete or update the existing record?
                                        if(MessageToProcess->Message==IRQ_UNSUBSCRIBE)
                                        {
                                          // Unsubscribe = delete the record from the linear list.
                                          if(SearchPointer==PreviousSearchPointer)
                                          {
                                            // The record is the very first in the list.
                                            TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions=SearchPointer->NextSubscription;
                                          }
                                          else
                                          {
                                            // Remove the record from within the list.
                                            PreviousSearchPointer->NextSubscription=SearchPointer->NextSubscription;
                                          }
                                          free(SearchPointer);

                                          if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d (HostID %d) unsubscribed from GPIO %d.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->MessageType);

                                          // Was this the last subscription to this GPIO? Then we can 'release' this GPIO.
                                          if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions==NULL)
                                          {
                                             // No subscriptions.
                                            if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "No tasks subscribed to GPIO %d, releasing.", MessageToProcess->MessageType);

                                            // Todo; interrupt handler for GPIO's are never released - not even possible in SDK?
                                          }
                                        }
                                        else
                                        {
                                          // Update the existing record in the list.
                                          SearchPointer->Events=MessageToProcess->Message;

                                          if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Subscription to GPIO %d modified.", MessageToProcess->MessageType);
                                        }
                                      }
                                      else
                                      {
                                        // Add a new record to the linear list
                                        struct TISM_IRQHandlerSubscription *NewSubscription=(struct TISM_IRQHandlerSubscription *)malloc(sizeof(struct TISM_IRQHandlerSubscription));
                                        NewSubscription->TaskID=MessageToProcess->SenderHostID;
                                        NewSubscription->TaskID=MessageToProcess->SenderTaskID;
                                        NewSubscription->Events=MessageToProcess->Message;
                                        NewSubscription->AntiBounceTimeout=(MessageToProcess->Specification & 0xFFFFFF);
                                        NewSubscription->LastSuccessfullInterrupt=0;
                                        NewSubscription->NextSubscription=NULL;
                                        PreviousSearchPointer->NextSubscription=NewSubscription;

                                        if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d (HostID %d) subscribed to GPIO %d, anti-bounce value %d", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->MessageType, NewSubscription->AntiBounceTimeout);                                        
                                      }
                                    }

                                    // Recalculate the new eventmask for this GPIO and set it.
                                    if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions!=NULL)
                                    {
                                      // There are still subscriptions left; recalculate the event mask and set the interrupt handler.
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask=TISM_IRQHandlerCalculateEventsMask(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions);

                                      // (Re)register our interrupt-handler to this GPIO.
                                      gpio_set_irq_enabled_with_callback(MessageToProcess->MessageType,TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask,true,(void*)&TISM_IRQHandlerCallback);
                                    }
                                    else
                                    {
                                      // No subscriptions left. Set the eventmask to 0 and disable the interrupt handler for this GPIO.
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask=0;
                                      //gpio_set_irq_enabled_with_callback(MessageToProcess->MessageType,TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask,false,(void*)&TISM_IRQHandlerCallback);                                    
                                    }
                                    
		                                if (ThisTask.TaskDebug) 
                                    {
                                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Subscriptions list for GPIO %d updated.", MessageToProcess->MessageType);
                                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "We are Host ID:%d", System.HostID);
                                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Tasks registered to interrupts on this GPIO:");
                                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "============================================");                                      
                                      SearchPointer=TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions;
                                      while(SearchPointer!=NULL)
                                      {
                                        TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d (HostID %d) subscribed to event %d, anti-bounce value %d.", SearchPointer->TaskID, SearchPointer->HostID, SearchPointer->Events, SearchPointer->AntiBounceTimeout);
                                        SearchPointer=SearchPointer->NextSubscription;
                                      }
                                      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "List complete. Summary event mask: %ld. GPIO initialized as %s.", TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask, (TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown?"pull-down":"pull-up"));
                                    }
                                    break;
                    case GPIO_23:   // Power save
                    case GPIO_24:   // VBUS detect
                    case GPIO_25:   // LED port of the Raspberry Pi Pico
                    default:        // Unknown message type - ignore.
                                    TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_ERROR, "Warning - Invalid GPIO subscription (%d) requested by %d (HostID %d); ignoring.", MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                    break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Go to sleep; we only wake on incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");
  return (OK);
}

