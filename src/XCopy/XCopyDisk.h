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
#include "XCopyModFile.h"
#include "MD5.h"

enum ADFFileSource
{
  _flashMemory = 0,
  _sdCard = 1
};

struct SearchResult {
  int block = 0;
  int offset = 0;
  int size = 0;
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

class XCopyDisk
{
  public:
    typedef SearchResult (*SearchProcessor)(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);

    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp);

    // UI
    void changeDisk();
    void drawFlux(uint8_t trackNum, uint8_t scale = 2, uint8_t yoffset = 0, bool updateWebUI = true);
  
    // File & Volume Names
    static void dateTime(uint16_t *date, uint16_t *time);
    String getADFVolumeName(String ADFFileName, ADFFileSource = _sdCard);
    String generateADFFileName(String diskname);

    // Cancel
    void cancelOperation();
    void OperationCancelled(uint8_t trackNum = -1);

    // Read & Write tracks
    int readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount, bool silent = false);
    int writeDiskTrack(uint8_t trackNum, uint8_t retryCount);

    // ADF Disk
    bool diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination, bool setEsp = true);
    void adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source, bool setEsp = true);
    void diskToDisk(bool verify, uint8_t retryCount);
    void diskFlux();
    void testDiskette(uint8_t retryCount);
    void scanEmptyBlocks(uint8_t retryCount);

    // Block
    bool writeBlocksToFile(byte blocks[], int offset, int size, String fileextension, uint8_t retryCount);
    bool writeFileToBlocks(String BinFileName, int startBlock, uint8_t retryCount);
    
    // Search
    int searchMemory(String searchText, byte* memory, size_t memorySize);
    static SearchResult processAscii(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);
    bool search(XCopyDisk* obj, String text, uint8_t retryCount, SearchProcessor processor);
    bool asciiSearch(String text, uint8_t retryCount) { 
      Log << "Ascii Search: '" + text + "'\r\n";
      return search(this, text, retryCount, processAscii);
    }

    // Search - Tracker Module
    bool modSearch(uint8_t retryCount) { 
      Log << "Tracker Module Search\r\n";
      // TODO: Add more magic strings for different trackers
      return search(this, "M.K.", retryCount, processModule); 
    }
    void loadModuleHeader(DiskLocation dl, ModInfo* modinfo, int offset, uint8_t retryCount);
    static SearchResult processModule(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);    
    bool modRip(int block, int offset, int size, uint8_t retryCount);

    // MD5
    String ctxToMD5(MD5_CTX *ctx);
    String adfToMD5(String ADFFileName);
    String flashToMD5();

  private:
    XCopyESP8266 *_esp;
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;
    volatile bool _cancelOperation = false;
};

#endif // XCOPYDISK_H