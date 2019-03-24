#ifndef XCOPY_H
#define XCOPY_H

#define XCOPYVERSION "v 703.2019"
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
  setSSID = 27,
  setPassword = 28
};

class XCopy
{
public:
  XCopy(TFT_ST7735 *tft);

  void begin(int sdCSPin, int flashCSPin, int cardDetectPin, int busyPin);
  void update();
  void debug();
  void debugCompareFile(File sdFile, SerialFlashFile flashFile);
  void debugCompareTempFile();
  void debugEraseCopyCompare(bool erase);
  void navigateUp();
  void navigateDown();
  void navigateSelect();
  void navigateLeft();
  void navigateRight();
  void processState();
  void intro();
  bool cardDetect();
  void cancelOperation();
  void setBusy(bool busy);
  bool getBusy() { return digitalRead(_busyPin); }

  static void theCallbackFunction(const String command);

  void printDirectory(File dir, int numTabs);
  void printDir();
  void flashTest();

  void ramReport();

  XCopyState _xcopyState = menus;

private:
  TFT_ST7735 *_tft;
  XCopyMenu _menu;
  XCopyCommandLine *_command;
  XCopyAudio _audio;
  XCopyDisk _disk;
  XCopyDirectory _directory;
  XCopyGraphics _graphics;
  XCopyConfig *_config;
  XCopyADFLib *_adfLib;
  XCopyESP8266 *_esp;

#ifdef XCOPY_DEBUG
  RamMonitor _ram;
  uint32_t _lastRam = 0;
#endif
  uint8_t _cardDetectPin;
  uint8_t _sdCSPin;
  uint8_t _flashCSPin;
  uint8_t _busyPin;

  XCopyMenuItem *verifyMenuItem;
  XCopyMenuItem *retryCountMenuItem;
  XCopyMenuItem *volumeMenuItem;
  XCopyMenuItem *ssidMenuItem;
  XCopyMenuItem *passwordMenuItem;

  bool _drawnOnce;
  bool _cancelOperation;
  int _prevSeconds = -1;
};

#endif // XCOPY_H