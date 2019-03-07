#ifndef XCOPYDISK_H
#define XCOPYDISK_H

#include <Arduino.h>
#include <Streaming.h>
#include <SD.h>
#include <SerialFlash.h>
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "FloppyDrive.h"

enum ADFFileSource
{
  _flashMemory = 0,
  _sdCard = 1
};

class XCopyDisk
{
  public:
    XCopyDisk();
    void begin(XCopyGraphics *graphics, XCopyAudio *audio, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin);

    void readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount);
    void writeDiskTrack(uint8_t trackNum, uint8_t retryCount);

    void diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination = _sdCard);
    void adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source = _sdCard);
    void diskToDisk(bool verify, uint8_t retryCount);
    void diskFlux();
    void changeDisk();
    void testDisk(uint8_t retryCount);
    String getADFVolumeName(String ADFFileName, ADFFileSource = _sdCard);
    void flashDetails();

    bool cardDetect() { return digitalRead(_cardDetectPin) == 0 ? true : false; };

    void cancelOperation();

  private:
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;
    uint8_t _sdCSPin;
    uint8_t _flashCSPin;
    uint8_t _cardDetectPin;
    volatile bool _cancelOperation = false;
};

#endif // XCOPYDISK_H