#ifndef XCOPYDEBUG_H
#define XCOPYDEBUG_H

#include <Arduino.h>
#include <Streaming.h>
#include <SdFat.h>
#include <sdios.h>
#include <SerialFlash.h>
#include "XCopyAudio.h"
#include "XCopyGraphics.h"

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
    void debugEraseCopyCompare(bool erase);
    void debug();
    bool cardDetect();
    void printDirectory(File dir, uint8_t numTabs);
    void printDir();
    void flashTest();
    void flashDetails();

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
};

#endif // XCOPYDEBUG_H