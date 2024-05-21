/* 
  
  The main file of "The Incredible State Machine", starting point for the system.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "TISM.h"

// The different user tasks to be included here.
#include "ExampleTask1.c"
#include "ExampleTask2.c"
#include "ExampleTask3.c"
#include "ExampleTask4.c"


// Start the 2nd scheduler on the 2nd core of the RP2040.
void StartCore2()
{
  if (TISM_Scheduler(CORE1))
    fprintf(STDERR, "Main: TISM Scheduler for CORE1 exited with error.\n");
}


void main(void)
{
  // Initialize the TISM system.
  System.SystemDebug=DEBUG_LOW;     // Always set the debug-level before init. Use with caution!
  TISM_InitializeSystem();
 
  // Register the processes that do the actual work. When registering tasks the TaskName and Priority MUST be provided. 
  if((TISM_RegisterTask(&ExampleTask1,"ExampleTask1",PRIORITY_NORMAL) + 
      TISM_RegisterTask(&ExampleTask2,"ExampleTask2",PRIORITY_NORMAL) +  
      TISM_RegisterTask(&ExampleTask3,"ExampleTask3",PRIORITY_NORMAL) +
      TISM_RegisterTask(&ExampleTask4,"ExampleTask4",PRIORITY_NORMAL))!=0)
  {
     // An error occured during registering of the tasks. Abort.
     fprintf(STDERR, "Main: Error occured when registering a tasks. Stopping...\n");
     exit;
  };  
 
  // Extreme debugging; set all tasks' debug levels to HIGH. Use with caution!
  // for(int TaskCounter=0;TaskCounter<System.NumberOfTasks;TaskCounter++)
  //   System.Task[TaskCounter].TaskDebug=DEBUG_HIGH;

  // Set debug-levels of individual tasks. Use with caution!
  System.Task[TISM_GetTaskID("ExampleTask1")].TaskDebug=DEBUG_NONE;
  System.Task[TISM_GetTaskID("ExampleTask2")].TaskDebug=DEBUG_NONE;
  System.Task[TISM_GetTaskID("ExampleTask3")].TaskDebug=DEBUG_NONE;
  System.Task[TISM_GetTaskID("ExampleTask4")].TaskDebug=DEBUG_NONE;
  
  // Start up the 2nd core and fire up a 2nd TISM_Scheduler.
  multicore_launch_core1(StartCore2);

  // All tasks registered and 2nd core running. Now start up the scheduler for Core 0.
  if (TISM_Scheduler(CORE0))
    fprintf(STDERR, "Main: TISM Scheduler for CORE0 exited with error.\n");
  
  printf("Program completed.\n");
}