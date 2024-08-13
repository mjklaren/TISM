# TISM - "The Incredible State Machine"

![TISM_mini](https://github.com/mjklaren/TISM/assets/127024801/c5aa2888-e35b-4955-86ff-b8fce6673e07) 

TISM is a framework for developing tasks that can run concurrently on a Raspberry Pi Pico (or compatible microcontroller using the RP2040) by applying cooperative multitasking techniques. The framework supports multicore use, consists of different elements to control task scheduling, interrupt handling, interprocess messaging, event logging and software timers. The source code also includes example applications to demonstrate how the different components work, as well as a template to help you to quickly build your own tasks.

Please note that TISM is not an operating system. It does not provide a filesystem, it does not manage resources or switch contexts. Furthermore, all elements of the system can be modified with little safeguards and poor behaving tasks will affect the whole system. But with a little discipline it will allow you to quickly develop multiple tasks that run concurrently on both cores of your RP2040 microcontroller.

## Why TISM?
I started developing TISM when I wanted to experiment with interacting with different devices from my Raspberry Pi Pico (leds, relais, sensors, motors) but realized that the Pico can only run one task at a time. And for most activities (e.g. waiting on a keypress, adjusting the PWM) a dual core 125Mhz processor provides way more capacity than that is actually used. What originally started as a few routinges to jump between sections of code gradually grew to the framework that it is today. The framework provides a way to quickly get going and with little overhead, while lots of parts can be tuned to meet your specific use case. 

But still, TISM is only an easy way to jump between tasks and allow sharing of CPU capacity. There is no forced context switching; bad behaving code can still lock all task. So if you're looking for 'real' preemptive multitasking capabilities consider solutions like FreeRTOS.

## Getting started with the example tasks
The framework includes three example tasks that demonstrate some of the inner workings of TISM. The wiring is pretty simple; a single button acting as a pull-down connected to GPIO 15 (including an RC network to debounce, which is optional):

![Example](https://github.com/mjklaren/TISM/assets/127024801/2ab32137-552a-4969-b3de-f1f85a9da09d)

To build and install TISM on the Pico:
- Install the [Raspberry Pi Pico SDK](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) on your device.
- Pull the [Github repository](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository) or download all the files to a folder.
- Modify the CMakeLists.txt file to include the folder and follow the steps in [Getting started with Raspberry Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) to build the image.
- Hold the BOOTSEL-button while plugging in the Raspberry Pi Pico. It will now appear as a USB mass storage device on your computer.
- Drag and drop "main.uf2" from the "build" folder into the Pico folder.

After succesful installation the Pico's onboard LED will start flashing. The LED's frequency of flashing will change every 10 seconds. If the button is pressed the frequency will change as well. 

And that's it! Check the sourcecode (ExampleTask1.c to ExampleTask4.c) to see what happens internally. TISM (and the example application) will write some logging information to standard output. To see the output (on Linux) use a terminal emulator:

`sudo screen /dev/ttyACM0`


The source code is distributed under the GPLv3 license.

