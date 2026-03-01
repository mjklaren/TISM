/*

  TISM_NetworkManager.c
  =====================
  Task responsible for managing the network 'neighborhood' in the TISM-system.

  Copyright (c) 2026 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <string.h>
#include <stdlib.h>
#include "TISM.h"


#ifndef TISM_DISABLE_UARTMX                                  // In case UartMX is disabled in TISM.h, to prevent compilation errors.

#define T_NETMGR_REVIEW_LIST	      0x64	  	               // (100) For scheduling, command triggering review the remote host linear list.
#define T_NETMGR_INTRODUCE          0x65                     // (101) Command triggering transmission of beacon.
#define T_NETMGR_INTRODUCE_PERIOD   2                        // In minutes, period between TISM_INTRODUCE broadcasts.
#define T_NETMGR_HOST_REFRESH       3                        // In minutes, period between linear list reviews.
#define T_NETMGR_HOST_REACHOUT      T_NETMGR_HOST_REFRESH*3  // In minutes, period after which remote host is requested to update their data when not 'seen' earlier.
#define T_NETMGR_HOST_EXPIRE        T_NETMGR_HOST_REFRESH*4  // In minutes, period after which host data expires when not updated (=host unavailable) and is removed.
#define T_NETMGR_BROADCAST_PERIOD   2500000                  // In usec, period between broadcasts in the TaskID broadcasting sequence.
#define T_NETMGR_DISCOVER_TIMEOUT   3000000                  // In usec, time to wait for initiating new discovery when there is already network discovery activity.
                                                             // Should be longer than T_NETMGR_BROADCAST_PERIOD.                                                             

static RemoteHost *Network;                                  // Our network neighborhood, starting point for a linear list.
static uint8_t NumberOfRemoteHosts;                          // Number of remote hosts discovered in our neighborhood.
static mutex_t NetworkMutex;                                 // Mutex for thread-safe access to the network list.

// Used for broadcasting our own tasks.
uint64_t TaskBroadcastTimer;                                 // Virtual timer for scheduling task broadcasts.
uint8_t TaskBroadcasting;                                    // TaskID we're broadcasting.

// To manage the activity on the network.
uint64_t DiscoveryOnTheNetworkTimer;                         // Timer used to stop new discovery/beacon activities when the network is active.


/*
  Description:
  Frees all tasks for a given host. Helper function for cleanup.
  Must be called BEFORE freeing the host itself.

  Parameters:
  uint8_t Host - The RemoteHost whose tasks should be freed

  Return value:
  uint8_t      - Number of tasks that were freed
*/
static uint8_t TISM_NetworkManagerFreeHostTasks(RemoteHost *Host)
{
  if(Host==NULL)
    return 0;
  uint8_t TasksFreed=0;
  RemoteTask *CurrentTask=Host->NextTask;
  while (CurrentTask!=NULL)
  {
    RemoteTask *TaskToRemove=CurrentTask;
    CurrentTask=CurrentTask->NextTask;
    free(TaskToRemove);
    TasksFreed++;
  }
  Host->NextTask=NULL;  // Clear the pointer
  return TasksFreed;
}


/*
  Description:
  Returns the number of remote hosts currently registered in the network list.
  Thread-safe.

  Parameters:
  None

  Return value:
  Number of remote hosts (0-255)
*/
uint8_t TISM_NetworkManagerNumberOfRemoteHosts(void)
{
    uint8_t Count;
    
    // Lock mutex for thread-safe access
    mutex_enter_blocking(&NetworkMutex);
    Count=NumberOfRemoteHosts;
    mutex_exit(&NetworkMutex); // Unlock mutex
    return Count;
}


/*
  Description:
  Resolves a HostID to a RemoteHost struct. Thread-safe.
  Copies the host data from the network list to the provided RemoteHost pointer.
  When HostID is not found, sends a general TISM_DISCOVER broadcast (empty payload).

  Parameters:
  TISM_Task ThisTask - Struct containing (among other things) the OutboundMessageQueue
  uint8_t HostID     - The HostID to resolve (1-254)
  RemoteHost *Host   - Pointer to RemoteHost struct where data will be copied
  bool ContactHost   - Whether to contact the host if the requested HostID cannot be resolved.

  Return value:
  - true             - HostID found, data copied to Host pointer
  - false            - HostID not found in network list (general TISM_DISCOVER broadcast sent)
*/
bool TISM_NetworkManagerResolveHostID(TISM_Task ThisTask, uint8_t HostID, RemoteHost *Host, bool ContactHost)
{
    bool HostFound=false;
    
    // Validate parameters
    if(Host==NULL || HostID==0x00 || HostID==UARTMX_BROADCAST)
        return false;
    
    // Lock mutex for thread-safe access
    mutex_enter_blocking(&NetworkMutex);
    
    // Search through the linear list
    RemoteHost *CurrentHost=Network;
    while (CurrentHost!=NULL)
    {
      if(CurrentHost->HostID==HostID)
      {
        // Found it - copy all data to the provided struct
        Host->HostID=CurrentHost->HostID;
        Host->NumberOfTasks=CurrentHost->NumberOfTasks;
        Host->MinutesSinceLastSeen=CurrentHost->MinutesSinceLastSeen;
        Host->ReachedOut=CurrentHost->ReachedOut;
        strncpy(Host->Hostname, CurrentHost->Hostname, MAX_TASK_NAME_LENGTH);
        Host->Hostname[MAX_TASK_NAME_LENGTH]='\0';
        Host->NextHost=NULL;  // Don't expose internal list structure
        Host->NextTask=NULL;  // Don't expose internal list structure
        HostFound=true;

        if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "HostID %d resolved to hostname '%s'.", HostID, Host->Hostname);
        break;
      }
      CurrentHost=CurrentHost->NextHost;
    }
    
    // Unlock mutex
    mutex_exit(&NetworkMutex);
    
    // If host not found, send general TISM_DISCOVER broadcast (empty payload)
    if(!HostFound && ContactHost)
    {
      // Send TISM_DISCOVER broadcast with empty payload (general discovery)
      // All hosts in the network will respond with TISM_INTRODUCE
      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "HostID %d not found, sending TISM_DISCOVER broadcast.", HostID);

      TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, UARTMX_BROADCAST, 0x00, TISM_DISCOVER, 0, 0, time_us_64());
    }
    return HostFound;
}


/*
  Description:
  Resolves a Hostname to a HostID using the network list. Thread-safe.
  When hostname is not found, sends a TISM_DISCOVER broadcast with the requested 
  hostname as payload (max 8 characters). Only the host with matching hostname will respond.

  Parameters:
  TISM_Task ThisTask  - Struct containing (among other things) the OutboundMessageQueue
  const char Hostname - The hostname to resolve (null-terminated string, max MAX_TASK_NAME_LENGTH chars)
  bool ContactHost    - Whether to contact the host if the requested HostID cannot be resolved.


  Return value:
  uint 8_t HostID     - (1-254) if found, 0 if hostname not found in network list (targeted TISM_DISCOVER broadcast sent)
*/
uint8_t TISM_NetworkManagerResolveHostname(TISM_Task ThisTask, const char *Hostname, bool ContactHost)
{
  uint8_t ResolvedHostID=0;
  
  // Validate parameter
  if(Hostname==NULL)
      return 0;
  
  // Lock mutex for thread-safe access
  mutex_enter_blocking(&NetworkMutex);
  
  // Search through the linear list
  RemoteHost *CurrentHost=Network;
  while (CurrentHost!=NULL)
  {
    if(strncmp(CurrentHost->Hostname, Hostname, MAX_TASK_NAME_LENGTH)==0)
    {
      // Found it - return the HostID
      ResolvedHostID=CurrentHost->HostID;

      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Hostname '%s' resolved to HostID %d.", Hostname, ResolvedHostID);
      break;
    }
    CurrentHost=CurrentHost->NextHost;
  }
  
  // Unlock mutex
  mutex_exit(&NetworkMutex);
  
  // If hostname not found, send TISM_DISCOVER broadcast with hostname as payload
  if(ResolvedHostID==0 && ContactHost)
  {
    // Convert hostname to payloads (max 8 characters)
    uint32_t Payload0, Payload1;
    TISM_StringToPayloads((char *)Hostname, &Payload0, &Payload1);
    
    // Send TISM_DISCOVER broadcast with hostname as payload (targeted discovery). Only the host with matching hostname will respond with TISM_INTRODUCE.
    // As this is only one outgoing message and one reply, we allow this without delays.
    if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Hostname '%s' not found, sending TISM_DISCOVER broadcast.", Hostname);
    
    TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, UARTMX_BROADCAST, 0x00, TISM_DISCOVER, Payload0, Payload1, time_us_64());
  }
  return ResolvedHostID;
}


/*
  Description:
  Resolves a TaskID running on a remote host to a task name. Thread-safe. Searches through the task list of the specified host.
  When task not found, sends TISM_RESOLVE_TASKID command asking remote host to resolve the requested TaskID.

  Parameters:
  TISM_Task ThisTask - Struct containing OutboundMessageQueue
  uint8_t HostID     - The remote HostID where the task runs (1-254)
  uint8_t TaskID     - The TaskID to resolve on the remote host (0-254)
  char *TaskName     - Buffer to store the resolved task name (must be MAX_TASK_NAME_LENGTH+1 bytes)
  bool ContactHost   - Whether to contact the host if the requested HostID cannot be resolved.


  Return value:
  bool               - true: TaskID resolved, TaskName filled; false: TaskID not found, discovery request sent - check again later
*/
bool TISM_NetworkManagerResolveTaskID(TISM_Task ThisTask, uint8_t HostID, uint8_t TaskID, char *TaskName, bool ContactHost)
{
    bool TaskFound=false;
    
    // Validate parameters
    if(TaskName==NULL || HostID==0x00 || HostID==UARTMX_BROADCAST || HostID==System.HostID || TaskID==UARTMX_BROADCAST)
      return false;
    
    // Initialize output
    TaskName[0]='\0';
    
    // Lock mutex for thread-safe access
    mutex_enter_blocking(&NetworkMutex);
    
    // First, find the host
    RemoteHost *CurrentHost=Network;
    while (CurrentHost!=NULL)
    {
      if(CurrentHost->HostID==HostID)
      {
          // Host found - now search through its tasks
          RemoteTask *CurrentTask=CurrentHost->NextTask;
          while (CurrentTask!=NULL)
          {
            if(CurrentTask->TaskID==TaskID)
            {
              // Task found - copy name
              strncpy(TaskName, CurrentTask->TaskName, MAX_TASK_NAME_LENGTH);
              TaskName[MAX_TASK_NAME_LENGTH]='\0';
              TaskFound=true;

              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d on HostID %d resolved to task name '%s'.", TaskID, HostID, TaskName);
              break;
            }
            CurrentTask=CurrentTask->NextTask;
          }
          break;
        }
        CurrentHost=CurrentHost->NextHost;
    }
    
    // Unlock mutex
    mutex_exit(&NetworkMutex);
    
    // If task not found, send command to remote host to resolve the requested TaskID.
    if(!TaskFound && ContactHost)
    {
      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d not found for HostID %d, requesting remote host to resolve task ID.", TaskID, HostID);
      
      // As this is only one outgoing message and one reply, we allow this without delays.
      TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, HostID, 0x00, TISM_RESOLVE_TASKID, TaskID, 0x00, time_us_64());    
    }
    return TaskFound;
}


/*
  Description:
  Resolves a task name on a remote host to a TaskID. Thread-safe. Searches through the task list of the specified host.
  When task not found, sends TISM_RESOLVE_TASKNAME command asking remote host to resolve the requested task name.

  Parameters:
  TISM_Task ThisTask   - Struct containing OutboundMessageQueue
  uint8_t HostID       - The remote HostID where the task runs (1-254)
  char *TaskName       - Pointer to the task name to resolve (null-terminated, max MAX_TASK_NAME_LENGTH chars)
  bool ContactHost     - Whether to contact the host if the requested HostID cannot be resolved.


  Return value:
  uint8_t              - TaskID (0-254) if found, 0xFF (255) if task name not found, discovery request sent - check again later

  Note: if the function caller makes the mistake of using our own HostID for the lookup, TISM_GetTaskID is called. 
        But when not found, TaskID=0x00 is returned instead of 0xFF!
*/
uint8_t TISM_NetworkManagerResolveTaskname(TISM_Task ThisTask, uint8_t HostID, char *TaskName, bool ContactHost)
{
    uint8_t ResolvedTaskID=0xFF;  // 0xFF=not found
    
    // Validate parameters
    if(TaskName==NULL || HostID==0x00 || HostID==System.HostID || HostID==UARTMX_BROADCAST)
    {
      // Being helpfull to the caller; if the HostID is ours, use TISM_GetTaskID.
      if(HostID==System.HostID)
        return(TISM_GetTaskID(TaskName));
      else
        return 0xFF;
    }
       
    // Lock mutex for thread-safe access
    mutex_enter_blocking(&NetworkMutex);
    
    // First, find the host
    RemoteHost *CurrentHost=Network;
    while(CurrentHost!=NULL)
    {
      if(CurrentHost->HostID==HostID)
      {
        // Host found - now search through its tasks
        RemoteTask *CurrentTask=CurrentHost->NextTask;
        while (CurrentTask!=NULL)
        {
          if(strncmp(CurrentTask->TaskName, TaskName, MAX_TASK_NAME_LENGTH)==0)
          {
              // Task found - return TaskID
              ResolvedTaskID=CurrentTask->TaskID;

              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task name '%s' on HostID %d resolved to TaskID %d.", TaskName, HostID, ResolvedTaskID);
              break;
          }
          CurrentTask=CurrentTask->NextTask;
        }
        break;
      }
      CurrentHost=CurrentHost->NextHost;
    }
    
    // Unlock mutex
    mutex_exit(&NetworkMutex);
    
    // If task not found, send TISM_RESOLVE_TASKNAME command to remote host.
    if(ResolvedTaskID==0xFF && ContactHost)
    {
      // As this is only one outgoing message and one reply, we allow this without delays.
      uint32_t Payload0, Payload1;
      TISM_StringToPayloads(TaskName, &Payload0, &Payload1);
      TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, HostID, 0x00, TISM_RESOLVE_TASKNAME, Payload0, Payload1, time_us_64());    

      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task name '%s' on HostID %d not found, asking remote host to resolve task name.", TaskName, HostID);    
    }
    return ResolvedTaskID;
}


/*
  Description:
  Discovers a host by sequence number (position in the linear list). Thread-safe. Used to iterate through the network list.

  Parameters:
  TISM_Task ThisTask     - Struct containing (among other things) the OutboundMessageQueue
  uint8_t SequenceNumber - Position in the list (0 = first host, 1 = second host, etc.)
  RemoteHost *Host       - Pointer to RemoteHost struct where data will be copied

  Return value:
  true                   - Host found at this position, data copied to Host pointer
  false                  - No host at this position (sequence number out of range)
*/
bool TISM_NetworkManagerDiscoverHost(TISM_Task ThisTask, uint8_t SequenceNumber, RemoteHost *Host)
{
  bool HostFound=false;
  
  // Validate parameter
  if(Host==NULL)
    return false;
  
  // Lock mutex for thread-safe access
  mutex_enter_blocking(&NetworkMutex);
  
  // Walk through the linear list to find the host at the specified position
  RemoteHost *CurrentHost=Network;
  uint8_t CurrentPosition=0;    
  while (CurrentHost!=NULL)
  {
    if(CurrentPosition==SequenceNumber)
    {
      // Found it - copy all data to the provided struct
      Host->HostID=CurrentHost->HostID;
      Host->NumberOfTasks=CurrentHost->NumberOfTasks;
      Host->MinutesSinceLastSeen=CurrentHost->MinutesSinceLastSeen;
      Host->ReachedOut=CurrentHost->ReachedOut;
      strncpy(Host->Hostname, CurrentHost->Hostname, MAX_TASK_NAME_LENGTH);
      Host->Hostname[MAX_TASK_NAME_LENGTH]='\0';
      Host->NextTask=NULL;    // Don't expose internal list structure
      Host->NextHost=NULL;    // Don't expose internal list structure
      HostFound=true;

      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Host at position %d: HostID %d, hostname '%s'.", SequenceNumber, Host->HostID, Host->Hostname);
      break;
    }
    CurrentPosition++;
    CurrentHost=CurrentHost->NextHost;
  }
  
  // Unlock mutex
  mutex_exit(&NetworkMutex);
  return HostFound;
}


/*
  Description:
  This is the function that is registered in the TISM-system via the TISM_RegisterTask. A pointer to this function is used.
  For debugging purposes the TISM_EventLoggerLogEvent-function is used (not mandatory).

  Parameters:
  TISM_Task ThisTask      - Struct containing all relevant information for this task to run. This is provided by the scheduler.
  
  Return value:
  <non-zero value>        - Task returned an error when executing. A non-zero value will stop the system.
  OK                      - Run succesfully completed.
*/
uint8_t TISM_NetworkManager (TISM_Task ThisTask)
{
  if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run starting.");

  switch(ThisTask.TaskState)   
  {
    case INIT:  // Activities to initialize this task (e.g. initialize ports or peripherals).
                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Initializing with priority %d.", ThisTask.TaskPriority);
				        
                // Initialize variables
                Network=NULL;
                NumberOfRemoteHosts=0;
                mutex_init(&NetworkMutex);    // Initialize mutex for thread-safe access to network list
                TaskBroadcasting=0;
                TaskBroadcastTimer=0; 
                DiscoveryOnTheNetworkTimer=0;

                // Schedule a repeating task to send out TISM_INTRODUCE packets to the network.
                TISM_SoftwareTimerSet(ThisTask, T_NETMGR_INTRODUCE, true, T_NETMGR_INTRODUCE_PERIOD*60000);

                // Schedule a repeating task to run through network neighborhood list
                TISM_SoftwareTimerSet(ThisTask, T_NETMGR_REVIEW_LIST, true, T_NETMGR_HOST_REFRESH*60000);

                // Schedule a single task to send TISM_INTRODUCE at startup, with a delay based on our UniqueID to prevent all hosts from sending at the same time.
                uint32_t StartupDelay=System.UniqueID & 0x3F;                                   // Use last 6 bits of UniqueID for startup delay (0-63 seconds)
                TISM_SoftwareTimerSet(ThisTask, T_NETMGR_INTRODUCE, false, StartupDelay*1000);  // Schedule single task with delay in milliseconds 

                // For tasks that only respond to events (=messages) we can set the sleep attribute to ´true'.
                TISM_SchedulerSetMyTaskAttribute(ThisTask, TISM_SET_TASK_SLEEP, true);
				        break;
	  case RUN:   // Do the work.						
		      	    if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Doing work with priority %d on core %d.", ThisTask.TaskPriority, ThisTask.RunningOnCoreID);

                // First check for incoming messages and process them.
                uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent (ThisTask, TISM_LOG_EVENT_NOTIFY, "Message '%ld' type %s-0x%02X from TaskID %d (HostID %d) received, addressed to TaskID %d (HostID %d).", MessageToProcess->Payload0, TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID, MessageToProcess->RecipientTaskID, MessageToProcess->RecipientHostID);

                  // Who did we receive a message from? Reset the last seen timestamp for this host in our network list.
                  // Note; only reception of discovery-related messages (routed to TISM_NetworkManager) will reset the timestamp.
                  RemoteHost *CurrentHost=Network, *PreviousHost=NULL;
                  mutex_enter_blocking(&NetworkMutex);
                  while (CurrentHost!=NULL)
                  {
                    if(CurrentHost->HostID==MessageToProcess->SenderHostID)
                    {
                      CurrentHost->MinutesSinceLastSeen=0;  // Reset last seen timestamp for this host

                      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Last seen timestamp for HostID %d reset.", MessageToProcess->SenderHostID);
                      break;
                    }
                    PreviousHost=CurrentHost;
                    CurrentHost=CurrentHost->NextHost;
                  }
                  mutex_exit(&NetworkMutex); 

                  // Processed the message; then delete it.
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_TEST:                   // Test packet, no action to take. Just enter a log entry.
                                                      TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_TEST message received from TaskID %d (HostID %d). No action taken.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                      break;                    
                    case TISM_PING:                   // Check if this host is still alive. Reply with a ECHO message type; return same message payload.
                                                      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_PING request received from HostID %d, TaskID %d. Sending TISM_ECHO message.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                     
                                                      TISM_PostmanTaskWriteMessage(ThisTask, MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, TISM_ECHO, MessageToProcess->Payload0, 0);
                                                      break;
                    case TISM_INTRODUCE:              // A host is introducing itself to us
                                                      {
                                                        if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_INTRODUCE message received from HostID %d, TaskID %d. Validating this host in our network list.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                        
                                                        char ReceivedHostname[MAX_TASK_NAME_LENGTH+1];
                                                        TISM_PayloadsToString(MessageToProcess->Payload0, MessageToProcess->Payload1, &ReceivedHostname[0]);
                                                        
                                                        // Check if this host is already registered in our network list
                                                        CurrentHost=Network; 
                                                        PreviousHost=NULL;
                                                        bool HostFound=false;

                                                        // Store host in the linear list; apply a mutex to prevent multi-thread issues.
                                                        mutex_enter_blocking(&NetworkMutex);
                                                        while (CurrentHost!=NULL)
                                                        {
                                                          if(CurrentHost->HostID==MessageToProcess->SenderHostID)
                                                          {
                                                            // Host already registered; last seen timestamp is already reset.
                                                            // SenderTaskID contains NumberOfTasks in a TISM_INTRODUCE message. Do the number of tasks on the host differ from what we've registered earlier? 
                                                            if(CurrentHost->NumberOfTasks!=MessageToProcess->SenderTaskID)
                                                            {
                                                              // Yes, update the registered number of tasks and request the sender to report all its tasks.
                                                              CurrentHost->NumberOfTasks=MessageToProcess->SenderTaskID; 

                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Updated number of tasks detected for HostID %d.", CurrentHost->HostID);

                                                              // Are we allowed to start a discovery sequence? Check the status of the timer.
                                                              if(TISM_SoftwareTimerVirtualExpired(DiscoveryOnTheNetworkTimer))
                                                              {
                                                                TISM_PostmanTaskWriteMessage(ThisTask, MessageToProcess->SenderHostID, 0x00, TISM_RESOLVE_TASKID, 0xFF, 0);
                                                                
                                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Requesting list of known tasks.");
                                                              }  
                                                            }
                                                            CurrentHost->ReachedOut=false;

                                                            // Check if the sent hostname and recorded hostname differ. If so, there might be duplicate HostID's in use in the network.
                                                            if(strncmp(CurrentHost->Hostname, ReceivedHostname, MAX_TASK_NAME_LENGTH)!=0)
                                                            {
                                                              // Different hostname than expected, generate error message. But changes are still recorded.
                                                              TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Hostname mismatch detected, duplicate HostID's in the network? Recorded %s, received %s for HostID %d. Updating network list anyway.", CurrentHost->Hostname, ReceivedHostname, CurrentHost->HostID);
                                                            }
                                                            strncpy(CurrentHost->Hostname, ReceivedHostname, MAX_TASK_NAME_LENGTH);
                                                            CurrentHost->Hostname[MAX_TASK_NAME_LENGTH]='\0';
                                                            HostFound=true;
                                                            
                                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Data for HostID %d (%s) updated in network list.", CurrentHost->HostID, CurrentHost->Hostname);
                                                            break;
                                                          }
                                                          PreviousHost=CurrentHost;
                                                          CurrentHost=CurrentHost->NextHost;
                                                        }
                                                        
                                                        // If not found, add new host to the linear list
                                                        if(!HostFound)
                                                        {
                                                          RemoteHost *NewHost=(RemoteHost *)malloc(sizeof(RemoteHost));
                                                          if(NewHost!=NULL)
                                                          {
                                                            NewHost->HostID=MessageToProcess->SenderHostID;
                                                            NewHost->NumberOfTasks=MessageToProcess->SenderTaskID;               // SenderTaskID contains NumberOfTasks in a TISM_INTRODUCE message
                                                            NewHost->MinutesSinceLastSeen=0;
                                                            strncpy(NewHost->Hostname, ReceivedHostname, MAX_TASK_NAME_LENGTH);
                                                            NewHost->Hostname[MAX_TASK_NAME_LENGTH]='\0';
                                                            NewHost->ReachedOut=false;
                                                            NewHost->NextTask=NULL;
                                                            NewHost->NextHost=NULL;
                                                            
                                                            // Add to the linear list
                                                            if(Network==NULL)
                                                              Network=NewHost;                 // First host in the list
                                                            else
                                                              PreviousHost->NextHost=NewHost;  // Append to the end of the list
                                                            NumberOfRemoteHosts++;

                                                            // New host, request the sender to report all its tasks.
                                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "New host %d (%s) registered in network list, %d in total.", NewHost->HostID, NewHost->Hostname, NumberOfRemoteHosts);

                                                            // Are we allowed to start a discovery sequence? Check the status of the timer.
                                                            if(TISM_SoftwareTimerVirtualExpired(DiscoveryOnTheNetworkTimer))
                                                            {
                                                              TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, MessageToProcess->SenderHostID, 0x00, TISM_RESOLVE_TASKID, 0xFF, 0x00, time_us_64());
                                                            
                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Requesting list of known tasks.");
                                                            }
                                                          }
                                                          else
                                                          {
                                                            // Memory allocation failed
                                                            TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Failed to allocate memory to register new host %d.", MessageToProcess->SenderHostID);
                                                          }
                                                        }

                                                        // Release the mutex
                                                        mutex_exit(&NetworkMutex);

                                                        // Set a timer to prevent new discovery/beacon activities for a while.
                                                        DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT);
                                                      }  
                                                      break;
                    case TISM_DISCOVER:               // A host is looking for other hosts on the network; do we need to reply?
                                                      {
                                                        if((MessageToProcess->RecipientHostID==UARTMX_BROADCAST && MessageToProcess->Payload0==0x00) ||                                         // Broadcast without specifying a hostname.
                                                           (MessageToProcess->RecipientHostID==UARTMX_BROADCAST && strncmp((char *)&MessageToProcess->Payload0, &System.Hostname[0], MAX_TASK_NAME_LENGTH)==0) || // Broadcast with a hostname that matches ours.
                                                           (MessageToProcess->RecipientHostID==System.HostID))                                                                                  // Our HostID directly addressed.
                                                        {
                                                          // This is a discovery request we should reply to, despite the status of the DiscoveryOnTheNetworkTimer.
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_DISCOVER request received from HostID %d. Returning TISM_INTRODUCE broadcast.", MessageToProcess->SenderHostID);
                                                        
                                                          // Reply with a TISM_INTRODUCE broadcast.
                                                          uint32_t Payload0, Payload1;
                                                          TISM_StringToPayloads(&System.Hostname[0], &Payload0, &Payload1);
                                                          TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, System.NumberOfTasks, UARTMX_BROADCAST, 0x00, TISM_INTRODUCE, Payload0, Payload1, time_us_64());

                                                          // Set a timer to prevent new discovery/beacon activities for a while.
                                                          DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT); 
                                                        }                                                       
                                                      }
                                                      break;      
                    case TISM_RESOLVE_TASKNAME:       // A host is requesting the TaskID for a specific TaskName.
                                                      {
                                                        // Make sure a payload is provided; otherwise ignore the request.
                                                        if(MessageToProcess->Payload0!=0)
                                                        {
                                                          char TaskNameToResolve[MAX_TASK_NAME_LENGTH+1];
                                                          TISM_PayloadsToString(MessageToProcess->Payload0, MessageToProcess->Payload1, &TaskNameToResolve[0]);

                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_RESOLVE_TASKNAME request received from HostID %d, TaskID %d for task name '%s'.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, TaskNameToResolve);
                                                          
                                                          // Lookup the task name and reply with the TaskID.
                                                          uint8_t TaskIDResolved=0xFF;
                                                          for(uint8_t TaskCounter=0; TaskCounter<System.NumberOfTasks; TaskCounter++)
                                                          {
                                                            if(strncmp(System.Task[TaskCounter].TaskName, TaskNameToResolve, MAX_TASK_NAME_LENGTH)==0)
                                                            {
                                                              // Task found.
                                                              TaskIDResolved=TaskCounter;
                                                            }
                                                          }

                                                          if(ThisTask.TaskDebug)
                                                          {
                                                              if(TaskIDResolved!=0xFF)
                                                                TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task name '%s' resolved to TaskID %d.", TaskNameToResolve, TaskIDResolved);
                                                              else
                                                                TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task name '%s' not found on this host.", TaskNameToResolve); 
                                                          }

                                                          // Reply with the resolved TaskID (in SenderTaskID field), original task name in payload.
                                                          // As this is only one outgoing message and one reply, we allow this without considering the DiscoveryOnTheNetworkTimer.
                                                          TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, TaskIDResolved, UARTMX_BROADCAST, 0x00, TISM_RESOLVE_TASKNAME_REPLY, MessageToProcess->Payload0, MessageToProcess->Payload1, time_us_64());
                                                        }
                                                        else
                                                        {
                                                          // Message received, but no payload. Log this, but ignore the request.
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_RESOLVE_TASKNAME request received from HostID %d, TaskID %d with empty payload. Ignoring.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                        }                                                           
                                                      }   
                                                      break;
                    case TISM_RESOLVE_TASKNAME_REPLY: {
                                                        // A host is replying to our or another host's TISM_RESOLVE_TASKNAME request.
                                                        // Was the request resolved succesfully? Then extract task name from payload.

                                                        if(MessageToProcess->SenderTaskID!=0xFF)  // 0xFF=requested TaskID not found.
                                                        {
                                                          // TaskID resolved successfully by remote host; extract task name from payload.
                                                          char ReceivedTaskName[MAX_TASK_NAME_LENGTH+1];
                                                          TISM_PayloadsToString(MessageToProcess->Payload0, MessageToProcess->Payload1, &ReceivedTaskName[0]);
                                                          
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_RESOLVE_TASKNAME_REPLY received from HostID %d: TaskID is %d, task name is %s.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, ReceivedTaskName);

                                                          // Store task in the host's task list; apply a mutex to prevent multi-thread issues.
                                                          mutex_enter_blocking(&NetworkMutex);
                                                          
                                                          // Find the host
                                                          CurrentHost=Network;
                                                          while (CurrentHost!=NULL)
                                                          {
                                                            if(CurrentHost->HostID==MessageToProcess->SenderHostID)
                                                            {
                                                              // Host found - check if task already in list
                                                              RemoteTask *CurrentTask=CurrentHost->NextTask;
                                                              RemoteTask *PreviousTask=NULL;
                                                              bool TaskExists=false;
                                                              
                                                              while (CurrentTask!=NULL && TaskExists==false)
                                                              {
                                                                if(CurrentTask->TaskID==MessageToProcess->SenderTaskID)
                                                                {
                                                                  // Task already exists - update name
                                                                  strncpy(CurrentTask->TaskName, ReceivedTaskName, MAX_TASK_NAME_LENGTH);
                                                                  CurrentTask->TaskName[MAX_TASK_NAME_LENGTH]='\0';
                                                                  TaskExists=true;

                                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Network list updated.");                                                                  
                                                                }
                                                                PreviousTask=CurrentTask;
                                                                CurrentTask=CurrentTask->NextTask;
                                                              }
                                                              
                                                              // If task doesn't exist, create new entry (but only if payload is not empty; = task found)
                                                              if(!TaskExists && MessageToProcess->Payload0!=0)
                                                              {
                                                                RemoteTask *NewTask=(RemoteTask *)malloc(sizeof(RemoteTask));
                                                                if(NewTask!=NULL)
                                                                {
                                                                  NewTask->TaskID=MessageToProcess->SenderTaskID;
                                                                  strncpy(NewTask->TaskName, ReceivedTaskName, MAX_TASK_NAME_LENGTH);
                                                                  NewTask->TaskName[MAX_TASK_NAME_LENGTH]='\0';
                                                                  NewTask->NextTask=NULL;
                                                                  
                                                                  // Add to list; either the list is empty, or we add to the end of the list. 
                                                                  // TaskID's are not necessarily received in order, so we don't assume that a new task should be added at the end of the list.
                                                                  if(CurrentHost->NextTask==NULL)
                                                                    CurrentHost->NextTask=NewTask;
                                                                  else
                                                                    PreviousTask->NextTask=NewTask;
                                                                  
                                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Record created in network list.");                                                                  
                                                                }
                                                                else
                                                                {
                                                                  // Memory allocation failed
                                                                  TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Failed to allocate memory to register new task %d for host %d.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                                }
                                                              }
                                                              break;
                                                            }
                                                            CurrentHost=CurrentHost->NextHost;
                                                          }                                                    
                                                          mutex_exit(&NetworkMutex);
                                                        }
                                                        else
                                                        {
                                                          // TaskID not resolved, task name not found on remote host. Log this.                                                          
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_RESOLVE_TASKNAME_REPLY broadcasted by HostID %d: requested TaskID was %d, but not found on this host.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                        }   
                                                      }
                                                      break;
                    case TISM_RESOLVE_TASKID:         // A host is requesting the task name for a specific TaskID
                                                      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TISM_RESOLVE_TASKID request received from HostID %d for TaskID %d.", MessageToProcess->SenderHostID, (uint8_t)MessageToProcess->Payload0);

                                                      // Is the request for a specific TaskID, or for all TaskID's on this host (0xFF)? 
                                                      // When 0xFF, we start a sequence of broadcasting all TaskID-TaskName pairs on this host, unless already running.
                                                      if(MessageToProcess->Payload0==0xFF)
                                                      {
                                                        if(TaskBroadcastTimer==0)
                                                        {
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Broadcast request for all TaskID-Taskname pairs received from HostID %d, starting sequence.", MessageToProcess->SenderHostID);
                                                        
                                                          TaskBroadcasting=0;
                                                          TaskBroadcastTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_BROADCAST_PERIOD);
                                                        }
                                                        else
                                                        {
                                                          // We're already running a broadcast sequence, ignore this request.
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Broadcast request for all TaskID-Taskname pairs received from HostID %d, but broadcast already in progress. Ignoring.", MessageToProcess->SenderHostID);
                                                        }
                                                      }
                                                      else
                                                      {
                                                        // Is the supplied TaskID a valid TaskID on this host? If so, lookup the TaskID and reply with the TaskName.
                                                        if(TISM_IsValidTaskID((uint8_t)MessageToProcess->Payload0))
                                                        {
                                                          uint32_t Payload0, Payload1;
                                                          TISM_StringToPayloads(System.Task[(uint8_t)MessageToProcess->Payload0].TaskName, &Payload0, &Payload1);
                                                          TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, (uint8_t)MessageToProcess->Payload0, UARTMX_BROADCAST, 0x00, TISM_RESOLVE_TASKID_REPLY, Payload0, Payload1, time_us_64());

                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Returning TaskName '%s' for TaskID %d.", System.Task[(uint8_t)MessageToProcess->Payload0].TaskName, (uint8_t)MessageToProcess->Payload0);
                                                        }
                                                        else
                                                        {
                                                          // Invalid TaskID requested; reply with empty payload to indicate that the TaskID isn't found - but only when not responding to a broadcast.
                                                          if(MessageToProcess->RecipientHostID!=UARTMX_BROADCAST)
                                                          {
                                                            TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, UARTMX_BROADCAST, 0x00, TISM_RESOLVE_TASKID_REPLY, 0, 0, time_us_64());
                                                            
                                                            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Invalid TaskID %d requested by HostID %d. Replying with empty payload.", (uint8_t)MessageToProcess->Payload0, MessageToProcess->SenderHostID);
                                                          }
                                                        }                                                          
                                                      }  
                                                      break;
                    case TISM_RESOLVE_TASKID_REPLY:   // A host is replying to a RESOLVE_TASKID request, either from us or another host. 
                                                      // Extract task name from payload
                                                      char ReceivedTaskName[MAX_TASK_NAME_LENGTH+1];
                                                      TISM_PayloadsToString(MessageToProcess->Payload0, MessageToProcess->Payload1, &ReceivedTaskName[0]);
                                                      
                                                      if(ThisTask.TaskDebug)
                                                      {
                                                        if(MessageToProcess->Payload0!=0)
                                                          TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "RESOLVE_TASKID_REPLY received from HostID %d, TaskID %d: TaskID resolved to '%s'.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, ReceivedTaskName);
                                                        else
                                                          TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "RESOLVE_TASKID_REPLY received from HostID %d, TaskID %d: TaskID not found.", MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                      }

                                                      // Store task in the host's task list (same logic as TASKNAME_REPLY)
                                                      mutex_enter_blocking(&NetworkMutex);
                                                      CurrentHost=Network;
                                                      while (CurrentHost!=NULL)
                                                      {
                                                        if(CurrentHost->HostID==MessageToProcess->SenderHostID)
                                                        {
                                                          RemoteTask *CurrentTask=CurrentHost->NextTask;
                                                          RemoteTask *PreviousTask=NULL;
                                                          bool TaskRecordExists=false;
                                                          while (CurrentTask!=NULL)
                                                          {
                                                            if(CurrentTask->TaskID==MessageToProcess->SenderTaskID)
                                                            {
                                                              // Requested Taskname found in our records; update.
                                                              TaskRecordExists=true;  
                                                              if(ReceivedTaskName[0]!=0)
                                                              { 
                                                                // TaskID found on remote host; update task name in list.                                                               
                                                                strncpy(CurrentTask->TaskName, ReceivedTaskName, MAX_TASK_NAME_LENGTH);
                                                                CurrentTask->TaskName[MAX_TASK_NAME_LENGTH]='\0';

                                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task '%s' (TaskID %d) on HostID %d updated in network list.", ReceivedTaskName, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                              }
                                                              else
                                                              {
                                                                // TaskID not found on remote host; remove from list if it exists.
                                                                if(PreviousTask==NULL)
                                                                  CurrentHost->NextTask=CurrentTask->NextTask;                  // Removing first task in list
                                                                else                                                           
                                                                  PreviousTask->NextTask=CurrentTask->NextTask;                 // Removing task in middle or end of list
                                                                free(CurrentTask);

                                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d not found on HostID %d. Removed from network list.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                              }
                                                              break;
                                                            }
                                                            PreviousTask=CurrentTask;
                                                            CurrentTask=CurrentTask->NextTask;
                                                          }
                                                          
                                                          // If the TaskID is found on the remote host and not found in our list, create a RemoteTask struct and store it at the end of the list.
                                                          if(!TaskRecordExists && MessageToProcess->Payload0!=0) 
                                                          {
                                                            RemoteTask *NewTask=(RemoteTask *)malloc(sizeof(RemoteTask));
                                                            if(NewTask!=NULL)
                                                            {
                                                              NewTask->TaskID=MessageToProcess->SenderTaskID;
                                                              strncpy(NewTask->TaskName, ReceivedTaskName, MAX_TASK_NAME_LENGTH);
                                                              NewTask->TaskName[MAX_TASK_NAME_LENGTH]='\0';
                                                              NewTask->NextTask=NULL;
                                                              
                                                              if(CurrentHost->NextTask==NULL)
                                                                CurrentHost->NextTask=NewTask;
                                                              else
                                                                PreviousTask->NextTask=NewTask;
                                                              
                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Task '%s' (TaskID %d) on HostID %d stored.", ReceivedTaskName, MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                            }
                                                            else
                                                            {
                                                              // Memory allocation failed
                                                              TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_ERROR, "Failed to allocate memory to register new task %d for host %d.", MessageToProcess->SenderTaskID, MessageToProcess->SenderHostID);
                                                            }
                                                          }
                                                          break;
                                                        }
                                                        CurrentHost=CurrentHost->NextHost;
                                                      }                                                      
                                                      mutex_exit(&NetworkMutex);

                                                      // Set a timer to prevent new discovery/beacon activities for a while.
                                                      DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT);
                                                      break; 
                    case T_NETMGR_REVIEW_LIST:        // Maintain the remote host list.
                                                      {
                                                        if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Scheduled network linear list maintenance.");
                                                        
                                                        // Check if the DiscoveryOnTheNetworkTimer timer is expired; if not, skip this maintenance run to prevent too much overhead on the network. 
                                                        if(TISM_SoftwareTimerVirtualExpired(DiscoveryOnTheNetworkTimer))
                                                        {
                                                          // Now run through the hosts in the list.
                                                          uint8_t HostsInList=0, HostsContacted=0, HostsRemoved=0;
                                                          mutex_enter_blocking(&NetworkMutex);
                                                          CurrentHost=Network;
                                                          PreviousHost=NULL;

                                                          // Run through the list of hosts.
                                                          while (CurrentHost!=NULL)
                                                          {
                                                            HostsInList++;

                                                            // If 'minutes since last seen' is BELOW the T_NETMGR_HOST_REACHOUT threshold but reached-out flag is true, 
                                                            // reset the flag to false.
                                                            if(CurrentHost->MinutesSinceLastSeen<T_NETMGR_HOST_REACHOUT && CurrentHost->ReachedOut)
                                                              CurrentHost->ReachedOut=false;

                                                            // Increment the 'minutes since last seen' counter.
                                                            CurrentHost->MinutesSinceLastSeen+=T_NETMGR_HOST_REFRESH; 

                                                            // Depending on the 'minutes since last seen' and the reached-out flag, we have three possibilities for each host in the list.
                                                            if(CurrentHost->MinutesSinceLastSeen<T_NETMGR_HOST_REACHOUT)
                                                            {
                                                              // -= POSSIBILITY I =-
                                                              // If 'minutes since last seen' is BELOW the T_NETMGR_HOST_REACHOUT threshold, check the task list of the host.
                                                              bool TaskListNeedsUpdate=false;

                                                              // If the number of tasks registered for this host doesn't match the number of tasks in the list:
                                                              // - Remove all tasks for this host from the linear list, and free the associated memory.
                                                              // - Flag that a TISM_RESOLVE_TASKID message with RemoteTaskID=0xFF is needed to trigger an update of the host's task list.
                                                              uint8_t RegisteredTasks=0;
                                                              RemoteTask *CurrentTask=CurrentHost->NextTask;

                                                              // Count the number of registered tasks for this host and check if all TaskID's have a task name stored. 
                                                              // If not, flag that an update of the task list is needed.
                                                              while(CurrentTask!=NULL)   
                                                              {
                                                                RegisteredTasks++;
                                                                if(CurrentTask->TaskName[0]=='\0')
                                                                {
                                                                  // No task name found.
                                                                  TaskListNeedsUpdate=true;
                                                                  
                                                                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "TaskID %d for HostID %d has no task name.", CurrentTask->TaskID, CurrentHost->HostID);
                                                                }
                                                                CurrentTask=CurrentTask->NextTask;
                                                              }

                                                              // Does the number of registered tasks match the number of tasks reported by the remote host?
                                                              if(CurrentHost->NumberOfTasks!=RegisteredTasks)
                                                              {
                                                                // Number of registered tasks don't match reported by remote host. Rebuild task list.                                                               
                                                                TaskListNeedsUpdate=true;

                                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Number of tasks for HostID %d doesn't match our records.", CurrentHost->HostID);
                                                              }

                                                              // If the flag is set, rebuild the task list for this host.
                                                              if(TaskListNeedsUpdate)
                                                              {
                                                                // Remove the existing records, send a TISM_RESOLVE_TASKID message with RemoteTaskID=0xFF, and set the DiscoveryOnTheNetworkTimer.
                                                                TISM_NetworkManagerFreeHostTasks(CurrentHost);
                                                                TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, CurrentHost->HostID, 0x00, TISM_RESOLVE_TASKID, 0xFF, 0x00, time_us_64());
                                                                DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT);

                                                                if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Sent TISM_RESOLVE_TASKID message to update task list for HostID %d.", CurrentHost->HostID);
                                                              }
                                                              
                                                              // Move to next host
                                                              PreviousHost=CurrentHost;
                                                              CurrentHost=CurrentHost->NextHost;
                                                            } 
                                                            else if(CurrentHost->MinutesSinceLastSeen>=T_NETMGR_HOST_REACHOUT && CurrentHost->MinutesSinceLastSeen<T_NETMGR_HOST_EXPIRE && !CurrentHost->ReachedOut)
                                                            {
                                                              // -= POSSIBILITY II =-                                                            
                                                              // If the 'minutes since last seen' is ABOVE the reach-out threshold AND BELOW the expiration time, 
                                                              // check if we haven't reached out to this host yet. If so, send a TISM_DISCOVER message to trigger this host.

                                                              // Send a TISM_DISCOVER message to trigger this host, set the reached-out flag to true, and set the DiscoveryOnTheNetworkTimer.
                                                              TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, 0x00, CurrentHost->HostID, 0x00, TISM_DISCOVER, 0, 0, time_us_64());
                                                              CurrentHost->ReachedOut=true; 
                                                              HostsContacted++;
                                                              DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT);

                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Sent TISM_DISCOVER to host %d (%s), not seen for %d minutes.", CurrentHost->HostID, CurrentHost->Hostname, CurrentHost->MinutesSinceLastSeen);

                                                              // Move to next host
                                                              PreviousHost=CurrentHost;
                                                              CurrentHost=CurrentHost->NextHost;                                                              
                                                            }
                                                            else if(CurrentHost->MinutesSinceLastSeen>=T_NETMGR_HOST_EXPIRE)
                                                            {
                                                              // -= POSSIBILITY III =-                                                            
                                                              // If 'minutes since last seen' is ABOVE the expiration time, remove the host from the list and free all associated memory, including tasks.
                                                              RemoteHost *HostToRemove=CurrentHost;
                                                              
                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Removing expired host %d (%s) from network list.", HostToRemove->HostID, HostToRemove->Hostname);

                                                              // Free all tasks for this host before freeing memory
                                                              uint8_t TasksRemoved=TISM_NetworkManagerFreeHostTasks(HostToRemove);
                                                              if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Removed %d tasks for HostID %d.", TasksRemoved, HostToRemove->HostID);
                                                              
                                                              // Remove this host from the linked list.
                                                              if(PreviousHost==NULL)
                                                                Network=CurrentHost->NextHost;                  // Move start of list to next host, effectively removing the first host in the list.
                                                              else                                                           
                                                                PreviousHost->NextHost=CurrentHost->NextHost;   // Removing host in middle or end of list.
                                                              CurrentHost=CurrentHost->NextHost;                // Move pointer to next host before freeing memory.
                                                              
                                                              // Free the host record
                                                              free(HostToRemove);
                                                              HostsRemoved++;                                                              
                                                              NumberOfRemoteHosts--;
                                                            }
                                                            else                                                          
                                                            {
                                                              // Host between REACHOUT and EXPIRE, already contacted - just wait.
                                                              PreviousHost=CurrentHost;
                                                              CurrentHost=CurrentHost->NextHost;                                                              
                                                            }
                                                          }    
                                                          mutex_exit(&NetworkMutex);
                                                          
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Network linear list maintenance completed. Hosts in list: %d, contacted: %d, removed: %d.", HostsInList, HostsContacted, HostsRemoved);
                                                        } 
                                                        else
                                                        {
                                                          // DiscoveryOnTheNetworkTimer not expired yet, skip this maintenance run to prevent too much overhead on the network.                                                      
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "DiscoveryOnTheNetworkTimer not expired yet, skipping maintenance run.");                                                          
                                                        }
                                                      }
                                                      break;
                    case T_NETMGR_INTRODUCE:          // Send out a TISM_INTRODUCE broadcast, if allowed.
                                                      {
                                                        if(TISM_SoftwareTimerVirtualExpired(DiscoveryOnTheNetworkTimer))
                                                        {
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Beacon timeout reached; broadcasting TISM_INTRODUCE message.");

                                                          uint32_t Payload0, Payload1;
                                                          TISM_StringToPayloads(&System.Hostname[0], &Payload0, &Payload1);
                                                          TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, System.NumberOfTasks, UARTMX_BROADCAST, 0x00, TISM_INTRODUCE, Payload0, Payload1, time_us_64());

                                                          // Set a timer to prevent new discovery/beacon activities for a while.
                                                          DiscoveryOnTheNetworkTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_DISCOVER_TIMEOUT);
                                                        }
                                                        else
                                                        {
                                                          // DiscoveryOnTheNetworkTimer not expired yet, skip this broadcast to prevent too much overhead on the network.                                                      
                                                          if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "DiscoveryOnTheNetworkTimer not expired yet, skipping periodic TISM_INTRODUCEbroadcast.");                                                          
                                                        } 
                                                      }
                                                      break;
                    default:                          // Unknown message type, not captured earlier by TISM_UARTMX subscription.
                                                      // Return error message ERR_RECIPIENT_INVALID to sender, with original MessageType as payload.
                                                      TISM_PostmanTaskWriteMessage(ThisTask, MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID, ERR_RECIPIENT_INVALID, MessageToProcess->MessageType, 0);

                                                      if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "No handler for message type %s-0x%02X from HostID %d, TaskID %d. Returning error message.", TISM_MessageTypeToString(MessageToProcess->MessageType), MessageToProcess->MessageType, MessageToProcess->SenderHostID, MessageToProcess->SenderTaskID);
                                                      break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }

                // Do we need to broadcast a TaskID-TaskName pair? And are we allowed to (timer expired)?
                if(TaskBroadcastTimer>0 && TISM_SoftwareTimerVirtualExpired(TaskBroadcastTimer) &&  TISM_SoftwareTimerVirtualExpired(DiscoveryOnTheNetworkTimer))
                {
                  // Timerd expired, including DiscoveryOnTheNetworkTimer; broadcast the next TaskID-TaskName pair.
                  uint32_t Payload0, Payload1;
                  TISM_StringToPayloads(System.Task[TaskBroadcasting].TaskName, &Payload0, &Payload1);
                  TISM_PostmanWriteMessage(ThisTask.OutboundMessageQueue, System.HostID, TaskBroadcasting, UARTMX_BROADCAST, 0x00, TISM_RESOLVE_TASKID_REPLY, Payload0, Payload1, time_us_64());

                  if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Broadcasting TaskID %d with taskname '%s'.", TaskBroadcasting, System.Task[TaskBroadcasting].TaskName);

                  TaskBroadcasting++;
                  if(TaskBroadcasting>=System.NumberOfTasks)
                    TaskBroadcastTimer=0;   // All tasks broadcasted; stop timer.
                  else
                    TaskBroadcastTimer=TISM_SoftwareTimerSetVirtual(T_NETMGR_BROADCAST_PERIOD);  // Set timer for next broadcast.
                }

                // All done, back to sleep?
                if(TaskBroadcastTimer==0)   
                  TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case STOP:  // Task required to stop this task.
		            if(ThisTask.TaskDebug) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Stopping.");
		                          
                // Set the task state to DOWN. 
                TISM_SchedulerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
		
  // Run completed.
  if(ThisTask.TaskDebug==DEBUG_HIGH) TISM_EventLoggerLogEvent(ThisTask, TISM_LOG_EVENT_NOTIFY, "Run completed.");

  return (OK);
}

#endif
