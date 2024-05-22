# TISM - "The Incredible State Machine"

TISM is a framework for developing tasks that can run concurrently on a Raspberry Pi Pico (or compatible microcontroller) by applying cooperative multitasking techniques. The framework supports multicore use, consists of different elements to control task scheduling, interrupt handling, interprocess messaging and software timers. The source code also includes example applications to demonstrate how the different components work, as well as a template to build your own tasks.

However, TISM is not an operating system. It does not provide a filesystem, it does not manage resources or switch contexts. Furthermore, all elements of the system can be affected with little safeguards and poor behaving tasks will affect the whole system. But with a little discipline it will allow you to quickly develop multiple tasks that run concurrently on one microcontroller.

## Getting started
- Install the [Raspberry Pi Pico SDK]{https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf} on your device.
- Pull the [Github repository]{https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository} or download all the files to a folder.

The source code is distributed under the GPLv3 license.

