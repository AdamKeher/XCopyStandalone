#ifndef XCOPYDEBUG_H
#define XCOPYDEBUG_H

#include <Arduino.h>
#include <Streaming.h>
#include <SdFat.h>
#include <sdios.h>
#include <SerialFlash.h>
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "XCopyConsole.h"

enum state
{
  flash = 0,
  sdcard = 1,
  paused = 2,
  both = 3
};

class XCopyDebug
{
  public:
    XCopyDebug(XCopyGraphics *graphics, XCopyAudio *audio, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin);

    void debugCompareFile(FatFile sdFile, SerialFlashFile flashFile);
    void debugCompareTempFile();
    void debugFaultFind();
    void debugEraseFlash();
    void debugFlashDetails();
    void debugEraseCopyCompare();    
    void debugCompare();
    void debugTestFlashSD();

    bool initSerialFlash();
    bool initSdCard();

    uint32_t calculateSize();
    bool eraseFlash();
    bool cardDetect();
    void printBanner(const char* heading = "");
    void printHeading(const char* heading = "");
    void printDirectory(File dir, uint8_t numTabs);    
    void listFlashFiles();
    void compareFiles();
    void SdTest();
    void flashTest();
    unsigned long flashDetails();

  private:
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;

    uint8_t _sdCSPin;
    uint8_t _flashCSPin;
    uint8_t _cardDetectPin;

    uint16_t _cycles = 0;
    uint16_t _sdcardErrorCount = 0;
    uint16_t _flashErrorCount = 0;
    uint16_t _flashReadCount = 0;
    uint16_t _sdcardReadCount = 0;

    bool _cardState = false;
    bool _cardInit = false;
    
    state _mode = both;
    SdFat SD;

    const char* debugVersion = "v0.6";
    const char* debugYear = "2022";
};

#endif // XCOPYDEBUG_H