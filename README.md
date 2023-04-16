# XCopy Standalone
An Arduino / Teensy based version of the Amiga XCopy application for copying floppy disks to and from ADF files. Built for the Teensy 3.2 using a custom PCB and an unmodified PC 3.5" floppy disk drive.

## Note
XCopy Standalone liberally uses floppy interface code taken from the excelent and generously open source ADF-Copy by Dominik Tonn. 
https://nickslabor.niteto.de/download/
Nicks software uses usb serial and a Java client or a MTP connection via usb to transfer data. XCopy Standalone focuses on a standalone unit using SD card storage and a LCD and / or web sockets interface for control.

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/ProjectGiants/brd/photo.png)

## Status
This project is under heavy development and is in a state of flux with new hardware and software being actively worked on.

## New Features
* v0.19 PCB released with intergrated ESP8266 for WiFi
* New web interface added
* Amiga module ripper
* Bootblock library / virus checker using brainfile format
* Web based disk 
* Network NTP time update

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


# PCB

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/ProjectGiants/brd/pcb.png)

# Web Interface

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/ProjectGiants/docs/web_interface.png)

# Build Environment Notes:

## VSCode Plugins:
1. PlatoformIO with Teensy & ESP8266 board 
2. Todo+ v4.18.4 by Fabio Spampinato

## ESP8266 Build Notes:
1. Switch PlatformIO Project Environment to "XCopyStandalone/esp8266"
2. Plugin XCopyStandalone device and select "Debugging >> ESP >> ESP Programming Mode"
3. Set serial port to the Teensy port, the device will passthrough serial data to the ESP8266 program
4. Upload code
5. Select "Debugging >> ESP >> Reset ESP"
6. Open a serial terminal
7. Connect to your wireless access point using the connect command
```shell
>> connect SSIDName Password
```
8. Type the following commands to confirm you have connected
```shell
>> status
WiFi Status: Connected
-----
Mode: STA
PHY mode: N
Channel: 10
AP id: 0
Status: 5
Auto connect: 1
SSID (6): SSIDName
Passphrase (12): Password
BSSID set: 0
>> ip
192.168.X.XX
>> ssid
SSIDName
>>
```

## How to build PlatformIO based project

1. [Install PlatformIO Core](http://docs.platformio.org/page/core.html)
2. Download [development platform with examples](https://github.com/platformio/platform-teensy/archive/develop.zip)
3. Open the project
4. Select 'PlatformIO:Build'
5. Select 'PlatformIO:Upload'