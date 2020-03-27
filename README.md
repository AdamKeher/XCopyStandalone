# XCopy Standalone
An Arduino / Teensy based version of the Amiga XCopy application for copying floppy disks to and from ADF files. Built for the Teensy 3.2 using a custom PCB and an unmodified PC 3.5" floppy disk drive.
##
XCopy Standalone liberally uses floopy interface code taken from the excelent and generously open source ADF-Copy by Dominik Tonn. 
https://nickslabor.niteto.de/download/
Nicks software uses usb serial and a Java client or a MTP connection via usb to transfer data. XCopy Standalone focuses on a standalone unit using SD card storage and a LCD and / or web sockets interface for control.
## Status
This project is under heavy development and is in a state of flux with new hardware and software being actively worked on.
Major changes in progress:
* PCB update to include native ESP8266EX
* PCB update to include native Teensy 3.2 circuitry
## Features
* Software
  * ADF Support
    * Amiga file system support for floppy disks and ADF files via ADFlib
  * Disk copy
    * Disk to SD Card
    * SD Card to Disk
    * Disk to Disk
    * Disk to Temporary Flash
    * Temporary Flash to Disk
  * Test disk
  * Format disk
  * Disk flux measurement
  * Serial command line
  * Interfaces
    * TFT LCD
    * USB Serial
    * HTML
  * Serial passthrough for programming onboard ESP8266
* Hardware
  * IDC 34 pin standard floppy drive cable interface
  * 1.8" ST7735R 128x160, 18bit color LCD
  * 5 way navigation joystick
  * Mono PAM8302 amplifier with adjustable Pot
    * Onboard mounted speaker
  * SD Card reader
  * 128MBit Flash Rom with built in ADF files
  * CR2032 battery backed realtime clock
  * ESP8266 for websockets driven interface
  * Single USB power for floppy and Teensy 3.2
  * Status LED's
  * Gerbers, schematic and pcb layout files available 

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/master/brd/ADF%20Copy%20v0.5.png)
![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/master/files/Graphics/XCopy%20Board.png)

