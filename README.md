# XCopy Standalone
An Arduino / Teensy based version of the Amiga XCopy application for copying floppy disks to and from ADF files. Built for the Teensy 3.2 using a custom PCB and an unmodified PC 3.5" floppy disk drive.

### Note
XCopy Standalone liberally uses floppy interface code taken from the excelent and generously open source ADF-Copy by Dominik Tonn. 
https://nickslabor.niteto.de/download/
Nicks software uses usb serial and a Java client or a MTP connection via usb to transfer data. XCopy Standalone focuses on a standalone unit using SD card storage and a LCD and / or web sockets interface for control.

### Thanks
Special thanks to Giants for all his help with this version including web interface graphics, documentation, suggestions etc.

## Device

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/master/brd/photo.png)

## Status
This project is under heavy development and is in a state of flux with new hardware and software being actively worked on.

## New Features - 04/2023
* v0.19 PCB released with intergrated ESP8266 for WiFi
  * Moved to Kicad from Eagle
  * Changed SD Card to MicroSD reader
  * Added WiFi
  * Connected USB serial to Teensy USB serial
  * Moved TFT to cheap chinese Adafruit knock off, this has changed the pinout
    * https://www.aliexpress.com/item/4001144194129.html
    * USD $1.89!
  * Moved navigation stick to cheap chinese item from LCSC
  * Additional 3.3v power regulator to relieve Teensy's
  * Added larger electrolytic cap for floppy power
  * Switced to four layer board as they are cheap and provide better power / ground planes
* Project code moved from Arduino to ProjectIO with all libraries preinstalled
* New web interface
* Amiga module ripper
* Bootblock library / virus checker using brainfile format
* Web based disk monitor
* Network NTP time update
* Upload and download ADF files via web interface

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
  * CR1220 battery backed realtime clock
  * ESP8266 for websockets driven interface
  * Single USB plug for serial and to power floppy and Teensy 3.2
  * Status LED's
  * Additional 3.3v power regulator
  * Gerbers, schematic and pcb layout files available 

# PCB

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/master/brd/pcb.png)

# Web Interface

![XCopy Board Image](https://github.com/AdamKeher/XCopyStandalone/blob/master/docs/web_interface.png)

# Build Environment Notes:

## VSCode Plugins:
1. PlatoformIO with Teensy & ESP8266 board 
2. Todo+ v4.18.4 by Fabio Spampinato

* Xcopy.h contains PCBVERSION define to target firmware builds to different PCB versions

## ESP8266 Build Notes:
1. Select the `d1_mini` environment (both boards are environments of this one project)
2. Plugin XCopyStandalone device and select "Debugging >> ESP >> ESP Programming Mode"
3. Set serial port to the Teensy port, the device will passthrough serial data to the ESP8266 program
4. Upload code, then upload the web assets with the `d1_mini` "Build Filesystem Image" and "Upload Filesystem Image" tasks

### Over the air

Once the ESP is on your network it can be updated over Wi-Fi instead, using the
`d1_mini_ota` environment - both the firmware and the web assets:

```shell
pio run -e d1_mini_ota -t upload      # firmware
pio run -e d1_mini_ota -t uploadfs    # web interface
pio run -e d1_mini_ota -t uploadall   # both, firmware first
```

`uploadall` is a task of this project rather than of PlatformIO, and it waits
between the two because each upload reboots the ESP.

Use `uploadfs` for the web assets, not `uploadfsota`. The platform decides
whether espota is offering a sketch or a filesystem image by matching the target
name exactly, and `uploadfsota` misses that test - so the image goes over as a
sketch, is measured against the 1MB sketch region instead of its own 2MB
partition, and is refused with `ERROR[4]: Not Enough Space`. `scripts/
espota_fs_flag.py` puts the missing flag back, so both targets now work.

The device calls itself `xcopy` to the DHCP server, to mDNS and to OTA, so it is
reachable by whichever name your network gives it. `upload_port` in
`platformio.ini` is set to `xcopy.lab` - change it to `xcopy.local` to go via
mDNS instead, or to a bare IP address if neither resolves from your machine.

The serial passthrough above is unchanged and remains the way back in if an
update ever leaves the ESP unable to join a network.
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

## Project layout

Both firmwares are environments of a single PlatformIO project, so the IDE lists
them in one Project Tasks tree and `pio run` with no arguments builds the pair.

| | |
|---|---|
| `src/` | Teensy 3.2 firmware, built by the `teensy31` environment |
| `src/esp8266/` | ESP8266 firmware, built by the `d1_mini` environment |
| `esp8266/data/` | web interface, uploaded to the ESP as a LittleFS image |
| `shared/` | the Teensy to ESP link contract, compiled into both |
| `lib/` | libraries for both; `esp8266/lib/` holds the ESP only ones |

Both boards are environments of the one project: `teensy31`, `d1_mini`, and
`d1_mini_ota` for updating the ESP over Wi-Fi.

Neither firmware is a `.ino` sketch: PlatformIO only converts a sketch sitting at
the top of the source directory, and with two source trees neither one can be.

## How to build PlatformIO based project

1. [Install PlatformIO Core](http://docs.platformio.org/page/core.html)
2. Download [development platform with examples](https://github.com/platformio/platform-teensy/archive/develop.zip)
3. Open the project
4. Pick the environment you want - `teensy31` or `d1_mini` - or build both at once
5. Select 'PlatformIO:Build'
6. Select 'PlatformIO:Upload'