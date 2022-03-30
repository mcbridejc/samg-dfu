DFU Bootloader for SAMG55
=========================

A DFU bootloader to program a SAMG55 device over USB, built with 
[modm](https://github.com/modm-io/modm) and [tinyusb](https://github.com/hathach/tinyusb). 

This was originally written for the [PurpleDrop](https://github.com/uwmisl/purpledrop),
but makes a good stand-alone example.

## Overview

This bootloader will enumerate as a USB DFU device, which can be programmed 
using standard utilities, such as [dfu-util](http://dfu-util.sourceforge.net/).

Programming command example: `dfu-util -d <vid>:<pid> -a 0 -D myapplication.bin`.  

There isn't any CRC check or such on the application, so whether or not 
there is an application in memory, the bootloader will normally jump straight
to the application upon boot. However, it will remain in DFU mode indefinitely
if one of the following conditions are met: 

- The boot pin (GPIO A1, by default) is high at power on
- The last reset was caused by a watchdog expiration
- A special code (0x4D49534C) has been written to word 7 in the General Purpose Backup Register (GPBR)

The SAMG55 watchdog is enabled by default, so if no valid application is programmed, the chip
should reset after a few seconds and go back into DFU mode. However, there's a big caveat here: 
If you place an external RC debounce on your reset pin with a rise time longer than 1 SCLK 
cycle (about 30us) this will prevent the reset controller status register from properly
reporting a watchdog reset (per datasheet section 19.4.3.4).

## Things you may want to configure

### USB VID/PID

These are defined in `src/tusb_descriptors.cpp`. They are populated here with 
random numbers; you may want to use your own. 

### Code Start Location

The bootloader occupies the first 8KB of flash. By default, the application
will be programmed at 0x4000 (16KB). This leaves the second 8KB sector available
for application use as non-volatile storage, but if you don't need that and 
want to get an extra 8KB of memory ( or if you want to move the application
even further back) you can update the value of PROGRAM_START_PAGE in `src/main.cpp`.

### The boot pin

A pin can be used to force the bootloader to go into DFU mode instead of running
the application. This can be connected to a button which pulls the pin high when
pressed. By default, this is connected to GpioA1, but it can be changed by 
updating the `using BootPin = GpioA1` line in `src/main.cpp`.

The boot pin can be disabled altogether with `using BootPin = GpioUnused`.

## Application Requirements

### Memory Layout

The application does not have to do *too much* differently to load via the
bootloader, but it must update the memory layout in its linker script to 
put the application code at the right spot.

Here's an example memory layout, which works with the default PROGRAM_START_PAGE
value of 32. FLASH_BOOT is where the bootloader lives, FLASH_CFG1 and FLASH_CFG2
are used for dual-buffered application data storage, and FLASH is where the
application code will live.

```
MEMORY
{
	FLASH_BOOT (rx) : ORIGIN = 0x00400000, LENGTH = 8K
	FLASH_CFG1 (r) : ORIGIN = 0x00402000, LENGTH = 4K
	FLASH_CFG2 (r) : ORIGIN = 0x00403000, LENGTH = 4k
	FLASH (rx) : ORIGIN = 0x00404000, LENGTH = 496K
	RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 163840
}
```

### Detach to bootloader

If you want to be able to program an application without holding the
boot button and resetting, you can implement a detach function. This can be done
by rolling your own, or if you create a DFU-RT (runtime) endpoint in your
application, then dfu-util will be able to automatically send a detach 
command, so that the programming command will work whether your chip is 
running the bootloader or the application.

Rebooting to DFU is done by writing a magic number into the `GPBR[7]` 
register, and resetting the processor. Like this:

```
void reboot_to_bootloader() {
    // Set a flag to signal bootloader to remain in DFU mode
    GPBR->SYS_GPBR[7] = 0x4D49534C;
    // Reset
    RSTC->RSTC_CR = RSTC_CR_PROCRST_Msk | RSTC_CR_PERRST_Msk | RSTC_CR_KEY_PASSWD;
}
```

How you implement DFU-RT will depend on your application, what USB driver you
use, etc. so I won't go into it too much. If you're using modm, it's pretty easy
though. First, add `device.dfu` to your tiny usb config in `project.xml`. For example,
on PurpleDrop the USB implements two modes, a CDC and the DFU-RT so the option looks
like this:

`<option name="modm:tinyusb:config">device.cdc, device.dfu</option>`

Then you just need to implement the callback for the DFU runtime: 

```
// Callback for the tinyusb DFU runtime driver, called when DFU_DETACH command
// is recieved.
extern "C" void tud_dfu_runtime_reboot_to_dfu_cb(void)
{
    reboot_to_bootloader();
}
```

For an example of an application which uses the bootloader, see [the purpledrop sam target](https://github.com/uwmisl/purpledrop-stm32/tree/master/sam).

## Building the bootloader

This project uses modm, a library of drivers and build tools supports a range of 
ARM Cortex microcontrollers. If you are looking for help getting build tools set
up, the [modm guide](https://modm.io/guide/installation/) is a great resource.

The quick reference, assuming you have an ARM toolchain installed and already installed
the modm python tools (e.g. `pip install modm`):

In the top level directory:

`git submodule update --init --recursive` to make sure the modm submodule and all 
of its submodules are checked out.

In the `app/` directory:

`lbuild build` to assemble the `app/modm` directory, based on the configuration 
options found in `project.xml`. 

`scons build` to compile. 

`scons program` to download to flash with an OpenOCD compatible programmer.
