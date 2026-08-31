#include "XCopyFloppy.h"

// --- state the interrupt handlers touch --------------------------------------
// NOTE: deliberately NOT static. ftm0_isr and diskWrite() write most of this
// behind the compiler's back and much of it is not volatile, so internal
// linkage would let GCC reason about it across the whole file as though the
// interrupts never fired. External linkage keeps the codegen this driver has
// always been built with.
// ftm0_isr fires on every flux transition (roughly every 4us on a DD disk) and
// diskWrite() on every write transition, so the state they reach stays at file
// scope with internal linkage. Routing it through an instance pointer would add
// a load per access inside those handlers. Everything the handlers do not touch
// is member state on XCopyFloppy.

// pins, assigned by registerSetup()
int _dens, _index, _drivesel, _motor, _dir, _step, _writedata, _writeen, _track0, _wprot, _readdata, _side, _diskChange;
uint32_t FTChannelValue, FTStatusControlRegister, FTPinMuxPort;

// flux capture
byte *stream;
int *streamBitband;
struct SectorTable sectorTable[25];
int hist[256];
volatile int sample = 0;
volatile unsigned long readBuff;
volatile byte bCnt = 0;
volatile int readPtr;
volatile boolean recordOn = false;
volatile unsigned int bitCount;
volatile byte sectorCnt;
int streamLen = streamSizeDD;

// read timings, defaults are for DD disks
byte low2 = 30;
byte high2 = 115;
byte high3 = 155;
byte high4 = 255;
byte sectors = 11;
word timerMode = timerModeDD;
word filterSetting = filterSettingDD;

// write path
volatile boolean writeActive;
volatile byte dataByte;
long writePtr;
int writeBitCnt;
int writeSize = writeSizeDD;
float transitionTime = transTimeDD;

// motor idle timeout and disk change
volatile int motorTick = 0;
volatile boolean motor = false;

// --- interrupt handlers and pure helpers ------------------------------------
void pinModeFast(uint8_t pin, uint8_t mode);
void driveSelect();
void driveDeselect();
void motorOffRaw();
void motorTimeout();
void diskWrite();
void diskChangeIRQ();
void bitCounter();
void readIndexISR();
unsigned char reverse(unsigned char b);
unsigned long oddLong(unsigned long odd);
unsigned long evenLonger(unsigned long even);
word mfmByte(byte curr, word prev);
void putLong(unsigned long tLong, byte *dest);


/*
   sets teensy pin to fastmode if OUTPUT
   slightly modified pinMode function from
   \hardware\teensy\avr\cores\teensy3\pins_teensy.c
*/
void pinModeFast(uint8_t pin, uint8_t mode)
{
    volatile uint32_t *config;

    if (pin >= CORE_NUM_DIGITAL)
        return;
    config = portConfigRegister(pin);

    if (mode == OUTPUT || mode == OUTPUT_OPENDRAIN)
    {
#ifdef KINETISK
        *portModeRegister(pin) = 1;
#else
        *portModeRegister(pin) |= digitalPinToBitMask(pin); // TODO: atomic
#endif
        *config = PORT_PCR_DSE | PORT_PCR_MUX(1);
        if (mode == OUTPUT_OPENDRAIN)
        {
            *config |= PORT_PCR_ODE;
        }
        else
        {
            *config &= ~PORT_PCR_ODE;
        }
    }
    else
    {
#ifdef KINETISK
        *portModeRegister(pin) = 0;
#else
        *portModeRegister(pin) &= ~digitalPinToBitMask(pin);
#endif
        if (mode == INPUT || mode == INPUT_PULLUP || mode == INPUT_PULLDOWN)
        {
            *config = PORT_PCR_MUX(1);
            if (mode == INPUT_PULLUP)
            {
                *config |= (PORT_PCR_PE | PORT_PCR_PS); // pullup
            }
            else if (mode == INPUT_PULLDOWN)
            {
                *config |= (PORT_PCR_PE); // pulldown
                *config &= ~(PORT_PCR_PS);
            }
        }
        else
        {
            *config = PORT_PCR_MUX(1) | PORT_PCR_PE | PORT_PCR_PS; // pullup
        }
    }
}

// ADDED FUNCTIONS

void XCopyFloppy::setupDrive()
{
    registerSetup(1);
    pinMode(_dens, OUTPUT);
    digitalWriteFast(_dens, HIGH);
    pinMode(_index, INPUT_PULLUP);
    pinMode(_motor, OUTPUT);
    digitalWriteFast(_motor, HIGH);
    pinMode(_drivesel, OUTPUT);
    digitalWriteFast(_drivesel, HIGH);
    pinMode(_dir, OUTPUT);
    digitalWriteFast(_dir, HIGH);
    pinMode(_step, OUTPUT);
    digitalWriteFast(_step, HIGH);
    pinModeFast(_writedata, OUTPUT);
    digitalWriteFast(_writedata, HIGH);
    pinMode(_writeen, OUTPUT);
    digitalWriteFast(_writeen, HIGH);
    pinMode(_track0, INPUT_PULLUP);
    pinMode(_wprot, INPUT_PULLUP);
    pinMode(_side, OUTPUT);
    digitalWriteFast(_side, HIGH);
    pinMode(_diskChange, INPUT_PULLUP);

    // pinMode(13, OUTPUT);
    attachInterrupt(_diskChange, diskChangeIRQ, FALLING);

    _motorTimer.priority(255);
    _motorTimer.begin(motorTimeout, 1000000);

    _extError.reserve(31);

    // The read buffer has to live in SRAM_U, at 0x20000000 and above: writeTrack()
    // reaches it through streamBitband, and the bit-band alias region only maps the
    // upper RAM bank. The heap starts at the end of .bss down in SRAM_L, so claim a
    // filler block spanning the rest of SRAM_L first, take the buffer above it, then
    // hand the filler back.
    //
    // The filler is sized from wherever the heap currently is, so this does not
    // depend on how much anything earlier in begin() happened to allocate - but the
    // result is still checked below, because being wrong here is silent otherwise.
    byte *filler;
    filler = (byte *)malloc(1);

    long fillerSize = 0x20000000 - (long)filler;
    free(filler);
    filler = (fillerSize > 0) ? (byte *)malloc(fillerSize) : NULL;

    stream = (byte *)malloc(streamSizeHD + 10);

    if (filler != NULL)
        free(filler);

    if (stream == NULL)
    {
        Serial.println("Out of memory allocating the track buffer");
        while (1)
            ;
    }

    if ((uint32_t)stream < 0x20000000)
    {
        // Carrying on would alias the buffer to a wild address the first time a
        // track is written, so stop here rather than corrupt memory later.
        Serial.println("Track buffer landed below 0x20000000, bit-band aliasing invalid");
        while (1)
            ;
    }

    streamBitband = (int *)BITBAND_ADDR(*stream, 0);
    initRead();
    _floppyPos.dir = 0;
    _floppyPos.side = 0;
    _floppyPos.track = 0;
}

Track *XCopyFloppy::getTrack()
{
    return &_track[0];
}

byte *XCopyFloppy::getStream()
{
    return stream;
}

void XCopyFloppy::setAutoDensity(bool setting)
{
    _autoDensity = setting;
}

void XCopyFloppy::setCurrentTrack(int track)
{
    _currentTrack = track;
}

bool XCopyFloppy::getWriteProtect()
{
    return !digitalRead(_wprot);
}

unsigned int XCopyFloppy::getBitCount()
{
    return bitCount;
}

byte XCopyFloppy::getSectorCnt()
{
    return sectorCnt;
}

void XCopyFloppy::setSectorCnt(byte count)
{
    sectorCnt = count;
}

uint32_t XCopyFloppy::bootSectorCRC32() {
    FastCRC32 CRC32;
    uint32_t boot_crc32 = 0;
    struct Sector *aSec;

    aSec = (Sector *)&_track[0].sector;
    boot_crc32 = CRC32.crc32(aSec->data, 512);
    
    aSec = (Sector *)&_track[1].sector;
    boot_crc32 = CRC32.crc32_upd(aSec->data, 512);

    return boot_crc32;
}

void XCopyFloppy::printBootSector()
{
    struct Sector *aSec = (Sector *)&_track[0].sector;
    Log << "Format Type: " + String(aSec->format_type) + " Track: " + String(aSec->track);
    Log << " Sector: " + String(aSec->sector) + " NumSec2Gap: " + String(aSec->toGap);
    Log << " Data Chk: ";
    Log << String(aSec->data_chksum, HEX);
    Log << " Header Chk: ";
    Log << String(aSec->header_chksum, HEX);
    Log << "\r\n";
    Log << ".-------------------------------------------------------------------------.\r\n";
    Log << "| Boot Sector                                                             |\r\n";
    Log << "|-------------------------------------------------------------------------|\r\n";

    for (int s = 0; s < 2; s++)
    {
        aSec = (Sector *)&_track[s].sector;
        for (int i = 0; i < 8; i++)
        {
            String line = "| 0x";
            String hex = String((s * 512) + (i * 64), HEX);
            line.append(hex.length() < 3 ? String("0000000").substring(0, 3 - hex.length()) + hex : hex);
            line.append(": ");
            for (int j = 0; j < 64; j++)
            {
                line.append(byte2char(aSec->data[(i * 64) + j]));
            }
            line.append(" |\r\n");
            Log << line;
        }
    }

    Log << "|-------------------------------------------------------------------------'------------------------------.\r\n";
    for (int s = 0; s < 2; s++)
    {
        aSec = (Sector *)&_track[s].sector;
        for (int i = 0; i < 16; i++)
        {
            String line = "| 0x";
            String hex = String((s * 512) + (i * 32), HEX);
            line.append(hex.length() < 3 ? String("0000000").substring(0, 3 - hex.length()) + hex : hex).append(": ");
            for (int j = 0; j < 32; j++)
            {
                if (aSec->data[(i * 32) + j] < 16)
                {
                    line.append("0");
                }
                line.append(String(aSec->data[(i * 32) + j], HEX)).append(" ");
            }
            line.append("|\r\n");
            Log << line;
        }
    }
    Log << "`--------------------------------------------------------------------------------------------------------'\r\n";

    char hexvalue[10];
    sprintf(hexvalue, "%08x", (unsigned int)bootSectorCRC32());

    Log << "crc32: 0x" + String(hexvalue) + "\r\n";
}

int *XCopyFloppy::getHist()
{
    return hist;
}

byte XCopyFloppy::getWeakTrack()
{
    if (_logTrack < 0 || _logTrack >= (int)sizeof(_weakTracks))
        return 0;
    return _weakTracks[_logTrack];
}

byte XCopyFloppy::getRetries()
{
    return _retries;
}

// ADDED FUNCTIONS

void driveSelect()
{
    digitalWriteFast(_drivesel, LOW);
    delayMicroseconds(100);
}

void driveDeselect()
{
    digitalWriteFast(_drivesel, HIGH);
    delayMicroseconds(5);
}

/*
   starts the motor if it isnt running yet
*/
void XCopyFloppy::motorOn()
{
    motorTick = 0;
    if (motor == false)
    {
        driveSelect();
        digitalWriteFast(_motor, LOW);
        motor = true;
        delay(600); // more than plenty of time to spinup motor
        if (_autoDensity)
            initDrive();
    }
}

/*
   stops motor and deselects drive

   motorTimeout() and diskChangeIRQ() both have to shut the motor down and
   neither can call a member, so the body lives here. It only touches file scope
   state, so no instance is needed.
*/
void motorOffRaw()
{
    motor = false;
    digitalWriteFast(_motor, HIGH);
    delayMicroseconds(50);
    driveDeselect();
}

void XCopyFloppy::motorOff()
{
    motorOffRaw();
}

/*
   true while the spindle motor is energised
*/
bool XCopyFloppy::getMotorStatus()
{
    return motor;
}

int XCopyFloppy::indexPin() const { return _index; }
int XCopyFloppy::driveSelectPin() const { return _drivesel; }
int XCopyFloppy::motorPin() const { return _motor; }
int XCopyFloppy::diskChangePin() const { return _diskChange; }

/*
   measures time for one rotation of the disk, returns milliseconds
*/
int XCopyFloppy::indexTimer()
{
    motorOn();
    attachInterrupt(_readdata, bitCounter, FALLING);
    while (digitalRead(_index) == 1)
        ;
    while (digitalRead(_index) == 0)
        ;
    long tRead = micros();
    bitCount = 0;
    delay(5);
    while (digitalRead(_index) == 1)
        ;
    while (digitalRead(_index) == 0)
        ;
    detachInterrupt(_readdata);
    tRead = micros() - tRead;
    return (tRead / 1000);
}

/*
   index pulse RPM measurement: the ISR keeps a rolling buffer of the last
   RPM_WINDOW edge timestamps, so readRPM() can be called at any time and always
   reports the most recent full revolutions. Unlike indexTimer() this never
   blocks, so the caller can keep drawing or polling while the drive spins.
*/
static volatile uint32_t indexTimes[RPM_WINDOW];
static volatile uint8_t indexHead = 0;
static volatile uint8_t indexSamples = 0;
static bool rpmRunning = false;
static const uint32_t rpmStallUs = 3000000;

void readIndexISR()
{
    indexTimes[indexHead] = micros();
    indexHead = (indexHead + 1) % RPM_WINDOW;
    if (indexSamples < RPM_WINDOW)
        indexSamples++;
}

/*
   caller must have the drive spinning
*/
void XCopyFloppy::beginRPM()
{
    if (rpmRunning)
        return;

    noInterrupts();
    indexHead = 0;
    indexSamples = 0;
    interrupts();

    attachInterrupt(_index, readIndexISR, FALLING);
    rpmRunning = true;
}

void XCopyFloppy::endRPM()
{
    if (!rpmRunning)
        return;
    detachInterrupt(_index);
    rpmRunning = false;
}

/*
   speed over the edges currently in the buffer. Safe to call as often as you
   like: it reports the newest complete window, or 0 when the drive is not
   turning or the buffer has not filled with at least two edges yet.
*/
float XCopyFloppy::readRPM()
{
    uint32_t times[RPM_WINDOW];
    uint8_t n, head;

    noInterrupts();
    n = indexSamples;
    head = indexHead;
    for (uint8_t i = 0; i < RPM_WINDOW; i++)
        times[i] = indexTimes[i];
    interrupts();

    if (n < 2)
        return 0.0f;

    // when the buffer is full head points at the oldest entry, otherwise the
    // samples simply run 0..n-1 and head sits one past the newest
    uint8_t oldest = (n == RPM_WINDOW) ? head : 0;
    uint8_t newest = (head + RPM_WINDOW - 1) % RPM_WINDOW;

    // the drive stopped or the disk was pulled part way through a window
    if (micros() - times[newest] > rpmStallUs)
        return 0.0f;

    uint32_t spanUs = times[newest] - times[oldest];
    if (spanUs == 0)
        return 0.0f;

    return (60000000.0f * (n - 1)) / spanUs;
}

void XCopyFloppy::densityDetect()
{
    indexTimer();
    if (bitCount > 60000)
    {
        setMode(HD);
    }
    else
    {
        setMode(DD);
    }
}

void XCopyFloppy::initDrive()
{
    densityDetect();
    digitalWrite(13, LOW);
}

/*
   wait for index hole
*/
void XCopyFloppy::waitForIndex()
{
    motorOn();
    while (digitalRead(_index) == 1)
        ;
    while (digitalRead(_index) == 0)
        ;
}

/*
   interrupt routine for writing data to floppydrive
*/
void diskWrite()
{
    if (writeActive == false)
        return;
    digitalWriteFast(_writedata, !streamBitband[writePtr]);
    writePtr++;
    if (writePtr >= ((writeSize * 8) + 8))
    {
        writeActive = false;
        digitalWriteFast(_writedata, HIGH);
    }
}


unsigned char reverse(unsigned char b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}
/*
   writes a track from buffer to the floppydrive
   return -1 if disk is write protected
*/
int XCopyFloppy::writeTrack()
{
    motorOn();
    _motorTimer.end();
    if (digitalRead(_wprot) == 0)
    {
        _motorTimer.begin(motorTimeout, 1000000);
        _extError = "Disk is write-protected\n";
        return -1;
    }
    for (int i = 0; i < writeSize; i++)
    {
        stream[i] = reverse(stream[i]);
    }
    writePtr = 0;
    writeBitCnt = 0;
    dataByte = stream[writePtr];
    writeActive = true;
    _writeTimer.priority(0);
    delayMicroseconds(100);
    waitForIndex();
    //  int zeit = millis();
    _writeTimer.begin(diskWrite, transitionTime);
    digitalWriteFast(_writeen, LOW); // enable writegate after starting timer because first interrupt
    // occurs in about 2µs
    while (writeActive == true)
    {
    }
    _writeTimer.end();
    //  zeit = millis() - zeit;
    //  Serial << "Time taken: " << zeit << "ms\n";
    delayMicroseconds(2);
    digitalWriteFast(_writeen, HIGH);
    delay(5);
    _motorTimer.begin(motorTimeout, 1000000);
    return 0;
}

/*
   reads a track from the floppy disk
   optional parameter silent to supress all debug messages
*/
int XCopyFloppy::readTrack(boolean silent)
{
    setMode(_mode);
    int j = 0;
    for (j = 0; j < _retries; j++)
    {
        for (int i = 0; i < 256; i++)
        {
            hist[i] = 0;
        }
        motorOn();
        initRead();
        startFTM0();
        long tZeit = millis();
        while (recordOn)
        {
            if ((millis() - tZeit) > 300)
            {
                if (!silent)
                {
                    Log << "Timeout reached\r\n";
                }
                stopFTM0();
                _errors = -1;
                _extError = "Timeout in read operation\r\n";
            }
        }
        tZeit = micros();
        decodeTrack(silent);
        tZeit = micros() - tZeit;
        if (!silent)
        {
            Log << "Decode took " + String(tZeit) + "\r\n";
        }
        if (getTrackInfo() != _logTrack)
        {
            _errors = -1;
            _extError = "Seek error\r\n";
        }
        if ((_errors == 0) && (sectorCnt == sectors))
        {
            _extError = "OK\r\n";
            break;
        }
        if (!silent)
        {
            Log << "Read Error, _retries left: " + String(_retries - j) + " CurrentTrack: " + String(_logTrack) + " error: " + String(_errors, BIN) + "\r\n";
        }
        // the following code tries to move the stepper / seek 0 before retrying to read the track
        // but i found out that about 6 _retries are sufficient to determine if a track is bad
        // but you may enable it and set _retries to 25 if you want to give it a shot

        adjustTimings();
        int tempTrack = _logTrack;
        switch (j)
        {
        case 5:
            gotoLogicTrack(tempTrack - 2);
            gotoLogicTrack(tempTrack);
            break;
        case 10:
            gotoLogicTrack(tempTrack + 2);
            gotoLogicTrack(tempTrack);
            delay(100);
            break;
        case 15:
            _currentTrack = -1;
            gotoLogicTrack(tempTrack);
            delay(100);
            break;
        case 20:
            _currentTrack = -1;
            gotoLogicTrack(tempTrack);
            delay(100);
            break;
        default:
            // do nothing
            break;
        }
    }
    // _logTrack is -1 until gotoLogicTrack() has run, and the retry path above can
    // briefly drive it negative near track 0, so do not index on it blind.
    if (_logTrack >= 0 && _logTrack < (int)sizeof(_weakTracks))
    {
        _weakTracks[_logTrack] = j;
        _trackLog[_logTrack] = getTrackInfo();
    }
    if (sectorCnt != sectors)
    {
        _errors = -1;
        _extError = "Incorrect number of sectors on track\r\n";
    }
    return _errors;
}

/*
   read Diskname from Track 80
*/
String XCopyFloppy::getName()
{
    String volumeName;
    gotoLogicTrack(80);
    readTrack(true);
    struct Sector *aSec = (Sector *)&_track[0].sector;
    volumeName = "NDOS";
    int nameLen = aSec->data[432];
    if (nameLen > 30)
        return "NDOS";
    int temp = 0;
    for (int i = 0x04; i < 0x0c; i++)
    {
        temp += aSec->data[i];
    }
    for (int i = 0x10; i < 0x14; i++)
    {
        temp += aSec->data[i];
    }
    for (int i = 463; i < 472; i++)
    {
        temp += aSec->data[i];
    }
    for (int i = 496; i < 504; i++)
    {
        temp += aSec->data[i];
    }
    if (temp != 0)
        return "NDOS";
    for (int i = 0; i < 4; i++)
    {
        temp += aSec->data[i];
    }
    if (temp != 2)
        return "NDOS";
    temp = 0;
    for (int i = 508; i < 512; i++)
    {
        temp += aSec->data[i];
    }
    if (temp != 1)
        return "NDOS";
    volumeName = "";
    for (int i = 0; i < nameLen; i++)
    {
        volumeName += (char)aSec->data[433 + i];
    }
    // printDiskName(volumeName);
    return volumeName;
}

/*
   Tests for HD Disk by switching drive to 360 rpm mode and mesuring index signals
   when no HD disk is present the drive ignores 360 rpm mode
*/
boolean XCopyFloppy::hdDisk()
{
    digitalWriteFast(_dens, LOW);
    int rotTimer = indexTimer();
    digitalWriteFast(_dens, HIGH);
    if (rotTimer < 180)
        return true;
    return false;
}

/*
   Initializes the Registers for the FlexTimer0 Module
*/
void XCopyFloppy::setupFTM0()
{
    // Input Filter waits for n cycles of stable input
    FTM0_FILTER = filterSetting;

    // Enable the FlexTimerModule and write registers
    // FAULTIE=0, FAULTM=00, CAPTEST=0, PWMSYNC=0, WPDIS=1, INIT=0, FTMEN=1
    FTM0_MODE = 0x05;

    // Initialize Timer registers
    FTM0_SC = 0x00;                                         // Diable Interrupts and Clocksource before initialization
    FTM0_CNT = 0x0000;                                      // set counter value to 0
    FTM0_MOD = 0xFFFF;                                      // set modulo to max value
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x48; // CHF=0  CHIE=1 (enable interrupt)
    // MSB=0  MSA=0 (Channel Mode Input Capture)
    // ELSB=1 ELSA=0 (Input Capture on falling edge)
    // DMA=0  DMA off

    // Enable FTM0 interrupt inside NVIC
    NVIC_SET_PRIORITY(IRQ_FTM0, 0);
    NVIC_ENABLE_IRQ(IRQ_FTM0);
    (*(volatile uint32_t *)FTPinMuxPort) = 0x403; // setup pin for Input Capture FTM0 in Pin Mux
}

/*
   Interrupt Service Routine for FlexTimer0 Module
*/
extern "C" void ftm0_isr(void)
{
    sample = (*(volatile uint32_t *)FTChannelValue);
    // Reset count value
    FTM0_CNT = 0x0000;

    (*(volatile uint32_t *)FTStatusControlRegister) &= ~0x80; // clear channel event flag
    // skip too short / long samples, occur usually in the track gap
    bitCount++;
    if (sample > high4)
    {
        return;
    }
    if (sample < low2)
    {
        return;
    }
    // fills buffer according to transition length with 10, 100 or 1000 (4,6,8µs transition)
    readBuff = (readBuff << 2) | B10;
    bCnt += 2;
    if (sample > high2)
    {
        readBuff = readBuff << 1;
        bCnt++;
    }
    if (sample > high3)
    {
        readBuff = readBuff << 1;
        bCnt++;
    }
    if (bCnt >= 8) // do we have a complete byte?
    {
        stream[readPtr] = readBuff >> (bCnt - 8); // store byte in streambuffer
        bCnt = bCnt - 8;                          // decrease bit count by 8
        readPtr++;                                // adjust pointer to next byte in stream
    }
    if (readBuff == 0xA4489448)
    { // look for magic word. usually 44894489, but detecting this way its
        // easier to byte align the received bitstream from floppy
        if (sectorCnt < sectors)
        { // as long we dont have x sectors store the sector start in a table
            sectorTable[sectorCnt].bytePos = readPtr - 7;
            sectorCnt = sectorCnt + 1;
            bCnt = 4; // set bit count to 4 to align to byte
        }
    }
    hist[sample]++;          // add sample to histogram
    if (readPtr > streamLen) // stop when buffer is full
    {
        recordOn = false;
        FTM0_SC = 0x00; // Timer off
    }
}

/*
   Initializes Variables for reading a track
*/
void XCopyFloppy::initRead()
{
    bCnt = 0;
    readPtr = 0;
    bitCount = 0;
    sectorCnt = 0;
    _errors = 0;
    _extError = "OK\n";

    for (int i = 0; i < streamLen; i++)
    {
        stream[i] = 0x00;
    }
    setupFTM0();
}

/*
   starts input capture
*/
void XCopyFloppy::startFTM0()
{
    recordOn = true;
    FTM0_CNT = 0x0000; // Reset the count to zero
    FTM0_SC = timerMode;
}

/*
   stops input capture
*/
void XCopyFloppy::stopFTM0()
{
    recordOn = false;
    FTM0_SC = 0x00; // Timer off
}

/*
   Interrupt routine for Motor idle timeout, gets called once per second
*/
void motorTimeout()
{
    motorTick++;
    if (motorTick > motorMaxTick)
    {
        motorOffRaw();
    }
}

/*
   selects travel direction of head
   dir = 0   outwards to track 0
   dir !=0 inwards to track 79
*/
void XCopyFloppy::setDir(int dir)
{
    motorTick = 0;
    _floppyPos.dir = dir;
    if (dir == 0)
    {
        digitalWriteFast(_dir, HIGH);
    }
    else
    {
        digitalWriteFast(_dir, LOW);
    }
    delay(20);
}

/*
   selects side to read/write
   side = 0  upper side
   side != 0 lower side
*/
void XCopyFloppy::setSide(int side)
{
    _floppyPos.side = side;
    // printTrack(_floppyPos.track, _floppyPos.side);
    motorTick = 0;
    if (side == 0)
    {
        digitalWriteFast(_side, HIGH);
    }
    else
    {
        digitalWriteFast(_side, LOW);
    }
    delay(2);
}

/*
   steps one track into the direction selected by setDir()
*/
void XCopyFloppy::step1()
{
    motorTick = 0;
    digitalWriteFast(_step, LOW);
    delayMicroseconds(2);
    digitalWriteFast(_step, HIGH);
    delay(3);
    if (_floppyPos.dir == 0)
    {
        _floppyPos.track--;
    }
    else
    {
        _floppyPos.track++;
    }
    // printTrack(_floppyPos.track, _floppyPos.side);
}

/*
   move head to track 0
*/
int XCopyFloppy::seek0()
{
    motorOn();
    int trkCnt = 0;
    setDir(0);
    while (digitalRead(_track0) == 1)
    {
        step1();
        trkCnt++;
        if (trkCnt > 85)
        {
            _extError = "Seek Error Track 0\n";
            return -1;
        }
    }
    _currentTrack = 0;
    _floppyPos.track = 0;
    // printTrack(_floppyPos.track, _floppyPos.side);
    _extError = "OK\n";
    return 0;
}

/*
   moves head to physical track (0-xx)
*/
int XCopyFloppy::gotoTrack(int track)
{
    motorOn();
    if (track == 0)
    {
        _currentTrack = -1;
    }
    int steps = 0;
    if (track < 0)
        return -1;
    if (_currentTrack == -1)
    {
        if (seek0() == -1)
        {
            return -1;
        }
    }
    if (track == _currentTrack)
    {
        return 0;
    }
    if (track < _currentTrack)
    {
        setDir(0);
        steps = _currentTrack - track;
        _currentTrack = track;
    }
    else
    {
        setDir(1);
        steps = track - _currentTrack;
        _currentTrack = track;
    }
    for (int i = 0; i < steps; i++)
    {
        step1();
    }
    return 0;
}

/*
   moves head to logical amiga track (0-159)
*/
void XCopyFloppy::gotoLogicTrack(int track)
{
    _logTrack = track;
    setSide(track % 2);
    gotoTrack(track / 2);
    delay(18);
}

/*
   prints some stuff, mostly for debugging disk signals
*/

void XCopyFloppy::printStatus()
{
    driveSelect();
    setDir(0);
    step1();
    Serial.print(" Trk0: ");
    Serial.print(digitalRead(_track0));
    Serial.print(" WProt: ");
    Serial.print(digitalRead(_wprot));
    Serial.print(" Disk Change: ");
    Serial.println(digitalRead(_diskChange));
}

/*
   checks if disk has changed / is inserted, do one step and check status again because the signal gets
   updated after one step
   returns 1 = disk is in drive, 0 = do disk in drive
*/
int XCopyFloppy::diskChange() // returns if a disk is inserted: 0 = no disk, 1 = disk inserted
{
    driveSelect();
    delay(50);
    //motorOn();
    int rdy = digitalRead(_diskChange);
    if (rdy == 1)
    {
        return 1;
    }
    else
    {
        setDir(0);
        step1();
        return digitalRead(_diskChange);
    }
    return 0;
}

/*
   the drive holds step, drive select and write data high through the pull ups
   on the ribbon cable. With the cable on upside down those lines read back low
   and every disk operation would silently do nothing. Only meaningful once
   setupDrive() has configured the pins.
*/
bool XCopyFloppy::detectCableOrientation()
{
    bool stepLine = digitalRead(_step);
    bool selectLine = digitalRead(_drivesel);
    bool writeLine = digitalRead(_writedata);

    return (stepLine & selectLine & writeLine);
}

void bitCounter()
{
    bitCount++;
}

int XCopyFloppy::findMinima(int start)
{
    int first = 0;
    int last = 0;
    int tMin = 100;
    for (int i = -30; i < 30; i++)
    {
        if (hist[i + start] < tMin)
        {
            tMin = hist[i + start];
            first = i + start;
        }
        if (hist[i + start] == tMin)
        {
            tMin = hist[i + start];
            last = i + start;
        }
    }
    return (first + last) / 2;
}

void XCopyFloppy::adjustTimings()
{
    high2 = findMinima(high2);
    high3 = findMinima(high3);
}

/*
   prints histogram of last read track in ascii
   mainly for debugging purposes
*/
void XCopyFloppy::printHist() {
    float zeit;
    for (int i = 0; i < 256; i++) {
        if (hist[i] > 0) {
            zeit = (float(i) * 0.04166667) + 0.25;
            String line = String(zeit).append(":").append(i).append("-").append(hist[i]);
            for (int j = 0; j < (hist[i] / 128); j++) {
                line.append("+");
            }
            line.append("\r\n");
            Log << line;
        }
    }    
    Log << "1. Minima: " + String(findMinima(high2)) + " high2:" + String(high2) + "\r\n";
    Log << "2. Minima: " + String(findMinima(high3)) + " high3:" + String(high3) + "\r\n";
}

/*
   outputs the histogram of flux transistions in binary form
*/
void XCopyFloppy::printFlux()
{
    byte a, b, c, d;
    for (int i = 0; i < 256; i++)
    {
        a = hist[i];
        b = hist[i] >> 8;
        c = hist[i] >> 16;
        d = hist[i] >> 24;
        Serial.write(a);
        Serial.write(b);
        Serial.write(c);
        Serial.write(d);
    }
}

/*
   counts the transistions and calculates the real read bits of the last read track
   mainly for debugging
*/
void XCopyFloppy::analyseHist(boolean silent)
{
    long trackLen = 0;
    long transitions = 0;
    int samp = 0;
    for (int i = 0; i < 256; i++)
    {
        samp = hist[i];
        if ((i >= low2) && (i <= high2))
        {
            trackLen += 2 * samp;
            transitions += samp;
        }
        if ((i >= high2 + 1) && (i <= high3))
        {
            trackLen += 3 * samp;
            transitions += samp;
        }
        if ((i >= high3 + 1) && (i <= high4))
        {
            trackLen += 4 * samp;
            transitions += samp;
        }
    }
    if (silent == false)
    {
        Serial.print("Transitions: ");
        Serial.print(transitions);
        Serial.print(" Real Bits: ");
        Serial.println(trackLen);
    }
}

/*
   decodes one MFM encoded Sector into Amiga Sector
   partly based on DecodeSectorData and DecodeLongword from AFR.C, written by
   Marco Veneri Copyright (C) 1997 released as public domain
*/
void XCopyFloppy::decodeSector(long secPtr, int index)
{
    secPtr += 8; // skip sync and magic word
    unsigned int tmp[4];
    unsigned long decoded;
    unsigned long chkHeader = 0;
    unsigned long chkData = 0;
    //decode format, track, sector, distance 2 gap
    for (int i = 0; i < 1; i++)
    {
        tmp[0] = ((stream[secPtr + (i * 8) + 0] << 8) + stream[secPtr + (i * 8) + 1]) & 0x5555;
        tmp[1] = ((stream[secPtr + (i * 8) + 2] << 8) + stream[secPtr + (i * 8) + 3]) & 0x5555;
        tmp[2] = ((stream[secPtr + (i * 8) + 4] << 8) + stream[secPtr + (i * 8) + 5]) & 0x5555;
        tmp[3] = ((stream[secPtr + (i * 8) + 6] << 8) + stream[secPtr + (i * 8) + 7]) & 0x5555;

        // even bits
        tmp[0] = (tmp[0] << 1);
        tmp[1] = (tmp[1] << 1);

        // or with odd bits
        tmp[0] |= tmp[2];
        tmp[1] |= tmp[3];

        // final longword
        decoded = ((tmp[0] << 16) | tmp[1]);

        sectorTable[index].sector = (decoded >> 8) & 0xff;
        index = (decoded >> 8) & 0xff;
        // if sector out of bounds, return with error
        // >=, not >: sectors are numbered 0..sectors-1, and letting index == sectors
        // through wrote 540 bytes past the end of _track[] on an HD disk.
        if ((index >= sectors) || (index < 0))
        {
            _errors = _errors | (1 << 31);
            _extError = "Sector out of bounds\n";
            return;
        }
        _track[index].sector[(i * 4) + 0] = decoded >> 24; // format type 0xff = amiga
        _track[index].sector[(i * 4) + 1] = decoded >> 16; // track
        _track[index].sector[(i * 4) + 2] = decoded >> 8;  // sector
        _track[index].sector[(i * 4) + 3] = decoded;       // distance to gap
    }
    //decode checksums
    for (int i = 5; i < 7; i++)
    {
        tmp[0] = ((stream[secPtr + (i * 8) + 0] << 8) + stream[secPtr + (i * 8) + 1]) & 0x5555;
        tmp[1] = ((stream[secPtr + (i * 8) + 2] << 8) + stream[secPtr + (i * 8) + 3]) & 0x5555;
        tmp[2] = ((stream[secPtr + (i * 8) + 4] << 8) + stream[secPtr + (i * 8) + 5]) & 0x5555;
        tmp[3] = ((stream[secPtr + (i * 8) + 6] << 8) + stream[secPtr + (i * 8) + 7]) & 0x5555;
        // even bits
        tmp[0] = (tmp[0] << 1);
        tmp[1] = (tmp[1] << 1);
        // or with odd bits
        tmp[0] |= tmp[2];
        tmp[1] |= tmp[3];
        // final longword
        decoded = ((tmp[0] << 16) | tmp[1]);
        _track[index].sector[(i * 4) + 0] = decoded >> 24;
        _track[index].sector[(i * 4) + 1] = decoded >> 16;
        _track[index].sector[(i * 4) + 2] = decoded >> 8;
        _track[index].sector[(i * 4) + 3] = decoded;
        // store checksums for later use
        if (i == 5)
        {
            chkHeader = decoded;
        }
        else
        {
            chkData = decoded;
        }
    }
    // decode all the even data bits
    unsigned int data;
    for (int i = 0; i < 256; i++)
    {
        data = ((stream[secPtr + (i * 2) + 56] << 8) + stream[secPtr + (i * 2) + 57]) & 0x5555;
        _track[index].sector[(i * 2) + 28] = (unsigned char)(data >> 7);
        _track[index].sector[(i * 2) + 29] = (unsigned char)(data << 1);
    }

    // or with odd data bits
    for (int i = 0; i < 256; i++)
    {
        data = ((stream[secPtr + (i * 2) + 56 + 512] << 8) + stream[secPtr + (i * 2) + 57 + 512]) & 0x5555;
        _track[index].sector[(i * 2) + 28] |= (unsigned char)(data >> 8);
        _track[index].sector[(i * 2) + 29] |= (unsigned char)(data);
    }
    // check für checksum _errors and generate error flags
    if (calcChkSum(secPtr, 0, 40) != chkHeader)
    {
        _errors = _errors | (1 << index);
        _extError = "Header/Data bad checksum\n";
    }
    if (calcChkSum(secPtr, 56, 1024) != chkData)
    {
        _errors = _errors | (1 << (index + 32));
        _extError = "Header/Data bad checksum\n";
    }
}

/*
   calculates a checksum of <secPtr> at <pos> for <b> bytes length
   returns checksum
*/
unsigned long XCopyFloppy::calcChkSum(long secPtr, int pos, int b)
{
    unsigned long chkSum = 0;
    unsigned long tSum = 0;
    for (int i = 0; i < b / 4; i++)
    {
        tSum = stream[secPtr + (i * 4) + pos + 0];
        tSum = tSum << 8;
        tSum += stream[secPtr + (i * 4) + pos + 1];
        tSum = tSum << 8;
        tSum += stream[secPtr + (i * 4) + pos + 2];
        tSum = tSum << 8;
        tSum += stream[secPtr + (i * 4) + pos + 3];
        chkSum = chkSum ^ tSum;
    }
    chkSum = chkSum & 0x55555555;
    return chkSum;
}

/*
   decodes a whole track
   optional parameter silent to suppress all serial debug info
*/
void XCopyFloppy::decodeTrack(boolean silent)
{
    if (!silent) { Log << "Sectors start at: "; }
    for (int i = 0; i < sectorCnt; i++) {
        if (!silent) { 
            Log << sectorTable[i].bytePos; 
            if (i != sectorCnt - 1) { Log << ", "; }
        }
        decodeSector(sectorTable[i].bytePos, i);
    }
    if (!silent) { Log << "\r\n"; }
}

/*
   returns current track number from decoded sectors in buffer
*/
int XCopyFloppy::getTrackInfo()
{
    int tTrack = 0;
    for (int i = 0; i < sectorCnt; i++)
    {
        tTrack = tTrack + _track[i].sector[1];
    }
    return tTrack / sectors;
}

/*
   dumps the sector <index> from the buffer in human readable acsii to the serial port
   mainly for debugging
*/
void XCopyFloppy::printAmigaSector(int index) {
    struct Sector *aSec = (Sector *)&_track[index].sector;
    String line = "Format Type: " + String(aSec->format_type) + " Logical Track: " + String(aSec->track) + " Sector: " + String(aSec->sector) + " NumSec2Gap: " + String(aSec->toGap) + " Data Chk: ";
    line.append(String(aSec->data_chksum, HEX));
    line.append(" Header Chk: ");
    line.append(String(aSec->header_chksum, HEX));
    Log << line + "\r\n";

    for (int i = 0; i < 16; i++) {
        line = "";
        for (int j = 0; j < 32; j++) {
            if (aSec->data[(i * 32) + j] < 16) {
                line.append("0");
            }
            line.append(String(aSec->data[(i * 32) + j], HEX) + " ");
        }
        for (int j = 0; j < 32; j++) {
            line.append(byte2char(aSec->data[(i * 32) + j]));
        }
        Log << line + "\r\n";
    }
}

/*
   dumps the whole track in ascii
   mainly for debugging
*/
void XCopyFloppy::printTrack() {
    for (int i = 0; i < sectorCnt; i++) {
        printAmigaSector(i);
    }
}

/*
   dumps the data section of a sector in binary format
*/
void XCopyFloppy::dumpSector(int index)
{
    struct Sector *aSec = (Sector *)&_track[index].sector;
    for (int i = 0; i < 512; i++)
    {
        Serial.print((char)aSec->data[i]);
    }
}

/*
   reads a sector from the serial in binary format
*/
int XCopyFloppy::loadSector(int index)
{
    char tBuffer[512];
    struct Sector *aSec = (Sector *)&_track[index].sector;
    int rByte = Serial.readBytes(tBuffer, 512);
    if (rByte != 512)
        return -1;
    for (int i = 0; i < 512; i++)
    {
        aSec->data[i] = tBuffer[i];
    }
    return 0;
}

/*
   dumps the whole track in binary form to the serial port
*/
void XCopyFloppy::downloadTrack()
{
    for (int i = 0; i < sectors; i++)
    {
        dumpSector(i);
    }
}

/*
   reads a whole track from the serial port in binary form
*/
void XCopyFloppy::uploadTrack()
{
    _errors = 0;
    _extError = "OK\n";
    for (int i = 0; i < sectors; i++)
    {
        if (loadSector(i) != 0)
        {
            _extError = "Track upload error\n";
            _errors = -1;
            return;
        }
    }
}

/*
   returns c if printable, else returns a whitespace
*/
char byte2char(byte c, char delim)
{
    if ((c < 32) | (c > 126))
    {
        return delim;
    }
    else
    {
        return (char)c;
    }
}


/*
   fills a sector for debugging
*/
void XCopyFloppy::fillSector(int sect)
{
    for (int i = 0; i < 256; i++)
    {
        _track[sect].sector[i + 28] = i;
        _track[sect].sector[i + 28 + 256] = i;
    }
}

/*
   encodes odd bits of a longword to mfm
*/
unsigned long oddLong(unsigned long odd)
{
    odd = ((odd >> 1) & MFM_MASK);
    odd = odd | ((((odd ^ MFM_MASK) >> 1) | 0x80000000) & ((odd ^ MFM_MASK) << 1));
    return odd;
}

/*
   encodes even bits of a longword to mfm
*/
unsigned long evenLonger(unsigned long even)
{
    even = (even & MFM_MASK);
    even = even | ((((even ^ MFM_MASK) >> 1) | 0x80000000) & ((even ^ MFM_MASK) << 1));
    return even;
}

/*
   encodes one byte <curr> into mfm, takes into account if previous byte ended with a set bit
   returns the two mfm bytes as a word
*/
word mfmByte(byte curr, word prev)
{
    byte even = ((curr >> 1) & 0x55);
    even = even | ((((even ^ MFM_MASK) >> 1) | 0x8000000) & (((even ^ MFM_MASK) << 1)));
    if ((prev & 0x0001) == 1)
    {
        even = even & 0x7f;
    }
    byte odd = (curr & 0x55);
    odd = odd | ((((odd ^ MFM_MASK) >> 1) | 0x8000000) & (((odd ^ MFM_MASK) << 1)));
    if ((prev & 0x0100) == 0x0100)
    {
        odd = odd & 0x7f;
    }
    return (odd << 8) | even;
}

/*
   stores a longword in the byte array
*/
void putLong(unsigned long tLong, byte *dest)
{
    *(dest + 0) = (unsigned byte)((tLong & 0xff000000) >> 24);
    *(dest + 1) = (unsigned byte)((tLong & 0xff0000) >> 16);
    *(dest + 2) = (unsigned byte)((tLong & 0xff00) >> 8);
    *(dest + 3) = (unsigned byte)((tLong & 0xff));
}

/*
   encodes a sector into a mfm bitstream
*/
void XCopyFloppy::encodeSector(unsigned long tra, unsigned long sec, byte *src, byte *dest)
{
    unsigned long tmp, headerChkSum, dataChkSum;
    word curr;
    word prev;
    byte prevByte;

    // write sync and magic word
    putLong(0xaaaaaaaa, dest + 0);
    putLong(0x44894489, dest + 4);

    // format, track, sector, distance to gap
    tmp = 0xff000000 | (tra << 16) | (sec << 8) | (sectors - sec);
    putLong(oddLong(tmp), dest + 8);
    putLong(evenLonger(tmp), dest + 12);

    // fill unused space in sector header
    prevByte = *(dest + 15);
    for (int i = 16; i < 48; i++)
    {
        if ((prevByte & 0x01) == 1)
        {
            *(dest + i) = 0xaa & 0x7f;
        }
        else
        {
            *(dest + i) = 0xaa;
        }
        prevByte = *(dest + i);
    }

    // data block encode
    prev = (word)prevByte;
    for (int i = 64; i < 576; i++)
    {
        curr = mfmByte(*(src + i - 64), prev);
        prev = curr;
        *(dest + i) = (byte)(curr & 0xff);
        *(dest + i + 512) = (byte)(curr >> 8);
    }

    // calc headerchecksum
    headerChkSum = calcChkSum(sectorTable[sec].bytePos, 8, 40);
    putLong(oddLong(headerChkSum), dest + 48);
    putLong(evenLonger(headerChkSum), dest + 52);

    // calc datachecksum
    dataChkSum = calcChkSum(sectorTable[sec].bytePos, 64, 1024);
    putLong(oddLong(dataChkSum), dest + 56);
    putLong(evenLonger(dataChkSum), dest + 60);
}

/*
   fills bitstream with 0xAA
*/
void XCopyFloppy::fillTrackGap(byte *dst, int len)
{
    for (int i = 0; i < len; i++)
    {
        *dst++ = 0xaa;
    }
}

/*
   encodes a complete track + gap into mfm bitstream
   the gap gets encoded before the sectors to make sure when the track gap is too long for
   the track the first sector doesnt gets overwritten, this way only the track gap gets overwritten
   and the track contains no old sector headers
*/
void XCopyFloppy::floppyTrackMfmEncode(unsigned long track, byte *src, byte *dst)
{
    fillTrackGap(dst, FLOPPY_GAP_BYTES);
    for (int i = 0; i < sectors; i++)
    {
        sectorTable[i].bytePos = (i * 1088) + FLOPPY_GAP_BYTES;
        encodeSector(track, i, src + 28 + (i * 540), dst + (i * 1088) + FLOPPY_GAP_BYTES);
    }
}

int XCopyFloppy::hardwareVersion()
{
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    delay(505);
    pinMode(6, OUTPUT);
    digitalWriteFast(6, HIGH);
    pinMode(7, OUTPUT);
    pinMode(10, INPUT_PULLUP);
    for (int i = 0; i < 85; i++)
    {
        digitalWriteFast(7, LOW);
        delayMicroseconds(2);
        digitalWriteFast(7, HIGH);
        delay(3);
        if (digitalRead(10) == 0)
            return 0;
    }
    pinMode(4, INPUT);
    pinMode(6, INPUT);
    pinMode(7, INPUT);
    pinMode(10, INPUT);

    pinMode(16, OUTPUT);
    digitalWrite(16, LOW);
    delay(505);
    pinMode(18, OUTPUT);
    digitalWriteFast(18, HIGH);
    pinMode(19, OUTPUT);
    pinMode(7, INPUT_PULLUP);
    for (int i = 0; i < 85; i++)
    {
        digitalWriteFast(19, LOW);
        delayMicroseconds(2);
        digitalWriteFast(19, HIGH);
        delay(3);
        if (digitalRead(7) == 0)
            return 1;
    }

    pinMode(16, INPUT);
    pinMode(18, INPUT);
    pinMode(19, INPUT);
    pinMode(7, INPUT);

    return -1;
}

void XCopyFloppy::registerSetup(int version)
{
    version = 1;

    if (version == 1)
    {
        _dens = 14;      //2 density select IN
        _index = 15;     //8 index OUT
        _drivesel = 16;  //12 drive select 1 IN
        _motor = 17;     //16 motor1 on IN
        _dir = 18;       //18 direction IN
        _step = 19;      //20 step IN
        _writedata = 9;  //22 write data IN
        _writeen = 8;    //24 write enable IN
        _track0 = 7;     //26 track 0 OUT
        _wprot = 6;      //28 write protect OUT
        _readdata = 5;   //30 read data OUT (FTM0_CH1) *** do not change this pin ***
        _side = 4;       //32 head select IN
        _diskChange = 3; //34 disk change OUT

        // FlexTimerModule defines for Pin 5
        FTChannelValue = 0x40038048;
        FTStatusControlRegister = 0x40038044;
        FTPinMuxPort = 0x4004C01C;
    }
    else
    {
        _dens = 2;        //2 density select IN
        _index = 3;       //8 index OUT
        _drivesel = 4;    //12 drive select 1 IN
        _motor = 5;       //16 motor1 on IN
        _dir = 6;         //18 direction IN
        _step = 7;        //20 step IN
        _writedata = 8;   //22 write data IN
        _writeen = 9;     //24 write enable IN
        _track0 = 10;     //26 track 0 OUT
        _wprot = 11;      //28 write protect OUT
        _readdata = 22;   //30 read data OUT (FTM0_CH1) *** do not change this pin ***
        _side = 14;       //32 head select IN
        _diskChange = 15; //34 disk change OUT

        // FlexTimerModule defines for Pin 22
        FTChannelValue = 0x40038010;
        FTStatusControlRegister = 0x4003800C;
        FTPinMuxPort = 0x4004B004;
    }
}

void XCopyFloppy::setMode(int density)
{
    if (density == 1)
    {
        low2 = 30;
        high2 = 85;
        high3 = 125;
        high4 = 200;
        sectors = 22;
        timerMode = timerModeHD;
        streamLen = streamSizeHD;
        filterSetting = filterSettingHD;
        writeSize = writeSizeHD;
        transitionTime = transTimeHD;
        // _retries = 15;
        _retries = maxRetries;
        _mode = HD;
    }
    else
    {
        low2 = 30;
        high2 = 99;
        high3 = 155;
        high4 = 255;
        sectors = 11;
        timerMode = timerModeDD;
        streamLen = streamSizeDD;
        filterSetting = filterSettingDD;
        writeSize = writeSizeDD;
        transitionTime = transTimeDD;
        // _retries = 6;
        _retries = maxRetries;
        _mode = DD;
    }
}

void diskChangeIRQ()
{
    digitalWrite(13, HIGH);
    if (motor == true)
        motorOffRaw();
}

byte XCopyFloppy::getByte(int ptr)
{
    byte tByte = 0;
    for (int i = 0; i < 8; i++)
    {
        tByte = tByte + streamBitband[ptr + i];
        tByte = tByte << 1;
    }
    return tByte;
}
