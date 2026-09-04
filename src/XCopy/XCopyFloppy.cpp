#include "XCopyFloppy.h"
#include "XCopyFixed.h"

// --- state the interrupt handlers touch --------------------------------------
// ftm0_isr fires on every flux transition (roughly every 4us on a DD disk) and
// diskWrite() on every write transition, so the state they reach stays at file
// scope. Routing it through an instance pointer would add a load per access
// inside those handlers. Everything the handlers do not touch is member state
// on XCopyFloppy.
//
// These used to be non-static on the theory that external linkage stopped GCC
// reasoning about them across the whole file as though the interrupts never
// fired. That was always a weak guarantee and under -flto it is no guarantee at
// all: the whole program is one unit at link time, so extern hides a symbol from
// the optimiser exactly as well as static does, which is to say not at all.
//
// What actually holds is below. Everything the handlers and the main loop share
// is volatile, which no amount of whole program analysis may reorder or cache -
// except stream[], sectorTable[] and hist[], which are written a few million
// times per track and cannot afford it. Those three are handed over explicitly
// instead, with a compiler barrier at each end of the window the handler owns:
// one in startFTM0() before the timer is armed, and one after each wait on
// recordOn. See those three places for what each barrier is holding down.
//
// The linkage is left alone regardless, because changing it now would be a large
// diff asserting something that was never true.

// pins, assigned by registerSetup()
int _dens, _index, _drivesel, _motor, _dir, _step, _writedata, _writeen, _track0, _wprot, _readdata, _side, _diskChange;

/*
   Whether a disk change is allowed to stop the motor.

   True for every UI and console path, where an eject means the operation is over
   and the drive should stop. False for a live session, where the HOST owns the
   motor - the same reason the session turns off the idle timeout. Letting the
   interrupt stop it there means an eject kills the drive, and the motor is then
   still off when a disk is put back, so no index pulses are produced, the host
   cannot see the new media, and the drive never recovers.

   Defined here rather than beside diskChangeIRQ() because setDiskChangeStopsMotor()
   is above that point in the file and would not see it.
*/
volatile bool diskChangeStopsMotor = true;

/*
   Whether the drive may be deselected.

   A deselected drive stops driving its status lines and the ribbon pull-ups take
   over, so /DSKCHG, /TRK0 and /WPT all read HIGH - which for /DSKCHG means "disk
   present". motorOff() deselects, and a guest parks the motor whenever it is not
   reading, so an eject at the Kickstart insert-disk screen was invisible: the
   drive had been deselected, the line floated high, and the answer was "disk
   inserted" no matter what was actually in there.

   For a live session the host owns the drive for the whole session, so it stays
   selected and the lines mean something whenever they are read. False everywhere
   else, where select is asserted per operation as before.
*/
volatile bool keepDriveSelected = false;
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

// --- raw flux capture, for SCP imaging ---------------------------------------
// Written by ftm0_flux_isr and read by the main loop. Same file-scope, external
// linkage reasoning as the MFM state above.
//
// The counter is NOT reset per capture here, unlike ftm0_isr: it free-runs at
// MOD 0xffff and the interval is a subtraction. Resetting the counter silently
// drops however long the CPU took to enter the ISR from every interval, which is
// harmless when the value only picks a threshold bucket and a systematic bias
// when the value IS the artefact being preserved.
uint16_t *fluxRing;                       // caller supplied, see beginFluxCapture
volatile uint32_t fluxRingSize;           // in samples
volatile uint32_t fluxWriteIdx;           // producer, ISR only
volatile uint32_t fluxReadIdx;            // consumer, main loop only
volatile boolean fluxOverrun;
volatile boolean fluxCapturing;           // past the first index, storing samples
volatile boolean fluxArmed;               // spinning up, waiting for the first index
volatile uint8_t fluxRevsWanted;
volatile uint8_t fluxRevsSeen;
volatile uint32_t fluxTofTotal;           // FTM0 overflows since capture started
volatile uint32_t fluxLast32;             // absolute tick of the previous transition
volatile uint32_t fluxSamples;            // samples stored this capture
volatile uint32_t fluxRevTick[SCP_MAX_REVS + 1];   // absolute tick at each index edge
volatile uint32_t fluxRevSample[SCP_MAX_REVS + 1]; // sample count at each index edge
volatile uint8_t fluxIndexLevel;          // previous index pin level, for edge detect
// On KINETISK portInputRegister() is a bit-band alias, one byte per pin reading 0 or
// 1, and digitalPinToBitMask() is 1. Caching it turns the ISR's index sample into a
// single byte load; digitalRead() on a variable pin would cost an order of magnitude
// more, every 5us.
volatile uint8_t *fluxIndexReg;           // index pin input register, cached
uint8_t fluxIndexMask;                    // index pin bit, cached

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

/*
   Head and motor timings.

   Every one of these was a literal in the function that used it. They are variables
   now so a live streaming host can tune them through the protocol's CONFIG command,
   and every default is exactly the number that was there before: setDir()'s delay(20),
   step1()'s delayMicroseconds(2) and delay(3), setSide()'s delay(2),
   gotoLogicTrack()'s delay(18) and motorOn()'s delay(600). Nothing that does not ask
   sees any change at all.

   The step pulse is deliberately still 2us and not exposed as anything a caller is
   likely to lower: it is a pulse width the drive has to see, not a delay to be
   optimised away.
*/
uint32_t stepPulseUs = 2;
uint32_t stepIntervalUs = 3000;
uint32_t dirSettleUs = 20000;
uint32_t sideSettleUs = 2000;
uint32_t seekSettleUs = 18000;
uint32_t motorSpinupMs = 600;
//! False while a live session owns the drive, so the idle timeout cannot stop it.
volatile boolean motorIdleOff = true;

/*
   delay() takes milliseconds and delayMicroseconds() is a busy loop meant for short
   waits, so a settle that is configurable in microseconds and may be tens of
   milliseconds needs both.
*/
static void delayUs(uint32_t us)
{
    if (us >= 1000)
    {
        delay(us / 1000);
        us %= 1000;
    }
    if (us)
        delayMicroseconds(us);
}

// --- interrupt handlers and pure helpers ------------------------------------
void pinModeFast(uint8_t pin, uint8_t mode);
void driveSelect();
void driveDeselect();
void motorOffRaw();
void motorTimeout();
void diskWrite();
void diskChangeIRQ();
void bitCounter();
extern "C" void ftm0_isr(void);
extern "C" void ftm0_flux_isr(void);
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
    // The caller owns the drive for the duration - see keepDriveSelected.
    if (keepDriveSelected)
        return;
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
        delayUs(motorSpinupMs * 1000); // more than plenty of time to spinup motor
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
/*
   Waits for the index line to leave a level, but not forever.

   The unbounded spins this replaces are why the board hung when a live session
   was started with no disk in the drive. XCopyLive::run() calls motorOn(), which
   on an auto-density drive calls initDrive() -> densityDetect() -> indexTimer(),
   and an empty drive never produces an index pulse. The banner had already gone
   out, so the host saw a device that greeted it and then answered nothing, and
   the session's own watchdog could not help: it lives in the command loop that
   was never reached. Only a reset recovered it.

   A revolution is ~200 ms, so half a second is comfortably longer than any real
   wait and short enough that an empty drive is noticed promptly.
*/
static const uint32_t indexWaitTimeoutMs = 500;

static bool waitIndexLevel(int pin, int level)
{
    const uint32_t start = millis();
    while (digitalRead(pin) == level)
    {
        // Unsigned subtraction, so this stays correct across the millis() wrap.
        if (millis() - start > indexWaitTimeoutMs)
            return false;
    }
    return true;
}

/*
   Returned when no index pulse arrived. Deliberately LARGE rather than 0 or -1:
   hdDisk() reads this as "rotTimer < 180 means HD", so a small value would have
   an empty drive report itself as high density. Large means DD, which is both
   the safe default and what densityDetect() concludes from a bitCount of 0.
*/
const int indexTimerNoIndex = 9999;

int XCopyFloppy::indexTimer()
{
    motorOn();
    attachInterrupt(_readdata, bitCounter, FALLING);
    if (!waitIndexLevel(_index, 1) || !waitIndexLevel(_index, 0))
    {
        detachInterrupt(_readdata);
        bitCount = 0;
        return indexTimerNoIndex;
    }
    long tRead = micros();
    bitCount = 0;
    delay(5);
    if (!waitIndexLevel(_index, 1) || !waitIndexLevel(_index, 0))
    {
        detachInterrupt(_readdata);
        bitCount = 0;
        return indexTimerNoIndex;
    }
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

/*
   Free running index edge count, for the drive toolkit.

   readRPM() reports a rate and answers "how fast", which is not the same question
   as "did anything arrive at all". A rate needs two edges inside the stall window
   to say anything, so a drive producing one pulse every few seconds reads as
   stopped; a count says exactly what turned up. Kept here rather than behind an
   interrupt of its own because this ISR is already attached whenever anyone cares.
*/
static volatile uint32_t indexEdgeCount = 0;

void readIndexISR()
{
    indexTimes[indexHead] = micros();
    indexHead = (indexHead + 1) % RPM_WINDOW;
    if (indexSamples < RPM_WINDOW)
        indexSamples++;
    indexEdgeCount++;
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
   One whole index pulse, leading edge to trailing, with the verdict kept.

   waitForIndex() below throws it away, which is right for a read that can cope with
   finding nothing and wrong for a write: a caller that opened the write gate anyway
   would scribble a track it was told not to touch.
*/
static bool waitIndexPulse(int pin)
{
    return waitIndexLevel(pin, 1) && waitIndexLevel(pin, 0);
}

/*
   wait for index hole
*/
void XCopyFloppy::waitForIndex()
{
    motorOn();
    // Bounded for the same reason as indexTimer(): an empty drive must not wedge
    // the board. Callers already cope with a read that finds nothing.
    if (!waitIndexLevel(_index, 1))
        return;
    waitIndexLevel(_index, 0);
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
    return writeTrack(writeSize, true);
}

int XCopyFloppy::writeTrack(int bytes, bool fromIndex)
{
    /*
       diskWrite() ends the write at (writeSize * 8) + 8 cells, so it reads the byte
       one past the last one the caller filled. That byte has to exist and has to be
       quiet, which is what the +1 and the zero below are for.
    */
    if (bytes <= 0 || bytes + 1 > streamSizeHD)
        return -3;

    motorOn();
    _motorTimer.end();
    if (digitalRead(_wprot) == 0)
    {
        _motorTimer.begin(motorTimeout, 1000000);
        _extError = "Disk is write-protected\n";
        return -1;
    }
    for (int i = 0; i < bytes; i++)
    {
        stream[i] = reverse(stream[i]);
    }
    stream[bytes] = 0;

    /*
       writeSize is the ISR's bound as well as the mode's track size, and setMode() is
       the only other thing that writes it. Borrowed for this write and put back on
       every way out, so a live write cannot change what the ADF path does next.
    */
    const int savedWriteSize = writeSize;
    writeSize = bytes;

    int result = 0;
    writePtr = 0;
    writeBitCnt = 0;
    dataByte = stream[writePtr];
    writeActive = true;
    _writeTimer.priority(0);
    delayMicroseconds(100);

    /*
       An unbounded index wait would wedge the board on a drive whose platter has
       stopped - a host that parked the motor, or media pulled between the check and
       here - so the failure is reported instead of hung. waitForIndex() gives up
       silently after 500 ms, which is fine for a read that finds nothing and not fine
       for a write, because the caller has to know the track was left alone.
    */
    if (fromIndex && !waitIndexPulse(_index))
    {
        writeActive = false;
        _extError = "No index pulse\n";
        result = -2;
    }
    else
    {
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
    }

    writeSize = savedWriteSize;
    _motorTimer.begin(motorTimeout, 1000000);
    return result;
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

        /*
           Acquire. recordOn is volatile so the wait above is honest, but what the
           handler actually produced - stream[], sectorTable[] and hist[] - is not,
           and at link time the compiler can see every writer in the program and
           satisfy itself that nothing between here and decodeTrack() touches them.
           That is true and beside the point: ftm0_isr is not called from anywhere,
           it is reached through the vector table. Without this, a load of any of
           the three is free to be hoisted above the wait and decode the previous
           track's bytes.
        */
        __asm__ __volatile__("" ::: "memory");

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
   Odd/even MFM unpack of one longword.

   Amiga MFM stores a longword as two halves: the even data bits in the first four
   raw bytes and the odd bits in the next four, each interleaved with clock bits
   that the 0x5555 mask strips. decodeSector() spells this out three separate
   times; naming it once means calibrationRead() cannot get the arithmetic subtly
   different from the decoder it has to agree with.
*/
unsigned long XCopyFloppy::decodeLongword(long p)
{
    unsigned int even0 = ((stream[p + 0] << 8) + stream[p + 1]) & 0x5555;
    unsigned int even1 = ((stream[p + 2] << 8) + stream[p + 3]) & 0x5555;
    unsigned int odd0 = ((stream[p + 4] << 8) + stream[p + 5]) & 0x5555;
    unsigned int odd1 = ((stream[p + 6] << 8) + stream[p + 7]) & 0x5555;

    return (((unsigned long)((even0 << 1) | odd0) << 16) | ((even1 << 1) | odd1));
}

/*
   One pass of the head calibration test.

   Why this is not readTrack(): that retries up to six times, calls adjustTimings()
   between attempts - which moves the density thresholds and so makes consecutive
   passes incomparable - and at :788 turns "the header does not name the track I
   asked for" into a hard error. That last one is the whole signal here. An
   operator adjusting a head needs to see the drive reading its neighbour, not be
   told the read failed. Lowering _retries would not help either: readTrack()
   opens with setMode(), which puts _retries back to maxRetries.

   Everything is judged from sectorTable[] and the raw stream rather than from
   _track[], for two reasons. initRead() does not clear _track[], so a sector left
   over from the previous pass would read as present this pass. And decodeSector()
   records data checksum failures into a bit that a 32 bit shift throws away, so
   its verdict on a sector's data cannot be trusted. Both checksums are recomputed
   here from the bytes actually captured.
*/
bool XCopyFloppy::calibrationRead(uint8_t cylinder, uint8_t head, bool recal, CalibrationResult &out)
{
    // Forgetting the position forces gotoTrack() through seek0(), so a drive that
    // cannot find a cylinder reliably fails again rather than being let off by the
    // head already happening to sit in the right place.
    if (recal)
        _currentTrack = -1;

    motorOn();
    gotoLogicTrack((cylinder * 2) + head);

    for (int i = 0; i < 256; i++)
        hist[i] = 0;

    initRead();
    startFTM0();
    unsigned long started = millis();
    while (recordOn)
    {
        // Same guard readTrack() uses. Unlike readTrack() this does not record an
        // error: a timeout means there was no pass, not that the pass was bad.
        if ((millis() - started) > 300)
        {
            stopFTM0();
            return false;
        }
    }

    return censusTrack(cylinder, head, out);
}

/*
   The census half of calibrationRead(): every verdict is reached from the bytes
   the capture handler left in stream[] and sectorTable[], and nothing here goes
   near the drive.

   Split out so a second source can reach it. XCopyDiskInfo replays the flux in an
   SCP file through the same thresholding ftm0_isr uses, fills the same two
   buffers, and then asks this the same question - so a file and the disk it was
   imaged from cannot come back analysed differently. The barrier lives here
   rather than in the caller for the same reason: it is this function's reads that
   need ordering, whichever of the two filled the buffers.
*/
bool XCopyFloppy::censusTrack(uint8_t cylinder, uint8_t head, CalibrationResult &out)
{
    // Acquire, for the same reason readTrack() has one: everything read out below
    // comes from sectorTable[] and stream[], which the handler wrote and which
    // nothing marks as having changed.
    __asm__ __volatile__("" ::: "memory");

    memset(&out, 0, sizeof(out));
    out.cylinder = cylinder;
    out.head = head;
    out.sectorCount = sectors;
    out.syncs = sectorCnt;
    out.cylinderSeen = -1;
    // sectorMissing is zero, so the memset above has already set every slot.

    for (int i = 0; i < sectorCnt; i++)
    {
        unsigned long bytePos = sectorTable[i].bytePos;

        // Nothing readable here unless the header says otherwise below.
        if (i < (int)(sizeof(out.syncSector) / sizeof(out.syncSector[0])))
            out.syncSector[i] = 0xff;

        // decodeSector() has no such check and will read past the buffer on a sync
        // mark found near the end of the capture.
        if (bytePos + 1088 > (unsigned long)streamLen)
        {
            out.truncated++;
            continue;
        }

        long base = (long)bytePos + 8; // skip the sync and magic words
        unsigned long info = decodeLongword(base);

        /*
           The header checksum is checked before anything in the header is
           believed, and not as one more way of failing at the end.

           Which cylinder a sector claims to come from is the entire output of
           this test, and that claim lives in the header. Reading the track byte
           out of a header that did not checksum and then reporting the drive as
           one cylinder low would be inventing the very diagnostic the operator
           is about to act on.
        */
        if (calcChkSum(base, 0, 40) != decodeLongword(base + 40) || (info >> 24) != 0xff)
        {
            out.strays++;
            continue;
        }

        uint8_t headerTrack = (info >> 16) & 0xff;
        uint8_t headerSector = (info >> 8) & 0xff;

        if (headerSector >= out.sectorCount)
        {
            out.strays++;
            continue;
        }

        if (i < (int)(sizeof(out.syncSector) / sizeof(out.syncSector[0])))
            out.syncSector[i] = headerSector;

        if (out.status[headerSector] != sectorMissing)
            out.duplicates++;

        /*
           The header carries the LOGICAL track, cylinder * 2 + side, so one
           cylinder of head misplacement is a difference of two here. Getting this
           wrong reads every side 1 sector as a cylinder high.
        */
        uint8_t headerCylinder = headerTrack / 2;
        uint8_t headerHead = headerTrack & 1;

        if (out.cylinderSeen < 0)
            out.cylinderSeen = (int8_t)headerCylinder;

        uint8_t verdict;
        if (headerCylinder < cylinder)
            verdict = sectorCylLow;
        else if (headerCylinder > cylinder)
            verdict = sectorCylHigh;
        else if (headerHead != head)
            verdict = sectorHeadWrong;
        else if (calcChkSum(base, 56, 1024) != decodeLongword(base + 48))
            verdict = sectorBadCheck;
        else
            verdict = sectorOK;

        // A second sync mark for a sector already read cleanly never downgrades it.
        // Both copies are on the disk; one of them being good is what matters.
        if (out.status[headerSector] != sectorOK)
            out.status[headerSector] = verdict;
    }

    for (int i = 0; i < out.sectorCount; i++)
        if (out.status[i] == sectorOK)
            out.valid++;

    return true;
}

/*
   One analysis pass over a track, aligned to the index pulse.

   calibrationRead() arms the timer wherever the head happens to be, which is
   right for a test that only counts sectors and wrong for one that draws them.
   Stream position 0 would land at a different angle on every track, so the sector
   marks would sit at a random rotation per ring and a surface that actually has
   its sectors marching neatly round the disk would be drawn as noise.

   Waiting for the index first means a byte position in stream[] is an angle, and
   the same is true of an SCP revolution, which is index to index by definition.
   That is what lets a file and a disk be drawn the same way.

   A drive that never raises an index still gets its pass. The capture just starts
   unaligned and says so, which is more use than refusing to look at the disk.

   @param cylinder physical cylinder, 0 to MAX_CYLINDERS-1
   @param head 0 (lower) or 1 (upper)
   @param aligned set true when the capture did start on an index pulse
   @result false only when no capture happened at all - no disk, or a timeout
*/
bool XCopyFloppy::surveyCapture(uint8_t cylinder, uint8_t head, bool &aligned)
{
    motorOn();
    gotoLogicTrack((cylinder * 2) + head);

    for (int i = 0; i < 256; i++)
        hist[i] = 0;

    initRead();

    /*
       The wait sits between initRead() and startFTM0() rather than before both.
       initRead() zeroes 14KB of stream[] and calls setupFTM0(); doing that after
       the index pulse would spend most of the gap it just waited for.
    */
    aligned = waitIndexPulse(_index);

    startFTM0();
    unsigned long started = millis();
    while (recordOn)
    {
        // Same guard calibrationRead() uses, and for the same reason: a timeout
        // means there was no pass, not that the pass was bad.
        if ((millis() - started) > 300)
        {
            stopFTM0();
            return false;
        }
    }
    return true;
}

/*
   How dense the captured cells are over one slice of the track, 0 to 15.

   This is the disk surface texture. A slice of stream[] is one angular bucket, and
   the count of set bits in it says what is written there: track gap reads sparse,
   sync marks and sector data read dense, and an unformatted track reads flat.

   Counting cells rather than shipping the flux is not only a transfer saving. A
   cylinder ring is about two pixels wide, so a few hundred buckets is already
   finer than the pixels available to draw them in - the extra resolution in raw
   flux would land on top of itself.

   Scaled against half a full byte rather than eight bits: a DD cell stream runs
   about one transition in three cells, so measuring against 8 would leave the
   whole texture squashed into the bottom of the range.
*/
uint8_t XCopyFloppy::bitDensity(unsigned long fromByte, unsigned long toByte)
{
    if (toByte > (unsigned long)streamLen)
        toByte = streamLen;
    if (fromByte >= toByte)
        return 0;

    unsigned long ones = 0;
    for (unsigned long i = fromByte; i < toByte; i++)
        ones += __builtin_popcount(stream[i]);

    const unsigned long span = (toByte - fromByte) * 4;
    const unsigned long level = ((ones * 15) + (span / 2)) / span;
    return level > 15 ? 15 : (uint8_t)level;
}

/*
   Where in the captured stream the nth sync mark was found, and how much of the
   stream was filled. Together these turn a sync mark into an angle.
*/
unsigned long XCopyFloppy::getSyncBytePos(byte index)
{
    if (index >= sectorCnt)
        return 0;
    return sectorTable[index].bytePos;
}

int XCopyFloppy::getStreamPos()
{
    return readPtr;
}

/*
   How many sync marks the capture handler will record before it stops adding to
   sectorTable[].

   Not the same thing as setSectorCnt(), which sets the count already found. The
   handler bounds the table with `sectorCnt < sectors`, and sectors is otherwise
   only ever set by setMode() - so a DD survey stops looking after eleven, and a
   track carrying more than that is silently cut short. sectorTable[] holds 25.

   Raising it for a survey and putting it back afterwards widens the census for a
   custom or protected track without touching the handler itself.
*/
void XCopyFloppy::setExpectedSectors(byte count)
{
    sectors = count;
}

byte XCopyFloppy::getExpectedSectors()
{
    return sectors;
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
   Turn the interval in `sample` into cells, and file it.

   The body of ftm0_isr, named so a second caller can reach it. XCopyDiskInfo
   replays the flux out of an SCP file through this, so an image decodes into
   stream[] and sectorTable[] by the same rules and against the same thresholds
   the drive is read with - which is what makes an image and the disk it came from
   analyse alike rather than nearly alike.

   It reads the `sample` global rather than taking a parameter, which reads oddly
   until you remember what this is. `sample` is volatile and read five times here;
   a by-value parameter would collapse those to one load and so change the codegen
   of the tightest interrupt in the project - about four microseconds, and the one
   with the least slack - for the convenience of a caller that runs at its leisure.
   The caller stores to `sample` and calls in, and the handler is left exactly as
   it was. always_inline because at -Os GCC is otherwise free to make the ISR pay
   for a call.

   @result false for an interval outside the thresholds - too long, or too short.
           Those are gap and noise: no cells, and no histogram entry either.
*/
static inline __attribute__((always_inline)) bool mfmFeed(void)
{
    // skip too short / long samples, occur usually in the track gap
    bitCount++;
    if (sample > high4)
    {
        return false;
    }
    if (sample < low2)
    {
        return false;
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
    hist[sample]++; // add sample to histogram
    return true;
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
    if (!mfmFeed())
    {
        return;
    }
    if (readPtr > streamLen) // stop when buffer is full
    {
        recordOn = false;
        FTM0_SC = 0x00; // Timer off
    }
}

/*
   Feed one flux interval that did not come from the drive.

   The SCP replay path: same cells, same sync marks, same histogram as a real
   read. The only thing it does not do is stop a timer that is not running, so the
   buffer-full test is here rather than left to the handler.

   @param ticks interval in FTM0 ticks, the units the density thresholds are in.
          Anything longer than the histogram is clamped, exactly as a gap that
          overruns high4 is discarded by the handler.
   @result false once the stream buffer is full and nothing further will be stored
*/
/*
   Reset the decoder for a replay.

   initRead() without setupFTM0(). Everything the capture handler accumulates has
   to start from the same place a real read would, or the first sync mark of the
   track lands wherever the previous one left bCnt - but there is no timer to
   configure and no pin to configure it against.
*/
void XCopyFloppy::beginReplay()
{
    bCnt = 0;
    readPtr = 0;
    bitCount = 0;
    sectorCnt = 0;
    readBuff = 0;
    _errors = 0;
    _extError = "OK\n";

    for (int i = 0; i < streamLen; i++)
        stream[i] = 0x00;

    for (int i = 0; i < 256; i++)
        hist[i] = 0;
}

bool XCopyFloppy::feedFluxSample(uint32_t ticks)
{
    if (readPtr > streamLen)
        return false;

    sample = (ticks > 255) ? 256 : (int)ticks;
    mfmFeed();
    return readPtr <= streamLen;
}

/*
   Configures FTM0 for raw flux capture.

   Same input capture on the same pin as setupFTM0(), with two differences that
   matter: the timer overflow interrupt is enabled so intervals longer than 65535
   ticks can still be measured, and ftm0_flux_isr leaves FTM0_CNT alone so the
   counter free-runs and every interval is a subtraction of two absolute times.
*/
void XCopyFloppy::setupFTM0Flux()
{
    FTM0_FILTER = filterSetting;
    FTM0_MODE = 0x05;

    FTM0_SC = 0x00;
    FTM0_CNT = 0x0000;
    FTM0_MOD = 0xFFFF;
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x48; // CHF=0 CHIE=1, input
                                                            // capture, falling edge
    NVIC_SET_PRIORITY(IRQ_FTM0, 0);
    NVIC_ENABLE_IRQ(IRQ_FTM0);
    (*(volatile uint32_t *)FTPinMuxPort) = 0x403;
}

/*
   Interrupt Service Routine for raw flux capture.

   FTM0 raises the channel event and the timer overflow on the same vector, so this
   handler owns both and neither can preempt the other - which is what makes the
   overflow accounting below sound rather than merely likely.

   The index pin is sampled here rather than from its own interrupt so that a
   revolution boundary and the sample counter can never disagree. Resolution is one
   flux interval, about 5us out of a 200ms revolution.
*/
extern "C" void ftm0_flux_isr(void)
{
    volatile uint32_t *csc = (volatile uint32_t *)FTStatusControlRegister;
    uint32_t status = *csc;

    if (status & 0x80) // CHF: a flux transition was captured
    {
        uint16_t capture = (uint16_t)(*(volatile uint32_t *)FTChannelValue);
        *csc = status & ~0x80;

        // An overflow that has fired but not yet been serviced belongs before this
        // capture if the counter has already wrapped past it. Anything in the low
        // half of the range after a pending overflow is on the far side of the wrap.
        uint32_t tof = fluxTofTotal;
        if ((FTM0_SC & 0x80) && (capture < 0x8000))
            tof++;

        uint32_t now = (tof << 16) | capture;
        uint32_t delta = now - fluxLast32;
        fluxLast32 = now;

        // index edge, sampled in step with the samples it delimits
        uint8_t level = (*fluxIndexReg & fluxIndexMask) ? 1 : 0;
        uint8_t edge = (fluxIndexLevel && !level);
        fluxIndexLevel = level;

        if (edge)
        {
            if (fluxArmed)
            {
                // first index: everything before this was spin-up and a partial
                // revolution, so the capture starts here and is honestly index cued
                fluxArmed = false;
                fluxCapturing = true;
                fluxSamples = 0;
                fluxWriteIdx = 0;
                fluxReadIdx = 0;
                fluxRevsSeen = 0;
                fluxRevTick[0] = now;
                fluxRevSample[0] = 0;
                return; // the interval spanning the index belongs to neither side
            }

            fluxRevsSeen++;
            fluxRevTick[fluxRevsSeen] = now;
            fluxRevSample[fluxRevsSeen] = fluxSamples;

            if (fluxRevsSeen >= fluxRevsWanted)
            {
                fluxCapturing = false;
                FTM0_SC = 0x00; // timer off
                return;
            }
        }

        if (!fluxCapturing)
            return;

        // keep the histogram fed so drawFlux() has something to draw
        if (delta < 256)
            hist[delta]++;

        /*
           Everything is kept, including intervals ftm0_isr would discard as noise or
           as gap. Those out of band intervals are exactly what a protection scheme is
           made of, and dropping them is how a flux imager quietly stops being one.

           A gap too long for one 16 bit sample is split across several rather than
           clamped, so the samples still add up to the revolution that contains them.
           Only an unformatted or erased region gets that far, and by then nothing has
           happened for 2.7ms, so the loop has all the time in the world.
        */
        while (fluxCapturing)
        {
            uint32_t next = fluxWriteIdx + 1;
            if (next >= fluxRingSize)
                next = 0;

            if (next == fluxReadIdx)
            {
                // The SD card could not keep up. The track is now full of holes, so
                // stop and let the caller retry rather than write a plausible lie.
                fluxOverrun = true;
                fluxCapturing = false;
                FTM0_SC = 0x00;
                return;
            }

            fluxRing[fluxWriteIdx] = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;
            fluxWriteIdx = next;
            fluxSamples++;

            if (delta <= 0xFFFF)
                return;

            delta -= 0xFFFF;
        }
    }

    if (FTM0_SC & 0x80) // TOF: clear by reading SC while set, then writing the bit low
    {
        FTM0_SC &= ~0x80;
        fluxTofTotal++;
    }
}

/*
   Starts a raw flux capture of @p revolutions revolutions into @p ring.

   The caller owns the ring. In practice it is always getStream() - the MFM stream
   buffer is the only block of RAM on a 64KB part big enough to absorb an SD write
   stall, and it is completely idle during a capture. That does mean flux capture and
   readTrack() alias the same memory: they can never overlap, and nothing may rely on
   the decoded stream surviving a capture.

   Sizing: a DD revolution is around 37,700 transitions, so a full revolution cannot
   be buffered and the caller must drain to SD as it goes. 26.5KB of ring is 13,253
   samples, roughly 70ms of DD flux, which is the margin available for an SD card to
   stall before fluxOverran() goes true.

   Returns false if the arguments are unusable.
*/
bool XCopyFloppy::beginFluxCapture(uint16_t *ring, size_t ringSamples, uint8_t revolutions)
{
    if (ring == NULL || ringSamples < 2 || revolutions < 1 || revolutions > SCP_MAX_REVS)
        return false;

    motorOn();

    fluxRing = ring;
    fluxRingSize = ringSamples;
    fluxWriteIdx = 0;
    fluxReadIdx = 0;
    fluxSamples = 0;
    fluxOverrun = false;
    fluxRevsWanted = revolutions;
    fluxRevsSeen = 0;
    fluxTofTotal = 0;
    fluxLast32 = 0;

    for (uint8_t i = 0; i <= SCP_MAX_REVS; i++)
    {
        fluxRevTick[i] = 0;
        fluxRevSample[i] = 0;
    }

    fluxIndexReg = portInputRegister(digitalPinToPort(_index));
    fluxIndexMask = digitalPinToBitMask(_index);
    fluxIndexLevel = (*fluxIndexReg & fluxIndexMask) ? 1 : 0;

    // the ISR keeps feeding this so drawFlux() has something to draw during a
    // capture, exactly as initRead() does for an MFM read
    for (int i = 0; i < 256; i++)
        hist[i] = 0;

    fluxArmed = true;
    fluxCapturing = false;

    // Vector first: setupFTM0Flux() is what enables the NVIC line, and until the
    // swap has happened any interrupt it lets through would land in ftm0_isr, which
    // resets FTM0_CNT and would corrupt the first interval.
    attachInterruptVector(IRQ_FTM0, ftm0_flux_isr);
    setupFTM0Flux();

    FTM0_CNT = 0x0000;
    FTM0_SC = timerMode | 0x40; // clock source and prescaler as set by setMode(),
                                // plus TOIE so long intervals can still be measured
    return true;
}

/*
   Stops a capture and gives FTM0 back to the MFM read path.

   Safe to call more than once and on every exit path, including cancellation and
   error - leaving the flux vector installed would break the next ADF read in a way
   that looks like a drive fault.
*/
void XCopyFloppy::endFluxCapture()
{
    FTM0_SC = 0x00;
    fluxCapturing = false;
    fluxArmed = false;
    attachInterruptVector(IRQ_FTM0, ftm0_isr);
    fluxRing = NULL;
}

/*
   Longest run of captured samples available without wrapping the ring.

   Zero copy: the caller writes straight out of the ring and then calls
   fluxConsume(). Single producer in the ISR, single consumer here, both indices
   32-bit aligned and so atomic on this part, which is what lets this work without
   disabling interrupts on a path that runs every few hundred microseconds.
*/
size_t XCopyFloppy::fluxPeek(const uint16_t **samples)
{
    uint32_t write = fluxWriteIdx;
    uint32_t read = fluxReadIdx;

    *samples = &fluxRing[read];

    if (write >= read)
        return write - read;

    return fluxRingSize - read; // wrapped: this pass returns the tail only
}

void XCopyFloppy::fluxConsume(size_t count)
{
    uint32_t read = fluxReadIdx + count;
    if (read >= fluxRingSize)
        read -= fluxRingSize;
    fluxReadIdx = read;
}

/*
   True once every requested revolution has been seen. The ring may still hold
   samples the caller has not drained.
*/
bool XCopyFloppy::fluxCaptureDone()
{
    return !fluxArmed && !fluxCapturing;
}

bool XCopyFloppy::fluxOverran()
{
    return fluxOverrun;
}

uint8_t XCopyFloppy::fluxRevolutionsCaptured()
{
    return fluxRevsSeen;
}

uint32_t XCopyFloppy::fluxRevolutionTicks(uint8_t rev)
{
    if (rev >= SCP_MAX_REVS)
        return 0;
    return fluxRevTick[rev + 1] - fluxRevTick[rev];
}

uint32_t XCopyFloppy::fluxRevolutionSamples(uint8_t rev)
{
    if (rev >= SCP_MAX_REVS)
        return 0;
    return fluxRevSample[rev + 1] - fluxRevSample[rev];
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
    /*
       Publish. Everything the caller wrote before arming the timer has to be in
       memory before the first transition can arrive, and two of those writes are
       not volatile: initRead() zeroing the whole of stream[], and the callers
       zeroing hist[]. Nothing orders a plain store against a volatile one, so
       without this the compiler is free to leave either sitting in registers and
       flush it after the handler has already started filling them in - producing
       a track with holes punched in it, or an empty histogram, on a build where
       nothing else changed.
    */
    __asm__ __volatile__("" ::: "memory");

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
    // A live streaming session spins the motor up once and keeps it up for as long as
    // the host wants it, so it turns this off for the duration and back on afterwards.
    if (!motorIdleOff)
        return;

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
    delayUs(dirSettleUs);
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
    delayUs(sideSettleUs);
}

/*
   steps one track into the direction selected by setDir()
*/
void XCopyFloppy::step1()
{
    motorTick = 0;
    digitalWriteFast(_step, LOW);
    delayMicroseconds(stepPulseUs);
    digitalWriteFast(_step, HIGH);
    delayUs(stepIntervalUs);
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
    delayUs(seekSettleUs);
}

/*
   Head and motor timing setters.

   Clamped where a value could damage something or stall the drive rather than merely
   be slow: the step interval has a floor because stepping a head faster than it can
   move loses position silently, and the step pulse has one because a pulse too short
   to be seen is a seek that never happened.
*/
void XCopyFloppy::setStepPulseUs(uint32_t us) { stepPulseUs = (us < 1) ? 1 : us; }
void XCopyFloppy::setStepIntervalUs(uint32_t us) { stepIntervalUs = (us < 2000) ? 2000 : us; }
void XCopyFloppy::setDirSettleUs(uint32_t us) { dirSettleUs = us; }
void XCopyFloppy::setSideSettleUs(uint32_t us) { sideSettleUs = us; }
void XCopyFloppy::setSeekSettleUs(uint32_t us) { seekSettleUs = us; }
void XCopyFloppy::setMotorSpinupMs(uint32_t ms) { motorSpinupMs = ms; }

uint32_t XCopyFloppy::getStepPulseUs() const { return stepPulseUs; }
uint32_t XCopyFloppy::getStepIntervalUs() const { return stepIntervalUs; }
uint32_t XCopyFloppy::getDirSettleUs() const { return dirSettleUs; }
uint32_t XCopyFloppy::getSideSettleUs() const { return sideSettleUs; }
uint32_t XCopyFloppy::getSeekSettleUs() const { return seekSettleUs; }
uint32_t XCopyFloppy::getMotorSpinupMs() const { return motorSpinupMs; }

void XCopyFloppy::setMotorIdleOff(bool enabled)
{
    motorIdleOff = enabled;
    motorTick = 0;
}

/*
   The pieces of a seek, with the waiting left to the caller.

   Same lines, same order and same effect as setDir(), setSide() and step1() - what is
   missing is only the delay() each of those ends with. A caller that uses these owns
   the settling, and XCopyLive owns it from a state machine so that the flux capture
   keeps being drained while the head is moving.
*/
void XCopyFloppy::setDirFast(int dir)
{
    motorTick = 0;
    _floppyPos.dir = dir;
    digitalWriteFast(_dir, dir == 0 ? HIGH : LOW);
}

void XCopyFloppy::setSideFast(int side)
{
    motorTick = 0;
    _floppyPos.side = side;
    digitalWriteFast(_side, side == 0 ? HIGH : LOW);
}

void XCopyFloppy::stepPulse()
{
    motorTick = 0;
    digitalWriteFast(_step, LOW);
    delayMicroseconds(stepPulseUs);
    digitalWriteFast(_step, HIGH);

    if (_floppyPos.dir == 0)
        _floppyPos.track--;
    else
        _floppyPos.track++;
}

bool XCopyFloppy::readTrack0Line() { return digitalRead(_track0) == 0; }

/*
   The "no click" disk-change probe every buffered host driver relies on.

   A drive latches /DSKCHG when a disk is removed and clears it only on a step pulse.
   Stepping outward while already at track 0 is the one step the mechanism refuses to
   act on, so the line is re-sampled and the head stays put. The track counter is
   restored because stepPulse() has no idea the drive ignored it.
*/
bool XCopyFloppy::noClickStep()
{
    if (!readTrack0Line())
        return false;

    setDirFast(0);
    delayMicroseconds(10); // direction must be stable before the step edge (spec: 1us)
    stepPulse();
    setTrackPosition(0);
    return true;
}

/*
   The disk change line as it stands, with nothing moved to find out.

   diskChange() steps the head to make the drive update the line, which is right when
   nothing else is going on and completely wrong in the middle of a stream. This is
   the passive read, for a caller that is already stepping for its own reasons.
*/
bool XCopyFloppy::readDiskChangeLine() { return digitalRead(_diskChange) == 1; }

/*
   Hands motor ownership to the caller across a disk change. See diskChangeIRQ().
*/
void XCopyFloppy::setDiskChangeStopsMotor(bool enabled) { diskChangeStopsMotor = enabled; }

/*
   Holds drive select for the caller, so the drive keeps driving its status lines
   even with the motor stopped. See keepDriveSelected.
*/
void XCopyFloppy::setKeepDriveSelected(bool enabled)
{
    keepDriveSelected = enabled;
    if (enabled)
        driveSelect();
}

// --- raw line control, for the drive toolkit ---------------------------------
// See the note in the header. One line each, no coupling, no settling.

void XCopyFloppy::setSelectLine(bool asserted)
{
    digitalWriteFast(_drivesel, asserted ? LOW : HIGH);
    delayMicroseconds(100);
}

void XCopyFloppy::setMotorLine(bool asserted)
{
    // Kept in step with motorOn()/motorOffRaw() so the idle timeout and the disk
    // change interrupt do not act on a stale idea of whether the spindle is up.
    motorTick = 0;
    motor = asserted;
    digitalWriteFast(_motor, asserted ? LOW : HIGH);
}

void XCopyFloppy::setDensityLine(bool high)
{
    digitalWriteFast(_dens, high ? HIGH : LOW);
}

// Read back at the pin rather than from a shadow variable, so what is reported is
// what the drive is actually being shown.
bool XCopyFloppy::readSelectLine() { return digitalRead(_drivesel) == LOW; }
bool XCopyFloppy::readMotorLine() { return digitalRead(_motor) == LOW; }
bool XCopyFloppy::readDensityLine() { return digitalRead(_dens) == HIGH; }
//! setDir() drives LOW for dir != 0, which steps the head inward.
bool XCopyFloppy::readDirInward() { return digitalRead(_dir) == LOW; }
//! setSide() drives LOW for side != 0.
bool XCopyFloppy::readSideLower() { return digitalRead(_side) == LOW; }
bool XCopyFloppy::readWriteProtectLine() { return digitalRead(_wprot) == 0; }

bool XCopyFloppy::readDataActive(uint16_t microseconds)
{
    const int first = digitalRead(_readdata);
    const uint32_t start = micros();
    while (micros() - start < microseconds)
    {
        if (digitalRead(_readdata) != first)
            return true;
    }
    return false;
}

uint32_t XCopyFloppy::getIndexEdges()
{
    noInterrupts();
    const uint32_t count = indexEdgeCount;
    interrupts();
    return count;
}

void XCopyFloppy::clearIndexEdges()
{
    noInterrupts();
    indexEdgeCount = 0;
    interrupts();
}

void XCopyFloppy::setTrackPosition(int cylinder)
{
    _currentTrack = cylinder;
    _floppyPos.track = (byte)cylinder;
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
            String line = twoDecimals(zeit).append(":").append(i).append("-").append(hist[i]);
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
    if (motor == true && diskChangeStopsMotor)
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
