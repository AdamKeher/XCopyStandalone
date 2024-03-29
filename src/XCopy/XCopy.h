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
#define ESPBaudRate 576000

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
  void getFile(String path, size_t size);
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
  XCopyESP8266 *_esp;

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
  bool _cancelOperation;
  int _prevSeconds = -1;
  bool _playCardSound = false;
};

#endif // XCOPY_H