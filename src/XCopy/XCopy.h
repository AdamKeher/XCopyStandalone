#ifndef XCOPY_H
#define XCOPY_H

#define XCOPYVERSION "v 0.5.2019"
// #define XCOPY_DEBUG = 1

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
  copyDiskToFlash = 18,
  copyFlashToDisk = 19,
  debuggingFlashDetails = 23,
  fluxDisk = 24,
  formatDisk = 25
};

class XCopy
{
public:
  XCopy(TFT_ST7735 *tft);

  void begin(int sdCSPin, int flashCSPin, int cardDetectPin);
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
  void intro();
  bool cardDetect();
  void cancelOperation();
  // void testdrawtext(String text, uint16_t color, int x, int y);

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
  
#ifdef XCOPY_DEBUG
  RamMonitor _ram;
  uint32_t _lastRam = 0;
#endif
  uint8_t _cardDetectPin;
  uint8_t _sdCSPin;
  uint8_t _flashCSPin;

  XCopyMenuItem *verifyMenuItem;
  XCopyMenuItem *retryCountMenuItem;

  bool _drawnOnce;
  int _prevSeconds = -1;
};

#endif // XCOPY_H