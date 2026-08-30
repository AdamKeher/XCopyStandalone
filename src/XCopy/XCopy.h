#ifndef XCOPY_H
#define XCOPY_H

#define XCOPYVERSION "v713.2023"
// #define XCOPY_DEBUG = 1

// #define PCBVERSION 1 // expensive adafruit screen and joystick
#define PCBVERSION 2 // cheap screen and joystick

#if PCBVERSION == 1
    #define TFT_ROTATION 3
#else
    #define TFT_ROTATION 1
#endif


#define ESPSerial Serial1

// 1,000,000 divides exactly on both ends: Teensy BAUD2DIV -> 192e6/192, ESP8266 -> 80e6/80.
// Zero divisor error on both sides, unlike the old 576000 (0.54% combined skew).
#define ESPBaudRate 1000000

// Serial1 drops to this while the ESP is flashed through the Teensy passthrough, so the
// data-link rate stays independent of what esptool has to cope with.
#define ESPProgBaudRate 115200

// SD upload transfer protocol (see XCopy::getFile and esp8266.ino handleFileUpload).
// The ESP may never have more than one un-ACKed chunk outstanding, so peak Serial1 ring
// occupancy is XFER_CHUNK -- keep it well under SERIAL1_RX_BUFFER_SIZE.
#define XFER_CHUNK 1024
#define XFER_ACK 0x06
#define XFER_TIMEOUT 5000

#include <Arduino.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>
#include <Streaming.h>
#include <SdFat.h>
#include "XCopyState.h"
#include "XCopyLog.h"
#include "XCopyPins.h"
#include "XCopyMenu.h"
#include "XCopyCommand.h"
#include "XCopyDisk.h"
#include "XCopyAudio.h"
#include "XCopyConfig.h"
#include "XCopyDirectory.h"
#include "XCopyGraphics.h"
#include "XCopyDebug.h"
#include "XCopyTime.h"
#include "XCopyADFLib.h"
#include "XCopyESP8266.h"
#include "XCopyFloppy.h"
#include "XCopyDriveTest.h"
#include "XCopyConsole.h"
#include "XCopyBrainFile.h"

#ifdef XCOPY_DEBUG
#include "RamMonitor.h"
#endif

class XCopy {
public:
  XCopy(TFT_ST7735 *tft);

  void begin();
  void update();
  void navigateUp();
  void navigateDown();
  void navigateSelect();
  void navigateLeft();
  void navigateRight();
  void processState();
  void intro();
  void cancelOperation();
  bool detectCancelPin();
  void setBusy(bool busy);
  bool getBusy() { return digitalRead(PIN_BUSYPIN); }
  void refreshTimeNtp();
  void startFunction(XCopyState state, String param = "");
  void startCopyADFtoDisk(String path  = "");
  void sendFile(String path);
  void getFile(String path, size_t size, bool overwrite = false);
  void processKeys(String keys);
  void sendBlock(int block);
  void cardChange();
  XCopyDisk* getDisk() { return &_disk; }
  XCopyConfig* getConfig() { return _config; }
  static void onWebCommand(void* obj, const String command);
  XCopyState _xcopyState = menus;
  #ifdef XCOPY_DEBUG
  void ramReport();
  #endif
  String _searchText;

private:
  TFT_ST7735 *_tft;
  XCopyMenu _menu;
  XCopyCommandLine *_command;
  XCopyAudio _audio;
  XCopyDisk _disk;
  XCopyDirectory _directory;
  XCopyGraphics _graphics;
  XCopyConfig *_config;
  XCopyESP8266 *_esp = nullptr;

#ifdef XCOPY_DEBUG
  RamMonitor _ram;
  uint32_t _lastRam = 0;
#endif
  XCopyMenuItem *verifyMenuItem;
  XCopyMenuItem *retryCountMenuItem;
  XCopyMenuItem *volumeMenuItem;
  XCopyMenuItem *ssidMenuItem;
  XCopyMenuItem *passwordMenuItem;
  XCopyMenuItem *diskDelayMenuItem;
  XCopyMenuItem *timeZoneMenuItem;

  bool _drawnOnce;
  // Set from ISR_CANCEL (XCopyStandalone.ino) and polled by processState()'s
  // passthrough loop. Without volatile the compiler is free to hoist the load out
  // of that loop, and passthrough mode can then never be exited.
  volatile bool _cancelOperation = false;
  int _prevSeconds = -1;
  // Set from ISR_CARD_DETECT, consumed in update().
  volatile bool _playCardSound = false;
  volatile uint32_t _cardChangeMs = 0;
};

#endif // XCOPY_H