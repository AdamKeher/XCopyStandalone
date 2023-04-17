#ifndef XCOPYFLOPPY_H
#define XCOPYFLOPPY_H

#define FLOPPY_GAP_BYTES 1482
#define streamSizeDD 12 * 1088 + FLOPPY_GAP_BYTES //11 sectors + gap + spare sector
#define writeSizeDD 11 * 1088 + FLOPPY_GAP_BYTES  //11 sectors + xx bytes gap

#include <Arduino.h>
#include <Streaming.h>

enum XCopyFloppySide
{
    floppy_top = 0,
    floppy_bottom = 1
};

enum XCopyFloppyDirection
{
    floppy_backward = 0,
    floppy_forward = 1
};

struct FloppyPosition
{
    XCopyFloppyDirection direction;
    byte track;
    XCopyFloppySide side;
};

// struct Sector
// {
//     byte format_type;            //0
//     byte track;                  //1
//     byte sector;                 //2
//     byte toGap;                  //3
//     byte os_recovery[16];        //4
//     unsigned long header_chksum; //20
//     unsigned long data_chksum;   //24
//     byte data[512];              //28
// };

// struct Track
// {
//     byte sector[540];
// };

enum FloppyPins
{
    _density = 14,      //2 density select IN
    _index = 15,        //8 index OUT
    _driveSelect = 16,  //12 drive select 1 IN
    _motor = 17,        //16 motor1 on IN
    _direcion = 18,     //18 direction IN
    _step = 19,         //20 step IN
    _writeData = 9,     //22 write data IN
    _writeEnable = 8,   //24 write enable IN
    _track0 = 7,        //26 track 0 OUT
    _writeProtect = 6,  //28 write protect OUT
    _readData = 5,      //30 read data OUT (FTM0_CH1) *** do not change this pin ***
    _side = 4,          //32 head select IN
    _diskChange = 3     //34 disk change OUT
};

struct FloppyDelays
{
    uint32_t driveSelectDelayUs = 100;
    uint32_t driveDeselectDelayUs = 5;
    uint32_t driveSpinUpDelayMs = 600;
    uint32_t driveSpinDownDelayUs = 50;
    uint32_t stepDurationUs = 2;
    uint32_t stepDelayMs = 3;
    uint32_t directionDelayMs = 20;
    uint32_t selectSideDelayMs = 2;
};

class XCopyFloppy {
    public:
        bool detectCableOrientation();
        void setupDrive();
        void motor(bool enable);
        bool getMotorStatus() { return _motorStatus; }
        void driveSelect(bool selected);
        bool getWriteProtect();
        bool getDiskChange();      
        void setDirection(XCopyFloppyDirection direction);
        void setSide(XCopyFloppySide side);        
        void step();
        bool home();
        bool gotoTrack(int trackNumber);
        void readTrack();

    private:
        // 
        int hist[256];
        byte *stream;

        byte low2 = 30;
        byte high2 = 99;
        byte high3 = 155;
        byte high4 = 255;
        byte sectors = 11;
        word timerMode = 0x09;
        word filterSetting = 0;
        int streamLen = streamSizeDD;
        int writeSize = writeSizeDD;
        float transitionTime = 1.96;
        byte retries = 6;

        void initRead();
        //

        FloppyDelays _delays;
        FloppyPosition _position;
        bool _motorStatus = false;

        // FlexTimerModule defines for Pin 5
        uint32_t FTChannelValue = 0x40038048;
        uint32_t FTStatusControlRegister = 0x40038044;
        uint32_t FTPinMuxPort = 0x4004C01C;        

        void pinModeFast(uint8_t pin, uint8_t mode);
        void printPosition() { Serial << "Direction: " << (_position.direction == floppy_backward ? "Backward" : "Forward") << " | Track: " << _position.track << " | Side: " << (_position.side == floppy_bottom ? "Bottom" : "Top")  << "\r\n"; }
};

#endif // XCOPYFLOPPY_H