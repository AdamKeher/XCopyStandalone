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
#include "XCopySDCard.h"
#include "MD5.h"

enum ADFFileSource
{
  _flashMemory = 0,
  _sdCard = 1
};

class DiskLocation {
public:
  int block = 0;
  int track = 0;
  int logicalTrack = 0;
  int side = 0;
  int sector = 0;

  void setBlock(int logicalTrack, int sector) {
    this->logicalTrack = logicalTrack;
    this->track = logicalTrack / 2;
    this->side = logicalTrack % 2;
    this->sector = sector;
    block = (this->track * 22) + (this->side * 11) + this->sector;
  }

  void setBlock(int value) {
    block = value;
    logicalTrack = block / 11;
    track = block / 22;
    sector = block % 22;
    side = sector < 11 ? 0 : 1;
    sector = sector % 11;
  }
};

// typedef void (*SearchProcessor)(void* obj, DiskLocation dl, int offset, uint8_t retryCount);

class XCopyDisk
{
  public:
    typedef void (*SearchProcessor)(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);

    XCopyDisk();
    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp);
  
    static void dateTime(uint16_t *date, uint16_t *time);
    void changeDisk();
    String getADFVolumeName(String ADFFileName, ADFFileSource = _sdCard);
    String generateADFFileName(String diskname);

    int readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount, bool silent = false);
    int writeDiskTrack(uint8_t trackNum, uint8_t retryCount);

    bool diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination, bool setEsp = true);
    void adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source, bool setEsp = true);
    void diskToDisk(bool verify, uint8_t retryCount);
    void diskFlux();
    void drawFlux(uint8_t trackNum, uint8_t scale = 2, uint8_t yoffset = 0, bool updateWebUI = true);

    void testDiskette(uint8_t retryCount);
    void scanEmptyBlocks(uint8_t retryCount);
    bool writeBlocksToFile(byte blocks[], uint8_t retryCount);
    bool writeFileToBlocks(String BinFileName, int startBlock, uint8_t retryCount);
    
    int searchMemory(String searchText, byte* memory, size_t memorySize);
    static void processModule(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);
    static void processAscii(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);
    void moduleInfo(int logicalTrack, int sec, int offset, uint8_t retryCount);
    bool search(XCopyDisk* obj, String text, uint8_t retryCount, SearchProcessor processor);
    bool asciiSearch(String text, uint8_t retryCount) { return search(this, text, retryCount, processAscii); }
    bool modSearch(uint8_t retryCount) { return search(this, "M.K.", retryCount, processModule); }

    String ctxToMD5(MD5_CTX *ctx);
    String adfToMD5(String ADFFileName);
    String flashToMD5();

    void cancelOperation();
    void OperationCancelled(uint8_t trackNum = -1);

  private:
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;
    XCopyESP8266 *_esp;
    volatile bool _cancelOperation = false;
};

#endif // XCOPYDISK_H