#ifndef XCOPYDISK_H
#define XCOPYDISK_H

#define SD_ADF_PATH "XCopy ADF Files"

#include <Arduino.h>
#include <Streaming.h>
#include <SerialFlash.h>
#include <SdFat.h>
#include <TimeLib.h>
/* 
  FastCRC quick hacked to force SW CRC as HW CRC used static variables
  and can't do concurrent 16/32 bit CRC's, defined & REV16 & REV32 altered
*/
#include "../FastCRC/FastCRC.h"
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "FloppyDrive.h"
#include "XCopyState.h"
#include "XCopyESP8266.h"

enum ADFFileSource
{
  _flashMemory = 0,
  _sdCard = 1
};

class XCopyDisk
{
  public:
    XCopyDisk();
    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin);
  
    int readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount, bool silent = false);
    int writeDiskTrack(uint8_t trackNum, uint8_t retryCount);

    bool diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination = _sdCard);
    void adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source = _sdCard);
    void diskToDisk(bool verify, uint8_t retryCount);
    void diskFlux();
    void drawFlux(uint8_t trackNum, uint8_t scale = 2, uint8_t yoffset = 0);
    static void dateTime(uint16_t *date, uint16_t *time);
    void changeDisk();
    void testDiskette(uint8_t retryCount);
    String getADFVolumeName(String ADFFileName, ADFFileSource = _sdCard);
    String generateADFFileName(String diskname);

    bool cardDetect() { return digitalRead(_cardDetectPin) == 0 ? true : false; };

    void cancelOperation();
    void OperationCancelled(uint8_t trackNum = -1);

  private:
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;
    XCopyESP8266 *_esp;
    uint8_t _sdCSPin;
    uint8_t _flashCSPin;
    uint8_t _cardDetectPin;
    volatile bool _cancelOperation = false;
    SdFat SD;
};

#endif // XCOPYDISK_H