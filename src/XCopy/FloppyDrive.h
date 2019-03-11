#ifndef FLOPPYDRIVE_H
#define FLOPPYDRIVE_H

#include <Arduino.h>
#include <Streaming.h>

#define BITBAND_ADDR(addr, bit) (((uint32_t) & (addr)-0x20000000) * 32 + (bit)*4 + 0x22000000)

#define timerModeHD 0x08  // TOF=0 TOIE=0 CPWMS=0 CLKS=01 (Sys clock) PS=000 (divide by 1)
#define timerModeDD 0x09  // TOF=0 TOIE=0 CPWMS=0 CLKS=01 (Sys clock) PS=001 (divide by 2)
#define filterSettingDD 0 // 4+4×val clock cycles, 48MHz = 4+4*2 = 32 clock cycles = 0.25us
#define filterSettingHD 0 // 4+4×val clock cycles, 48MHz = 4+4*2 = 32 clock cycles = 0.25us

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

// ADDED FUNCTIONS
void setupDrive();
Track *getTrack();
byte *getStream();
void setAutoDensity(bool setting);
void setCurrentTrack(int track);
bool getWriteProtect();
unsigned int getBitCount();
byte getSectorCnt();
void setSectorCnt(byte count);
void printBootSector();
int *getHist();
byte getWeakTrack();
byte getRetries();
// ADDED FUNCTIONS

void pinModeFast(uint8_t pin, uint8_t mode);
void waitForIndex();
void diskWrite();
void olddiskWrite();
unsigned char reverse(unsigned char b);
int writeTrack();
int readTrack(boolean silent);
String getName();
boolean hdDisk();
void setupFTM0();
void ftm0_isr(void);
void initRead();
void startFTM0();
void stopFTM0();
void motorTimeout();
void motorOn();
void motorOff();
void driveSelect();
void driveDeselect();
void setDir(int dir);
void setSide(int side);
void step1();
int seek0();
int gotoTrack(int track);
void gotoLogicTrack(int track);
void printStatus();
int diskChange();
void bitCounter();
int indexTimer();
int findMinima(int start);
void adjustTimings();
void printHist();
void printFlux();
void analyseHist(boolean silent);
void decodeSector(long secPtr, int index);
unsigned long calcChkSum(long secPtr, int pos, int b);
void decodeTrack(boolean silent);
int getTrackInfo();
void printAmigaSector(int index);
void printTrack();
void dumpSector(int index);
int loadSector(int index);
void downloadTrack();
void uploadTrack();
char byte2char(byte c);
void print_binary(int v, int num_places);
void fillSector(int sect);
unsigned long oddLong(unsigned long odd);
unsigned long evenLonger(unsigned long even);
word mfmByte(byte curr, word prev);
void putLong(unsigned long tLong, byte *dest);
void encodeSector(unsigned long tra, unsigned long sec, byte *src, byte *dest);
void fillTrackGap(byte *dst, int len);
void floppyTrackMfmEncode(unsigned long track, byte *src, byte *dst);
int hardwareVersion();
void registerSetup(int version);
void setMode(int density);
void densityDetect();
void diskChangeIRQ();
void initDrive();
byte getByte(int ptr);

#endif // FLOPPYDRIVE_H