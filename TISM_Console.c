/*

  Console.c - A console task for the TISM system. Provides some general system and networking features, 
              but can be expanded by the user as needed. This task is registered in the TISM system and 
              can be used for debugging and interaction with the system.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include "TISM.h"
#include "pico/stdlib.h"
#include <time.h>
#include <stdio.h>
#include <malloc.h>
#include "hardware/watchdog.h"


// Menus
#define MAIN_MENU                   100
#define SYSTEM_MENU                 110
#define NETWORK_MENU                120
#define PING_MENU                   121
#define SET_HOSTID_MENU             122

// For interaction with TISM_NetworkManager
#define RESET_DEBUGGING             0x71                     // Reset debugging levels

// For the 'I am alive' timer.
#define I_AM_ALIVE_TIMER            0x72                     // Toggle the 'I am alive' timer in the network manager, which broadcasts our presence on the network. Useful for testing and debugging.


/*
  All data for this task to run. These variables allow the task to remain its state as the stack 
  and heap are not saved between runs.
*/
static int Character, ActiveMenu;
static bool DisplayHelpNext;
static uint64_t TimestampLastCommand;
static char UniqueIDString[17];
bool IamAliveTimerActive;    
 

// For measuring the free heap size.
extern char __bss_end__;
extern char __StackLimit;

static uint32_t TISM_GetTotalHeapBytes(void)
{
  return (uint32_t)(&__StackLimit - &__bss_end__);
}

static uint32_t TISM_GetFreeHeapBytes(void)
{
  struct mallinfo info=mallinfo();
  uint32_t used=(uint32_t)info.uordblks;    // Bytes used by malloc
  uint32_t total=TISM_GetTotalHeapBytes();  // Total heap+stack region
  if (used>total)
    return 0;                
  return total-used;
}



/*
  Description:
  This is the function that is registered in the TISM-system via the TISM_RegisterTask. A pointer to this function is used.
  For debugging purposes the TISM_EventLoggerLogEvent-function is used (not mandatory).

  Parameters:
  TISM_Task ThisTask - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>   - Task returned an error when executing. A non-zero value will stop the system.
  OK                 - Run succesfully completed.
*/
uint8_t TISM_Console (TISM_Task ThisTask)
{
  if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");
  
  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Convert the unique ID of the host to a string for easier display.
                sprintf(UniqueIDString, "%016llX", System.UniqueID);

                TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=====================================");
                TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, " Console running, press '?' for help");
                TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=====================================");
                ActiveMenu=MAIN_MENU;
                DisplayHelpNext=true;
                TimestampLastCommand=0;
                IamAliveTimerActive=false;
				        break;
	  case RUN:   // Do the work.						
		      	    if (ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  //if (ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %d from TaskID %d (HostID %d) received.", MessageToProcess->Payload0, MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);

                  // Process the message, then delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING:            // Check if this process is still alive. Reply with a ECHO message type; return same message payload.
                                               TISM_PostmanTaskWriteMessage(ThisTask, MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, TISM_ECHO, MessageToProcess->Payload0, 0);
                                               break;
                    case TISM_ECHO:            // TISM_ECHO message received for our earlier TISM_PING message.
                                               TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Ping reply (TISM_ECHO) message received from HostID %d.", MessageToProcess->SenderHostID);
                                               break;
                    case RESET_DEBUGGING:      TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Resetting debug levels.");
                                               TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_NetMgr"), TISM_SET_TASK_DEBUG, DEBUG_NONE);                                 
                                               TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_UartMX"), TISM_SET_TASK_DEBUG, DEBUG_NONE);  
                                               break; 
                    case I_AM_ALIVE_TIMER:     TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "I am alive-message received.");
                                               break;                                             
                    default:                   // Other message type received, print it.
                                               TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %s-0x%02X from TaskID %d (HostID %d) received.", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                               break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }


// ============================== The menu system ==============================
                // Read a character from STDIN and start a scene on request.
                Character=getchar_timeout_us(0);    // Non-blocking read
                if(Character!=PICO_ERROR_TIMEOUT || DisplayHelpNext==true)
                {
                  char Option;

                  // Do we need to display the help function?
                  if (DisplayHelpNext==true)
                  {
                    Option='?';
                    DisplayHelpNext=false;
                    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                    
                  }
                  else
                  {
                    // Character available for reading.
                    Option=(char)(Character & 0xFF);
                    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "--> %c", Option);
                    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");
                  }

                  // Which submenu are we in?
                  switch(ActiveMenu)
                  {

// ============================== The System submenu ==============================
                    case SYSTEM_MENU:    // System menu
                                         switch (Option)
                                         {
                                           case 'I': // Initialize the system again
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing the system again, may have unexpected effects.");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Consider using 'R' for rebooting instead.");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Setting the system state to INIT.");
                                                     TISM_SchedulerSetSystemState(ThisTask, INIT);
                                                     break;
                                           case 'R': // Reboot the system
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Setting the system state to REBOOT.");
                                                     TISM_SchedulerSetSystemState(ThisTask, REBOOT);
                                                     break;
                                           case 'S': // Stop the system
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Setting the system state to STOP.");
                                                     TISM_SchedulerSetSystemState(ThisTask, STOP);
                                                     break;
                                           case 'H': // Perform an immediate, 'hard' reboot.
                                                     printf("-= Rebooting NOW =-\n"); 
                                                     fflush(stdout);
                                                     watchdog_reboot(0, 0, 1000);
                                                     while(1)
                                                     {
                                                       // Do nothing untill we reboot.
                                                     }
                                                     break;
                                           case 'Q': // Back to the main menu
                                                     ActiveMenu=MAIN_MENU;
                                                     DisplayHelpNext=true;
                                           default:  // Unknown option or '?' key pressed. Show options.  
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Main Menu > TISM system nenu:");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=============================");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Total memory: %lu Kbytes", TISM_GetTotalHeapBytes()/1024);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Free memory : %lu Kbytes", TISM_GetFreeHeapBytes()/1024);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Used memory : %lu Kbytes", (TISM_GetTotalHeapBytes() - TISM_GetFreeHeapBytes())/1024);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(I) - Initialize the system again");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(R) - Reboot the system");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(S) - Stop the system");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(H) - Perform a immediate/hard reboot");                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(Q) - Quit, go back");                                          
                                                     break;
                                         }
                                         break;

// ============================== The Network submenu ==============================                                         
                    case NETWORK_MENU:   // Switches menu
                                         switch (Option)
                                         {                                      
                                           case 'h': // Print a list of known hosts
                                                     {
                                                       RemoteHost Host;
                                                       uint8_t NumberOfRemoteHosts=TISM_NetworkManagerNumberOfRemoteHosts();
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Known hosts (%d):", NumberOfRemoteHosts);
                                                       for(uint8_t counter=0; counter<NumberOfRemoteHosts; counter++)
                                                       {
                                                         if(TISM_NetworkManagerDiscoverHost(ThisTask, counter, &Host))
                                                           TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Host %s (HostID %d), runs %d tasks, last seen %d minutes ago.", Host.Hostname, Host.HostID, Host.NumberOfTasks, Host.MinutesSinceLastSeen);
                                                       }
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Done.");
                                                     }
                                                     break;
                                           case 'D': // Dump a full list of all known hosts and tasks on the hosts
                                                     {
                                                       RemoteHost Host;
                                                       char TaskName[MAX_TASK_NAME_LENGTH+1];
                                                       uint8_t NumberOfRemoteHosts=TISM_NetworkManagerNumberOfRemoteHosts();
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Known hosts (%d):", NumberOfRemoteHosts);
                                                       for(uint8_t HostCounter=0; HostCounter<NumberOfRemoteHosts; HostCounter++)
                                                       {
                                                         if(TISM_NetworkManagerDiscoverHost(ThisTask, HostCounter, &Host))
                                                         {
                                                           TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Host %s (HostID %d), runs %d tasks, last seen %d minutes ago. Known tasks:", Host.Hostname, Host.HostID, Host.NumberOfTasks, Host.MinutesSinceLastSeen);
                                                           for(uint8_t TaskCounter=0; TaskCounter<Host.NumberOfTasks; TaskCounter++)
                                                           {
                                                             if(TISM_NetworkManagerResolveTaskID(ThisTask, Host.HostID, TaskCounter, &TaskName[0], false))
                                                             {
                                                               // Task name resolved successfully.
                                                               TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d, Taskname: %s", TaskCounter, TaskName);
                                                             }
                                                             else
                                                             {
                                                               // Task name not resolved yet.
                                                               TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID: %d, Taskname: <unknown>", TaskCounter);
                                                             }
                                                           }
                                                         }
                                                         else
                                                           TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "No task records found for host record %d.", HostCounter);

                                                       }
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Done.");
                                                     }
                                                     break;
                                           case 'r': // Review TISM_DISCOVER sequence
                                                     // First send a TISM_DISCOVER broadcast
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Sending TISM_DISCOVER message, enabling TISM_NetworkManager debugging for 10 seconds to view responses.");
                                                     TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, 0xFF, 0x00, TISM_DISCOVER, 0, 0, time_us_64());

                                                     // Enable logging for TISM_NetworkManager to be able to view the response.
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_NetMgr"), TISM_SET_TASK_DEBUG, DEBUG_LOW);

                                                     // Disable debugging again after 10 seconds.
                                                     TISM_SoftwareTimerSet(ThisTask, RESET_DEBUGGING, false, 10000);
                                                     break;                                                                                                          
                                           case 'u': // Enable TISM_UartMX logging.
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Enabling TISM_UartMX debug logging, press 'R' to stop.");
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_UartMX"), TISM_SET_TASK_DEBUG, DEBUG_LOW);
                                                     break;
                                           case 'n': // Enable TISM_NetworkManager logging.
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Enabling TISM_NetworkManager debug logging, press 'R' to stop.");
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_NetMgr"), TISM_SET_TASK_DEBUG, DEBUG_LOW);                                 
                                                     break;
                                           case 'R': // Reset debug levels
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Resetting debug logging levels.");
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_NetMgr"), TISM_SET_TASK_DEBUG, DEBUG_NONE);                                 
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_UartMX"), TISM_SET_TASK_DEBUG, DEBUG_NONE);                                 
                                                     break; 
                                           case 'p': // Setting HostID menu
                                                     ActiveMenu=PING_MENU;
                                                     DisplayHelpNext=true;
                                                     break;                                                                                                                         
                                           case 's': // Setting HostID menu
                                                     ActiveMenu=SET_HOSTID_MENU;
                                                     DisplayHelpNext=true; 
                                                     break;                                                                                                                                                                      
                                           case 'Q': // Back to the main menu
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Exiting Network menu; resetting debug logging levels.");
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_NetMgr"), TISM_SET_TASK_DEBUG, DEBUG_NONE);                                 
                                                     TISM_SchedulerSetTaskAttribute(ThisTask, TISM_GetTaskID("T_UartMX"), TISM_SET_TASK_DEBUG, DEBUG_NONE);  
                                                     ActiveMenu=MAIN_MENU;
                                                     DisplayHelpNext=true;
                                                     break;
                                           default:  // Unknown option or '?' key pressed. Show options.  
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Main menu > Network menu:");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=========================");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our HostID: %d",System.HostID);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our Hostname: %s", System.Hostname); 
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of detected remote hosts: %d", TISM_NetworkManagerNumberOfRemoteHosts());
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                                                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(h) - Display hosts list");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(D) - Dump full list of hosts and tasks");                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(r) - Refresh remote host list (TISM_DISCOVER)");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(u) - Enable TISM_UartMX debug logging");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(n) - Enable TISM_NetworkManager debug logging");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(R) - Reset logging");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(p) - Ping menu");                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(s) - Set HostID menu");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(Q) - Quit, go back");                                          
                                                     break;
                                         }
                                         break;

// ============================== The Ping submenu ==============================                                         
                    case PING_MENU:   // Switches menu
                                         switch (Option)
                                         {                                      
                                           case 'h': // Print a list of known hosts
                                                     {
                                                       RemoteHost Host;
                                                       uint8_t NumberOfRemoteHosts=TISM_NetworkManagerNumberOfRemoteHosts();
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Known hosts (%d):", NumberOfRemoteHosts);
                                                       for(uint8_t counter=0; counter<NumberOfRemoteHosts; counter++)
                                                       {
                                                         if(TISM_NetworkManagerDiscoverHost(ThisTask, counter, &Host))
                                                           TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "HostID %d: %s, runs %d tasks, last seen %d minutes ago.", Host.HostID, Host.Hostname, Host.NumberOfTasks, Host.MinutesSinceLastSeen);
                                                       }
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Done.");
                                                     }                                                     
                                                     break;
                                            case '1':
                                            case '2':
                                            case '3':
                                            case '4':
                                            case '5':
                                            case '6':
                                            case '7':
                                            case '8':
                                            case '9': // Ping the requested HostID
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Sending PING message to HostID %d.", Option-'0');
                                                      TISM_PostmanTaskWriteMessage(ThisTask, Option-'0', 0x00, TISM_PING, 0x12345678, 0x12345678);
                                                      break;                                                      
                                           case 'Q': // Back to the main menu
                                                     ActiveMenu=NETWORK_MENU;
                                                     DisplayHelpNext=true;
                                                     break;
                                           default:  // Unknown option or '?' key pressed. Show options.  
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Main menu > Ping menu:");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=========================");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our HostID: %d",System.HostID);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of detected remote hosts: %d.", TISM_NetworkManagerNumberOfRemoteHosts());
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                      
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(h) - Display hosts list");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(1-9) - Ping HostID 1-9");       
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                      
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(Q) - Quit, go back");                                          
                                                     break;
                                         }
                                         break;

// ============================== The Set HostID submenu ==============================                                         
                    case SET_HOSTID_MENU: // Switches menu
                                          switch (Option)
                                          {                                      
                                            case '1':
                                            case '2':
                                            case '3':
                                            case '4':
                                            case '5':
                                            case '6':
                                            case '7':
                                            case '8':
                                            case '9': // Manually setting the HostID
                                                      System.HostID=Option-'0';
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Changing our HostId to %d.", System.HostID);                                                     
                                                      break;                                                                                                                                                                                                
                                            case 'Q': // Back to the main menu
                                                      ActiveMenu=NETWORK_MENU;
                                                      DisplayHelpNext=true;
                                                      break;
                                            default:  // Unknown option or '?' key pressed. Show options.  
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Main menu > Set HostID menu:");
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "============================");
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Warning: changing the HostID of a running system");
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "         can have unexpected effects!");                                                                                                                                                                                                                      
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our HostID: %d",System.HostID);
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our Hostname: %s", System.Hostname);
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our Unique ID: %s", UniqueIDString);
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(1-9) - Set HostID");
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                                                     
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(Q) - Quit, go back");                                          
                                                      break;
                                          }
                                          break;                                             
                                         

// ============================== The Main menu (default) ==============================
                    default:             // Default is Main menu.
                                         switch(Option)
                                         {
                                           case 'n': // Activate Networking menu
                                                     ActiveMenu=NETWORK_MENU;
                                                     DisplayHelpNext=true;                                                     
                                                     break;
                                           case 't': // Print list of tasks on this system
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Tasks running on this system:");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "=============================");
                                                     for(uint8_t counter=0; counter<System.NumberOfTasks; counter++)
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d: %s %s", counter, System.Task[counter].TaskName, TISM_SchedulerIsTaskSleeping(System.Task[counter].TaskID) ? "(sleeping)" : "");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Done.");
                                                     break;  
                                           case 's': // Ask TISM_SoftwareTimer to log the list of active timers in the event log.
                                                     TISM_PostmanTaskWriteMessage(ThisTask, System.HostID, TISM_GetTaskID("T_SofTim"), TISM_DISPLAY_TIMERS, 0, 0);
                                                     break;                                                             
                                           case 'T': // Activate System menu
                                                     ActiveMenu=SYSTEM_MENU;
                                                     DisplayHelpNext=true;                                                     
                                                     break; 
                                           case 'i': // Toggle 'I am alive' timer
                                                     if(IamAliveTimerActive)
                                                     {
                                                       TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Disabling 'I am alive' timer.");
                                                       TISM_SoftwareTimerCancel(ThisTask, I_AM_ALIVE_TIMER);
                                                       IamAliveTimerActive=false;
                                                     }
                                                     else
                                                     {
                                                        TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Enabling 'I am alive' timer, will log a message every 30 seconds.");
                                                        TISM_SoftwareTimerSet(ThisTask, I_AM_ALIVE_TIMER, true, 30000);
                                                        IamAliveTimerActive=true;
                                                     }
                                                     break;                          
                                           default:  // Unknown option or '?' key pressed. Show options.               
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Main menu options:");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "==================");   
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our HostID: %d",System.HostID);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our Hostname: %s", System.Hostname);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Our Unique ID: %s", UniqueIDString);
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");                                                                     
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(n) - Networking menu");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(t) - Task list");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(s) - Scheduled timers list");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(i) - Toggle 'I am alive' timer");
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "(T) - TISM system menu");                                          
                                                     TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "");
                                                     break;
                                         }
                  } 
                  
                  // Show how long ago previous command was sent, helps with determining the position of the actuator and pulse widths.
                  // As we're using the messaging system there is some delay, but this should be reasonably constant.
                  if(TimestampLastCommand>0)
                    TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Previous command sent %d msecs ago.", (int)((time_us_64()-TimestampLastCommand)/1000));
                  TimestampLastCommand=time_us_64();                 
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

