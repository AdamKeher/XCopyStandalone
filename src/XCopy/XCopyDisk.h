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

/**
 * Stores a disk location set by block or track / sector values.
 * 
 * Used to pass disk location information around functions and to translate from blocks to tracks / sectors.
 */
class DiskLocation {
private:
  int _block = 0;
  int _track = 0;
  int _logicalTrack = 0;
  int _side = 0;
  int _sector = 0;
public:
  int block() { return _block; }                //! current block number
  int track() { return _track; }                //! current block track
  int logicalTrack() { return _logicalTrack; }  //! current block logical track
  int side() { return _side; }                  //! current block side
  int sector() { return _sector; }              //! current block sector

  /**
   * @brief set location value by track and sector.
   *        be careful to use logical track values rather than track / side
   * 
   * @param logicalTrack logical track value betweeb 0 and 159. Logical tracks are contiguous without accounting for the side.
   * @param sector sector within logical track
  */
  void setBlock(int logicalTrack, int sector) {
    if (logicalTrack < 0) logicalTrack = 0;
    if (logicalTrack > 159) logicalTrack = 159;

    _logicalTrack = logicalTrack;
    _track = _logicalTrack / 2;
    _side = _logicalTrack % 2;
    _sector = sector;
    _block = (_track * 22) + (_side * 11) + _sector;
  }

  /**
   * @brief set location value by block.
   * 
   * @param block block value between 0 and 1759
  */
  void setBlock(int value) {
    if (value < 0) value = 0;
    if (value > 1759) value = 1759;

    _block = value;
    _logicalTrack = _block / 11;
    _track = _block / 22;
    _sector = _block % 22;
    _side = _sector < 11 ? 0 : 1;
    _sector = _sector % 11;
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
    static SearchResult processAscii(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);
    static SearchResult processModule(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount);    
    int searchMemory(String searchText, byte* memory, size_t memorySize);
    bool search(String text, uint8_t retryCount, SearchProcessor processor);
    bool asciiSearch(String text, uint8_t retryCount) { 
      Log << "Ascii Search: '" + text + "'\r\n";
      return search(text, retryCount, processAscii);
    }
    bool modSearch(uint8_t retryCount) { 
      Log << "Tracker Module Search\r\n";
      return search("\033MOD", retryCount, processModule); 
    }
    void loadModuleHeader(DiskLocation dl, int offset, ModInfo* modinfo, uint8_t retryCount);
    bool modRip(DiskLocation dl, int offset, int size, uint8_t retryCount);

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