# TISM - "The Incredible State Machine"

![TISM_mini](https://github.com/mjklaren/TISM/assets/127024801/c5aa2888-e35b-4955-86ff-b8fce6673e07) 

TISM is a framework for developing tasks that can run concurrently on a Raspberry Pi Pico (or compatible microcontroller using the RP2040) by applying cooperative multitasking techniques. The framework supports multicore use, consists of different elements to control task scheduling, interrupt handling, interprocess messaging, event logging and software timers. The source code also includes example applications to demonstrate how the different components work, as well as a template to help you to quickly build your own tasks.

Please note that TISM is not an operating system. It does not provide a filesystem, it does not manage resources or switch contexts. Furthermore, all elements of the system can be modified with little safeguards and poor behaving tasks will affect the whole system. But with a little discipline it will allow you to quickly develop multiple tasks that run concurrently on both cores of your RP2040 microcontroller.

## Why TISM?
I started developing TISM when I wanted to experiment with interacting with different devices from my Raspberry Pi Pico (leds, relais, sensors, motors) but realized that the Pico can only run one task at a time. And for most activities (e.g. waiting on a keypress, adjusting the PWM) a dual core 125Mhz processor provides way more capacity than that is actually used. What originally started as a few routinges to jump between sections of code gradually grew to the framework that it is today. The framework provides a way to quickly get going and with little overhead, while lots of parts can be tuned to meet your specific use case. 

But still, TISM is only an easy way to jump between tasks and allow sharing of CPU capacity. There is no forced context switching; bad behaving code can still lock all task. So if you're looking for 'real' preemptive multitasking capabilities consider solutions like FreeRTOS.

## Getting started with the example tasks
The framework includes three example tasks that demonstrate some of the inner workings of TISM. The wiring is pretty simple; a single button acting as a pull-down connected to GPIO 15 (including an RC network to debounce, which is optional):

<img width="1755" height="636" alt="TISM_Example_bb" src="https://github.com/user-attachments/assets/f4347fa7-aa3d-4b02-8a6b-28bdff27c91e" />

To build and install TISM on the Pico:
- Install the [Raspberry Pi Pico SDK](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) on your device.
- Pull the [Github repository](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository) or download all the files to a folder.
- Modify the CMakeLists.txt file to include the folder and follow the steps in [Getting started with Raspberry Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) to build the image. Don't forget to copy pico_sdk_import.cmake into the source directory.
- Hold the BOOTSEL-button while plugging in the Raspberry Pi Pico. It will now appear as a USB mass storage device on your computer.
- Drag and drop "main.uf2" from the "build" folder into the Pico folder.

After succesful installation the Pico's onboard LED will start flashing. The LED's flashing frequency will change every 10 seconds. If the button is pressed however the frequency will change immediately. 

## How the examples work
This is probably one of the most complex 'blinking led' examples. This example code consists of 4 separate tasks, demonstrating various aspects of the TISM framework:
- "ExampleTask1.c" monitors GPIO15 by 'subscribing' to GPIO_IRQ_EDGE_FALL and GPIO_IRQ_EDGE_FALL events (press and release of the button) on GPIO15 using the TISM IRQ handler. If the button is pressed messages are sent to ExampleTask2 and ExampleTask3 via TISM's messaging system. If the button is released another message is sent to ExampleTask3.
- "ExampleTask2.c" is responsible for blinking the onboard LED of the Raspberry Pi Pico (GPIO25). A repetitive software timer is set (TISM's software timer); whenever a message is received the frequency of blinking is changed; when a message is received from ExampleTask1 the frequency is also changed.
- "ExampleTask3.c" sets a repetitive timer and writes the number of cycles this specific task has run to STDOUT whenever an event from the software timer is received. But there is a twist; whenever the button is pressed (and a message is received from ExampleTask1) the priority of this task is set to PRIORITY_HIGH by modifying its task properties, resulting in this task running more often. When the button is released (again, a message is received from ExampleTask1) the priority is reset to PRIORITY_NORMAL. So holding the button means that ExampleTask3 will run more often.
- "ExampleTask4.c" emulates load on the system by using 'sleep_ms' and counting how many cycles it has run. When the maximum number of runs is reached the TISM-system is stopped.

And that's it! Check the sourcecode (ExampleTask1.c to ExampleTask4.c) to see what happens internally. TISM (and the example application) will write some logging information to standard output. To see the output (on Linux) use a terminal emulator:

`sudo screen /dev/ttyACM0`

## Tuning the behavior of TISM
A LOT of bits and part of the system can be modified, have a look at TISM.h. Uncommenting the following definitions will impact behavior and performance of the Raspberry Pi Pico:
- TISM_DISABLE_PRIORITIES        - Disable priorities mechanism; all tasks are executed round robin.
- TISM_DISABLE_SCHEDULER         - Disables the scheduler; all tasks start consecutively, no planning. Also disables the TISM_SoftwareTimer.
- TISM_DISABLE_DUALCORE          - Disables dual processor core operation; only use the first core.

TISM uses a priority mechanism based on the number of microseconds will pass before another run of a task will be retried. As this framework uses cooperative multitasking, there is no guarantee that the task will be executed exactly after this period of time (but it won't start earlier). Effectively; the lower the value, the higher the priority. Furthermore, tasks with PRIORITY_HIGH will be checked more often if tasks need to be executed, PRIORITY_LOW the least often.
- PRIORITY_HIGH            2500   - High priority task; time after which task should be restarted (in usec). 
- PRIORITY_NORMAL          5000   - Normal priority task; time after which task should be restarted (in usec).
- PRIORITY_LOW             10000  - Low priority task; time after which task should be restarted (in usec).

## Change log - 251024
- Major rewrite and cleanup of the code.
- Improved task switching and removed a bug where a task could run on both cores simultanously under low-load conditions.
- Extended the buffersize of the eventlogger to prevent missing messages when debuglevels are set to high.
- Tuned the priority settings in TISM.h.
- Added several options to alter the behavior of TISM (see above).
- Added option to cancel a software timer via an ID.
- Introduced step-by-step execution to facilitate debugging (see RUN_STEP_BY_STEP in TISM.h).
- Laid some groundwork for multi-host operation.
 
## Wish list:
- Multi-host operation using Wifi or RS485.

The source code is distributed under the GPLv3 license.

