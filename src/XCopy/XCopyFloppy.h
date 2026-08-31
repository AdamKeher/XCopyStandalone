#ifndef XCOPYFLOPPY_H
#define XCOPYFLOPPY_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyLog.h"
#include "../FastCRC/FastCRC.h"

#define BITBAND_ADDR(addr, bit) (((uint32_t) & (addr)-0x20000000) * 32 + (bit)*4 + 0x22000000)

#define timerModeHD 0x08  // TOF=0 TOIE=0 CPWMS=0 CLKS=01 (Sys clock) PS=000 (divide by 1)
#define timerModeDD 0x09  // TOF=0 TOIE=0 CPWMS=0 CLKS=01 (Sys clock) PS=001 (divide by 2)
#define filterSettingDD 0 // 4+4x val clock cycles, 48MHz = 4+4*2 = 32 clock cycles = 0.25us
#define filterSettingHD 0 // 4+4x val clock cycles, 48MHz = 4+4*2 = 32 clock cycles = 0.25us

#define motorMaxTick 5   // Idle Seconds before Motor off
#define maxRetries 6     // maximum retries to read a track
#define transTimeDD 1.96 // timing for write transitions
#define transTimeHD 0.98 // timing for write transitions

#define FLOPPY_GAP_BYTES 1482

#define streamSizeHD 23 * 1088 + FLOPPY_GAP_BYTES //22 sectors + gap + spare sector
#define streamSizeDD 12 * 1088 + FLOPPY_GAP_BYTES //11 sectors + gap + spare sector
#define writeSizeDD 11 * 1088 + FLOPPY_GAP_BYTES  //11 sectors + xx bytes gap
#define writeSizeHD 22 * 1088 + FLOPPY_GAP_BYTES  //22 sectors + xx bytes gap

#define MFM_MASK 0x55555555

#define RPM_WINDOW 11 //index timestamps kept -> 10 intervals

#define HD 1
#define DD 0

struct tfloppyPos
{
    byte track;
    byte side;
    byte dir;
};

struct SectorTable
{
    unsigned long bytePos;
    byte sector;
};

struct Track
{
    byte sector[540];
};

struct Sector
{
    byte format_type;            //0
    byte track;                  //1
    byte sector;                 //2
    byte toGap;                  //3
    byte os_recovery[16];        //4
    unsigned long header_chksum; //20
    unsigned long data_chksum;   //24
    byte data[512];              //28
};

/*
   returns c if printable, else returns delim
*/
char byte2char(byte c, char delim = '.');

/*
   Amiga floppy drive.

   The flux capture and write paths run from interrupts that fire every few
   microseconds, so the state those ISRs touch stays as file scope statics in
   XCopyFloppy.cpp rather than becoming members: reaching it through an
   instance pointer would add a load per access inside ftm0_isr. Everything the
   ISRs do not touch - the decoded track buffer, head position, error and retry
   bookkeeping - lives here. There is one drive, so there is one instance.
*/
class XCopyFloppy
{
  public:
    void setupDrive();

    // decoded track buffer, raw mfm stream and flux histogram
    Track *getTrack();
    byte *getStream();
    int *getHist();

    // configuration
    void setAutoDensity(bool setting);
    void setCurrentTrack(int track);
    void setMode(int density);
    void setSectorCnt(byte count);

    // drive and disk status
    bool getWriteProtect();
    bool getMotorStatus();
    bool detectCableOrientation();
    int diskChange(); // 1 = disk inserted, 0 = no disk
    unsigned int getBitCount();
    byte getSectorCnt();
    byte getWeakTrack();
    byte getRetries();
    int getTrackInfo();
    String getName();

    // pin numbers, for callers attaching their own interrupts
    int indexPin() const;
    int driveSelectPin() const;
    int motorPin() const;
    int diskChangePin() const;

    // motor and head
    void motorOn();
    void motorOff();
    int seek0();
    void gotoLogicTrack(int track);

    // track transfer
    int readTrack(boolean silent);
    int writeTrack();
    void floppyTrackMfmEncode(unsigned long track, byte *src, byte *dst);

    // drive speed, measured from the index pulse
    void beginRPM(); // caller must have the drive spinning
    float readRPM();
    void endRPM();

    // debug output
    uint32_t bootSectorCRC32();
    void printBootSector();
    void printTrack();
    void printAmigaSector(int index);
    void printHist();
    void printFlux();
    void printStatus();
    void analyseHist(boolean silent);

  private:
    Track _track[22];
    tfloppyPos _floppyPos;
    byte _weakTracks[168];
    byte _trackLog[168];
    long _errors = 0;
    String _extError;
    int _currentTrack = -1;
    int _logTrack = -1;
    int _retries = maxRetries;
    int _mode = 0;
    boolean _autoDensity = true;
    IntervalTimer _motorTimer;
    IntervalTimer _writeTimer;

    // hardware setup
    int hardwareVersion();
    void registerSetup(int version);
    void setupFTM0();
    void initRead();
    void startFTM0();
    void stopFTM0();
    void initDrive();
    void densityDetect();
    boolean hdDisk();

    // head movement
    void setDir(int dir);
    void setSide(int side);
    void step1();
    int gotoTrack(int track);
    void waitForIndex();
    int indexTimer();

    // mfm decode
    void decodeSector(long secPtr, int index);
    unsigned long calcChkSum(long secPtr, int pos, int b);
    void decodeTrack(boolean silent);
    int findMinima(int start);
    void adjustTimings();

    // mfm encode
    void encodeSector(unsigned long tra, unsigned long sec, byte *src, byte *dest);
    void fillTrackGap(byte *dst, int len);
    void fillSector(int sect);

    // serial transfer of a single track, for debugging
    void dumpSector(int index);
    int loadSector(int index);
    void downloadTrack();
    void uploadTrack();
    byte getByte(int ptr);
};

#endif // XCOPYFLOPPY_H
