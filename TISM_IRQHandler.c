/*

  IRQHandler.c - Functions of ITSM IRQ handler module that generates messages to task after specific events have occured.

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
  uint8_t TaskID;
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



// Subscribe a task to a specific event occuring on the specified GPIO by sending a request message to TISM_IRQHandler. 
bool TISM_IRQHandlerSubscribe(TISM_Task ThisTask, uint8_t GPIO, uint32_t Events, bool GPIOPullDown, uint32_t AntiBounceTimeout)
{
  // Use the Specification-field in the message to capture the AntiBounceTimeout and the GPIOPullDown.
  uint32_t CombinedValue=(0xFFFFFF & AntiBounceTimeout)+(GPIOPullDown==true?0x01000000:0);
  return(TISM_PostmanWriteMessage(ThisTask,System.TISM_IRQHandlerTaskID,GPIO,Events,CombinedValue));
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
  TISM_CircularBufferWrite(&IRQHandlerInboundQueue,System.TISM_IRQHandlerTaskID,System.TISM_IRQHandlerTaskID,GPIO,Events,0);
  gpio_acknowledge_irq(GPIO,Events);
}


// The main task for the IRQ handler. Handles the initialization and registration of subscriptions to GPIO interrupts.
// This function is called by TISM_Scheduler.
uint8_t TISM_IRQHandler (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run starting.\n", ThisTask.TaskName);
  
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Task required to initialize                
                if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Initializing with task ID %d and priority %d.\n", ThisTask.TaskName, ThisTask.TaskID, ThisTask.TaskPriority);

                // Init the circular buffer to receive interrupts and clear the subscription-list.
                TISM_CircularBufferInit (&IRQHandlerInboundQueue);           
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
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task %d doing work at %llu with priority %d on core %d.\n", ThisTask.TaskName, ThisTask.TaskID, time_us_64(), ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // Are there any interrupts waiting in the circular buffer we need to process?
                uint16_t MessageCounter=0;
                TISM_Message *MessageToProcess;	
                while((TISM_CircularBufferMessagesWaiting(&IRQHandlerInboundQueue)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  // Read the next IRQ message from the queue and check which tasks have subscribed to it.
                  MessageToProcess=TISM_CircularBufferRead(&IRQHandlerInboundQueue);

                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Processing interrupt message '%ld' with type %d from the IRQ handler queue.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->MessageType);

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
                          TISM_PostmanWriteMessage(ThisTask,SearchPointer->TaskID,MessageToProcess->MessageType,MessageToProcess->Message,TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown);                          
                          SearchPointer->LastSuccessfullInterrupt=MessageToProcess->MessageTimestamp;
                        }
                        else
                        {
                          // Message blocked as it is received within the anti bounce timeout period.
                          if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Interrupt from GPIO %d blocked for task ID %d, within anti bounce timeout (%d).\n", ThisTask.TaskName, MessageToProcess->MessageType, SearchPointer->TaskID, SearchPointer->AntiBounceTimeout);
                        }
                      }
                      SearchPointer=SearchPointer->NextSubscription;
                    }
                  }
          
                  // Processed the message; delete it.
                  TISM_CircularBufferDelete(&IRQHandlerInboundQueue);
                  MessageCounter++;
                }

                // Now check for other pending messages from other tasks.
                MessageCounter=0;
                while((TISM_PostmanMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanReadMessage(ThisTask);

                  if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Message '%ld' type %d from TaskID %d (%s) received.\n", ThisTask.TaskName, MessageToProcess->Message, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);

                  // Processed the message.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                    TISM_PostmanWriteMessage(ThisTask,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
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
                                    if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Processing GPIO request.\n", ThisTask.TaskName);
                                    
                                    struct TISM_IRQHandlerSubscription *SearchPointer, *PreviousSearchPointer;
                                    if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Initialized==false)
                                    {
                                      // Bug fix; when we receive an IRQ_UNSUBSCRIBE-request to an uninitialized port.
                                      if(MessageToProcess->Message==IRQ_UNSUBSCRIBE)
                                      {
                                        fprintf(STDERR,"%s: Warning - Unsubscribe request received from %d (%s) for an uninitialized GPIO (%d); ignoring.\n", ThisTask.TaskName,MessageToProcess->SenderTaskID,System.Task[MessageToProcess->SenderTaskID].TaskName,MessageToProcess->MessageType);
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

                                      if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: First subscription, GPIO %d initialized (request from Task ID %d, event %d, internal resistor pull-%s).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->Message, (TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown==false?"up":"down"));

                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Initialized=true;

                                      // Now register the first subscription in the linear list.
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions=(struct TISM_IRQHandlerSubscription *) malloc(sizeof(struct TISM_IRQHandlerSubscription));
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->TaskID=MessageToProcess->SenderTaskID;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->Events=MessageToProcess->Message;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->AntiBounceTimeout=(MessageToProcess->Specification & 0xFFFFFF);
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->LastSuccessfullInterrupt=0;
                                      TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions->NextSubscription=NULL;
                                    }
                                    else
                                    {
                                      // There are already tasks subscribed to this GPIO. 
                                      if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Subscription to GPIO %d being added or modified (Task ID %d, event %d).\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->Message);

                                      // Find the entry for this TaskID in the linear list, or create a new one.
                                      SearchPointer=TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions;
                                      PreviousSearchPointer=SearchPointer;
                                      while((SearchPointer!=NULL) && (SearchPointer->TaskID!=MessageToProcess->SenderTaskID))
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

                                          if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task ID %d unsubscribed from GPIO %d.\n", ThisTask.TaskName, MessageToProcess->SenderTaskID, MessageToProcess->MessageType);

                                          // Was this the last subscription to this GPIO? Then we can 'release' this GPIO.
                                          if(TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions==NULL)
                                          {
                                             // No subscriptions.
                                            if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: No tasks subscribed to GPIO %d, releasing.\n", ThisTask.TaskName, MessageToProcess->MessageType);

                                            // Todo; interrupt handler for GPIO's are never released - not even possible in SDK?
                                          }
                                        }
                                        else
                                        {
                                          // Update the existing record in the list.
                                          SearchPointer->Events=MessageToProcess->Message;

                                          if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Subscription to GPIO %d modified.\n", ThisTask.TaskName, MessageToProcess->MessageType);
                                        }
                                      }
                                      else
                                      {
                                        // Add a new record to the linear list
                                        struct TISM_IRQHandlerSubscription *NewSubscription=(struct TISM_IRQHandlerSubscription *) malloc(sizeof(struct TISM_IRQHandlerSubscription));
                                        NewSubscription->TaskID=MessageToProcess->SenderTaskID;
                                        NewSubscription->Events=MessageToProcess->Message;
                                        NewSubscription->AntiBounceTimeout=(MessageToProcess->Specification & 0xFFFFFF);
                                        NewSubscription->LastSuccessfullInterrupt=0;
                                        NewSubscription->NextSubscription=NULL;
                                        PreviousSearchPointer->NextSubscription=NewSubscription;

                                        if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Task ID %d subscribed to GPIO %d, anti-bounce value %d\n", ThisTask.TaskName, MessageToProcess->SenderTaskID, MessageToProcess->MessageType, NewSubscription->AntiBounceTimeout);                                        
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
                                      fprintf(STDOUT, "%s: Subscriptions list for GPIO %d updated.\n", ThisTask.TaskName, MessageToProcess->MessageType);
                                      fprintf(STDOUT, "%s: Tasks registered to interrupts on this GPIO:\n", ThisTask.TaskName);
                                      fprintf(STDOUT, "%s: ============================================\n", ThisTask.TaskName);                                      
                                      SearchPointer=TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].Subscriptions;
                                      while(SearchPointer!=NULL)
                                      {
                                        fprintf(STDOUT, "%s: Task ID: %d (%s) subscribed to event %d, anti-bounce value %d.\n", ThisTask.TaskName, SearchPointer->TaskID, System.Task[SearchPointer->TaskID].TaskName, SearchPointer->Events, SearchPointer->AntiBounceTimeout);
                                        SearchPointer=SearchPointer->NextSubscription;
                                      }
                                      fprintf(STDOUT, "%s: List complete. Summary event mask: %ld. GPIO initialized as %s.\n", ThisTask.TaskName, TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].EventMask, (TISM_IRQHandlerData.GPIO[MessageToProcess->MessageType].GPIOPullDown?"pull-down":"pull-up"));
                                    }
                                    break;
                    case GPIO_23:   // Power save
                    case GPIO_24:   // VBUS detect
                    case GPIO_25:   // LED port of the Raspberry Pi Pico
                    default:        // Unknown message type - ignore.
                                    fprintf(STDERR, "%s: Warning - Invalid GPIO subscription (%d) requested by %d (%s); ignoring.\n", ThisTask.TaskName, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, System.Task[MessageToProcess->SenderTaskID].TaskName);
                                    break;
                  }
                  TISM_PostmanDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Go to sleep; we only wake on incoming messages. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case STOP:  // Task required to stop
		            if (ThisTask.TaskDebug) fprintf(STDOUT, "%s: Stopping.\n", ThisTask.TaskName);
		        
				        // Tasks for stopping
			          
                // Set the task state to DOWN. 
                TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // All done.
  if (ThisTask.TaskDebug==DEBUG_HIGH) fprintf(STDOUT, "%s: Run completed.\n", ThisTask.TaskName);
  return (OK);
}

