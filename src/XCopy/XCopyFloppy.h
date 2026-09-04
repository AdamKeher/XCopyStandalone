#ifndef XCOPYFLOPPY_H
#define XCOPYFLOPPY_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyLog.h"
#include "../FastCRC/FastCRC.h"
// SCP_MAX_REVS: the most revolutions one flux capture will store, which is a limit
// of the image format rather than of the drive.
#include "SCPFormat.h"

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
   Head calibration.

   The verdict on one sector from one pass of the head calibration test, and the
   whole of one pass. This is a separate vocabulary from the _errors bitmask the
   copy path uses because it answers a different question: not "can this track be
   read" but "is the head over the cylinder it was told to go to". A sector that
   decodes perfectly but carries a neighbouring cylinder in its header is a clean
   read and an alignment fault at the same time, and only one of those two is
   interesting here.
*/
enum XCopySectorVerdict : uint8_t
{
    sectorMissing = 0, //!< no sync mark decoded carrying this sector number
    sectorOK,          //!< header and data checksums good, header names this cyl/head
    sectorCylLow,      //!< header names a LOWER cylinder than the one asked for
    sectorCylHigh,     //!< header names a HIGHER cylinder
    sectorHeadWrong,   //!< right cylinder, wrong side in the header
    sectorBadCheck     //!< right cylinder and head, but a checksum failed
};

/*
   One pass over one cylinder and one head.

   Deliberately fixed size and POD: two of these live for the whole of a
   calibration session on a part with roughly 6KB free, so nothing here may grow
   or allocate. status[] is sized for HD's 22 sectors; sectorCount says how much
   of it means anything for the density actually in the drive.
*/
struct CalibrationResult
{
    uint8_t cylinder;     //!< cylinder that was asked for
    uint8_t head;         //!< side that was asked for
    uint8_t sectorCount;  //!< 11 (DD) or 22 (HD): meaningful extent of status[]
    uint8_t valid;        //!< entries of status[] that are sectorOK
    uint8_t syncs;        //!< sync marks the capture ISR saw, before any decoding
    uint8_t strays;       //!< sync marks whose header could not be trusted:
                          //!< a failed header checksum or a sector number out of range
    uint8_t duplicates;   //!< sector numbers that appeared more than once
    uint8_t truncated;    //!< sectors whose 1088 bytes ran off the end of the capture
    int8_t cylinderSeen;  //!< cylinder the first readable header named, -1 if none did
    uint8_t status[22];   //!< XCopySectorVerdict, indexed by sector number
    /**
     * Sector number each sync mark carried, in the order the marks were found,
     * 0xff where the header could not be trusted. status[] is indexed by sector
     * number and so cannot say where on the track a sector physically sat; this
     * pairs with getSyncBytePos() to put a verdict at an angle, which is what
     * the disk info view draws. Head calibration does not read it.
     */
    uint8_t syncSector[22];
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
    //! Bytes allocated to getStream(). Kept next to the malloc that sizes it so a
    //! borrower - flux capture reuses this buffer - cannot get it wrong.
    size_t getStreamSize() { return streamSizeHD + 10; }

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

    /*
       Head and motor timings.

       These were literals inside setDir(), setSide(), step1(), gotoLogicTrack() and
       motorOn(), chosen for a UI driven copier where an extra 20ms on a seek is
       invisible. A live streaming host cares about every one of them, so they are
       settable - and every default below is exactly the number that was hard coded
       before, so nothing that does not ask behaves any differently.
    */
    void setStepPulseUs(uint32_t us);
    void setStepIntervalUs(uint32_t us);
    void setDirSettleUs(uint32_t us);
    void setSideSettleUs(uint32_t us);
    void setSeekSettleUs(uint32_t us);
    void setMotorSpinupMs(uint32_t ms);

    uint32_t getStepPulseUs() const;
    uint32_t getStepIntervalUs() const;
    uint32_t getDirSettleUs() const;
    uint32_t getSideSettleUs() const;
    uint32_t getSeekSettleUs() const;
    uint32_t getMotorSpinupMs() const;

    /*
       Lets the five second motor idle timeout run, or stops it.

       A live session spins the motor up once and leaves it up for as long as the host
       wants it, so the timeout is disabled for the duration and put back afterwards.
    */
    void setMotorIdleOff(bool enabled);

    /*
       Head movement with the waiting taken out.

       gotoLogicTrack() is built out of delay(), which is right everywhere it is used
       today and wrong for a live stream: a 40 cylinder seek is around 155ms of delay
       and the flux capture ring holds 78ms, so a blocking seek would overrun the ring
       every time. These are the same operations with the settling left to the caller,
       so a live session can drive a seek from its main loop and keep draining the
       capture the whole way through it.
    */
    void setDirFast(int dir);
    void setSideFast(int side);
    void stepPulse();
    //! One outward step at track 0: no movement, but the drive re-arms /DSKCHG.
    //! False if the track 0 line is not asserted, in which case nothing is pulsed.
    bool noClickStep();

    //! Reads the drive lines without moving anything. diskChange() steps the head.
    bool readTrack0Line();
    bool readDiskChangeLine();

    /*
       Raw interface line control and read back, for the drive toolkit.

       motorOn() asserts select, asserts motor and then blocks for motorSpinupMs,
       because every disk operation wants all three together. A diagnostic has to
       be able to take them apart: "does this drive respond to MOTOR ON at all"
       cannot be asked by a call that asserts select first, and "does select alone
       start the spindle" cannot be asked at all. These drive one line and return.

       motor is still tracked by setMotorLine(), so getMotorStatus(), motorTimeout()
       and diskChangeIRQ() keep agreeing with the hardware.
    */
    void setSelectLine(bool asserted);
    void setMotorLine(bool asserted);
    void setDensityLine(bool high);

    //! Commanded state of the output lines, read back at the pin.
    bool readSelectLine();
    bool readMotorLine();
    bool readDensityLine();
    //! True when DIR is asserted inward, matching setDir()'s dir != 0.
    bool readDirInward();
    //! True when SIDE selects head 1, matching setSide()'s side != 0.
    bool readSideLower();
    bool readWriteProtectLine();

    /**
     * @brief Is anything at all coming off the read head?
     *
     * Polls READ DATA for @p microseconds and says whether it moved. A level
     * sample of a line that toggles at a quarter of a megahertz while reading is
     * meaningless on its own, and attaching an interrupt to count edges properly
     * costs one ISR entry per flux transition - far too much to leave armed
     * behind a display that refreshes several times a second. This is bounded,
     * cheap, and answers the only question a bring up actually asks of the line.
     */
    bool readDataActive(uint16_t microseconds = 1000);

    //! Index edges since the last clearIndexEdges(). Counted by the RPM ISR, so
    //! it costs no interrupt of its own and is only live between beginRPM() and
    //! endRPM().
    uint32_t getIndexEdges();
    void clearIndexEdges();
    // Lets a caller keep the motor across a disk change - see diskChangeIRQ().
    void setDiskChangeStopsMotor(bool enabled);
    // Holds drive select, so the status lines stay valid with the motor stopped.
    void setKeepDriveSelected(bool enabled);

    int getCurrentTrack() const { return _currentTrack; }
    int getCurrentSide() const { return _floppyPos.side; }
    //! Tells the driver where the head actually is after a caller-driven seek.
    void setTrackPosition(int cylinder);

    // track transfer
    int readTrack(boolean silent);

    /**
     * @brief One capture pass over @p cylinder / @p head, classified for alignment.
     *
     * Deliberately not readTrack(). That one retries six times, calls
     * adjustTimings() between attempts, and turns a header naming the wrong
     * cylinder into a hard error - which is the single thing this test exists to
     * show the operator rather than hide from them. This does exactly one capture
     * with the density thresholds left alone, so consecutive passes are
     * comparable and a screw being turned shows up as the numbers changing.
     *
     * Headers and both checksums are read straight out of the raw stream via the
     * sync mark table, so this is unaffected by the data checksum bug in
     * decodeSector(). Neither _track[] nor _weakTracks[]/_trackLog[] is touched,
     * so a calibration session cannot disturb a later disk read.
     *
     * @param cylinder physical cylinder, 0 to MAX_CYLINDERS-1
     * @param head 0 (lower) or 1 (upper)
     * @param recal forget the head position first, so the seek goes through
     *        seek0() and a positioning fault reappears instead of being masked
     * @param out filled in on success, untouched on failure
     * @result false only when no capture happened at all - no disk, or the
     *         capture timed out. "0 of 11 valid" is a successful pass.
     */
    bool calibrationRead(uint8_t cylinder, uint8_t head, bool recal, CalibrationResult &out);

    /**
     * @brief The census half of calibrationRead(), over whatever last filled the
     *        capture buffers.
     *
     * Reads stream[] and sectorTable[] and touches nothing else, so a caller that
     * has put cells there by some other route - XCopyDiskInfo replaying the flux
     * out of an SCP file - gets the identical verdicts a disk read would. That is
     * the whole point of it being separate: an image and the disk it came from
     * cannot be analysed by two different pieces of code.
     */
    bool censusTrack(uint8_t cylinder, uint8_t head, CalibrationResult &out);

    /**
     * @brief One capture for analysis, started on the index pulse.
     *
     * calibrationRead()'s capture starts at an arbitrary rotation, which is fine
     * when the answer is a count and useless when it is a picture. Aligning to the
     * index makes a byte position in stream[] mean an angle on the disk.
     *
     * @param aligned set true when an index pulse was actually found. A drive that
     *        gives none is still surveyed, just without a meaningful rotation.
     * @result false only when no capture happened at all - no disk, or a timeout.
     */
    bool surveyCapture(uint8_t cylinder, uint8_t head, bool &aligned);

    /**
     * @brief Density of the captured cells over one slice of stream[], 0 to 15.
     *
     * The disk surface texture, one angular bucket at a time. Gap reads sparse,
     * sync marks and data read dense. Returned a bucket at a time rather than into
     * a caller's array because there is no room on this part for a 512 byte
     * buffer that only exists to be turned straight into a string.
     */
    uint8_t bitDensity(unsigned long fromByte, unsigned long toByte);

    //! Byte position of the nth sync mark in the last capture, 0 if there is no nth.
    unsigned long getSyncBytePos(byte index);

    //! Bytes the last capture actually filled. With the revolution length, an angle.
    int getStreamPos();

    /**
     * @brief Sync marks the capture handler will record before it stops counting.
     *
     * NOT setSectorCnt(), which sets the number already found. This is the bound
     * the handler tests against, otherwise only ever set by setMode(), and it is
     * why a DD capture stops looking after eleven sync marks. Raise it for a
     * survey of a track that may carry more, and put it back afterwards.
     */
    void setExpectedSectors(byte count);

    //! The bound setExpectedSectors() sets. Read it before widening a survey, so
    //! whatever setMode() chose for the density in the drive can be put back.
    byte getExpectedSectors();

    int writeTrack();

    /**
     * @brief Write `bytes` of MFM cells from the stream buffer, not the fixed track size.
     *
     * For a live session (XCL_CMD_WRITE_TRACK), where the host supplies the cells and
     * their length is whatever the guest's DMA produced - about 12,900 bytes for an
     * AmigaDOS DD track, but nothing guarantees it. The no-argument writeTrack() above
     * is the ADF-to-disk path and is unchanged; it delegates here with the mode's own
     * writeSize and an index wait.
     *
     * The caller has already put the cells at the front of the stream buffer, MSB
     * first, exactly as floppyTrackMfmEncode() would leave them. One byte past `bytes`
     * is written too: diskWrite() runs eight cells past the last byte the caller sent.
     *
     * @param bytes     cell bytes to clock out. Must leave room for that extra byte.
     * @param fromIndex wait for the index pulse before opening the write gate. False
     *                  starts wherever the head is, which is what a guest that did
     *                  not start its own write at the index asked for.
     * @result 0 written; -1 write-protected; -2 no index pulse inside the bound;
     *         -3 `bytes` out of range.
     */
    int writeTrack(int bytes, bool fromIndex);

    void floppyTrackMfmEncode(unsigned long track, byte *src, byte *dst);

    /*
       Raw flux capture, for SCP imaging.

       This is the same FTM0 input capture the MFM read path uses, with the interval
       kept instead of being thresholded into a bitcell and thrown away. It runs from a
       second ISR installed over the vector for the duration of a capture, so
       ftm0_isr() - the hot path for every ADF read - is left exactly as it was.

       The ring buffer is supplied by the caller because the only block of RAM big
       enough on a 64KB part is the MFM stream buffer, which is idle during a capture.
       See beginFluxCapture() for why that is safe and when it is not.
    */
    bool beginFluxCapture(uint16_t *ring, size_t ringSamples, uint8_t revolutions);
    void endFluxCapture();

    //! Longest run of samples readable without wrapping the ring. 0 when empty.
    size_t fluxPeek(const uint16_t **samples);
    //! Release @p count samples previously returned by fluxPeek().
    void fluxConsume(size_t count);

    bool fluxCaptureDone();
    bool fluxOverran();
    uint8_t fluxRevolutionsCaptured();
    //! Length of revolution @p rev in timer ticks, index to index.
    uint32_t fluxRevolutionTicks(uint8_t rev);
    //! Number of samples in revolution @p rev.
    uint32_t fluxRevolutionSamples(uint8_t rev);

    //! HD or DD, as last set by setMode() or densityDetect(). Sets the tick length.
    int getMode() { return _mode; }

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
    void setupFTM0Flux();
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
    //! Odd/even MFM unpack of the longword at @p p. decodeSector() open codes this
    //! three times over; calibrationRead() uses it by name.
    unsigned long decodeLongword(long p);
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
