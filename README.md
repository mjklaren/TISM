TISM - "The Incredible State Machine"

TISM is a framework for developing tasks that can run concurrently on a Raspberry Pi Pico (or compatible microcontroller) via cooperative multitasking. The framework consists of different elements to control task scheduling, interrupt handling, interprocess messaging and software timers. The source code also includes example applications to demonstrate how the different components work, as well as a template to build your own tasks.

However, TISM is not an operating system. It does not provide a filesystem, it does not manage resources, all elements of the system can be affected with little safeguards and poor behaving tasks will affect the whole system. 

The source code is distributed under the GPLv3 license.

