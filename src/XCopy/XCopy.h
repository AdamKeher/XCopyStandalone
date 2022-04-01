#ifndef XCOPY_H
#define XCOPY_H

#define XCOPYVERSION "v707.2022"
// #define XCOPY_DEBUG = 1

#define ESPSerial Serial1
#define ESPBaudRate 115200

#include <Arduino.h>
#include "TFT_ST7735.h"
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>
#include <Streaming.h>
#include <SdFat.h>
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

#ifdef XCOPY_DEBUG
#include "RamMonitor.h"
#endif

enum XCopyState
{
  undefined = 0,
  menus = 1,
  idle = 2,
  copyDiskToADF = 3,
  testDisk = 4,
  copyADFToDisk = 5,
  showTime = 6,
  about = 7,
  debuggingTempFile = 8,
  debuggingSDFLash = 9,
  debuggingEraseCopy = 10,
  debuggingCompareFlashToSDCard = 12,
  copyDiskToDisk = 13,
  directorySelection = 14,
  setVerify = 15,
  setRetry = 16,
  setVolume = 17,
  copyDiskToFlash = 18,
  copyFlashToDisk = 19,
  debuggingFlashDetails = 23,
  fluxDisk = 24,
  formatDisk = 25,
  debuggingSerialPassThrough = 26,
  debuggingSerialPassThroughProg = 27,
  setSSID = 28,
  setPassword = 29,
  resetESP = 30,
  debuggingFaultFind = 31,
  debuggingEraseFlash = 32,
  setDiskDelay = 33,
  testDrive = 34,
  resetDevice = 35,
  setTimeZone
};

class XCopy
{
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
  void setBusy(bool busy);
  bool getBusy() { return digitalRead(PIN_BUSYPIN); }
  void refreshTimeNtp();
  static void onWebCommand(void* obj, const String command);
  XCopyState _xcopyState = menus;
  #ifdef XCOPY_DEBUG
  void ramReport();
  #endif

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
};

#endif // XCOPY_H