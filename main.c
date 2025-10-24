/* 
  
  The main file of "The Incredible State Machine", starting point for the system.

  Copyright (c) 2025 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#ifndef TISM_DISABLE_DUALCORE
#include "pico/multicore.h"
#endif

#include "TISM.h"

// The different user tasks to be included here.
#include "ExampleTask1.c"
#include "ExampleTask2.c"
#include "ExampleTask3.c"
#include "ExampleTask4.c"


#ifndef TISM_DISABLE_DUALCORE
// Start the 2nd scheduler on the 2nd core of the RP2040/RP2350.
void StartCore2()
{
  if (TISM_Scheduler(CORE1))
    fprintf(STDERR, "TISM: TISM Scheduler for CORE1 exited with error.\n");
}
#endif


void main(void)
{
  // Initialize the TISM system.
  TISM_InitializeSystem();

  // Register the user tasks that do the actual work. When registering tasks the TaskName and Priority MUST be provided. 
  // Usage: TISM_RegisterTask(<pointer to function>,<short name>.<priority>)
  
  if((TISM_RegisterTask(&ExampleTask1,"ExampleTask1",PRIORITY_NORMAL)+
      TISM_RegisterTask(&ExampleTask2,"ExampleTask2",PRIORITY_NORMAL)+  
      TISM_RegisterTask(&ExampleTask3,"ExampleTask3",PRIORITY_NORMAL)+
      TISM_RegisterTask(&ExampleTask4,"ExampleTask4",PRIORITY_NORMAL))!=0)
  {
     // An error occured during registering of the tasks. Abort.
     fprintf(STDERR, "TISM: Error occured when registering a tasks. Stopping...\n");
     exit;
  };  

#ifdef EXTREME_DEBUGGING
  // Extreme debugging; set all user tasks' debug levels to HIGH. Use with caution, this creates a LOT of messages!
  for(int TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
     System.Task[TaskCounter].TaskDebug=DEBUG_HIGH;
#endif

  // Set debug-levels of individual tasks. Can be both system/TISM or user tasks. Use with caution, as it can generate A LOT of messages!
  System.Task[TISM_GetTaskID("TISM_UartMX")].TaskDebug=DEBUG_HIGH;
  //System.Task[TISM_GetTaskID("TISM_Postman")].TaskDebug=DEBUG_LOW;
  //System.Task[TISM_GetTaskID("TISM_EventLogger")].TaskDebug=DEBUG_LOW;
  //System.Task[TISM_GetTaskID("ExampleTask4")].TaskDebug=DEBUG_NONE;
  //System.Task[TISM_GetTaskID("ExampleTask5")].TaskDebug=DEBUG_LOW;

#ifndef TISM_DISABLE_DUALCORE  
  // Start up the 2nd core and fire up a 2nd TISM_Scheduler.
  multicore_launch_core1(StartCore2);
#endif

  // All tasks registered and 2nd core running. Now start up the scheduler for Core 0.
  // Initialization of tasks is taken care of by CORE0; CORE1 waits until system enters RUN-state.
  if (TISM_Scheduler(CORE0))
    fprintf(STDERR, "TISM: TISM Scheduler for CORE0 exited with error.\n");
  
  // Schedulers returned from execution; TISM stopped.
  printf("TISM: Program completed.\n");
  sleep_ms(STARTUP_DELAY);
}