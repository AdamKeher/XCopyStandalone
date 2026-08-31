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

// Baud rates, timeouts and the file transfer wire format are the ESP link contract and
// live in shared/XCopyProtocol.h, which both firmware trees compile.

#include <Arduino.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>
#include <Streaming.h>
#include <SdFat.h>
#include "XCopyProtocol.h"
#include "XCopyState.h"
#include "XCopyAction.h"
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
#include "XCopyTransfer.h"

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
  void startFunction(XCopyAction action, String param = "");
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
  XCopyTransfer _transfer;

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

  bool _drawnOnce = false;
  // True while the passthrough loop is servicing ESP *programming* mode, where
  // Serial1 runs at ESPProgBaudRate and the ESP is in its ROM bootloader.
  bool _espProgMode = false;
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