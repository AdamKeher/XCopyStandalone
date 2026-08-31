#ifndef XCOPYLIVE_H
#define XCOPYLIVE_H

#include <Arduino.h>
#include "XCopyLiveProtocol.h"
#include "XCopyLiveCapture.h"
#include "XCopyLivePLL.h"
#include "XCopyFloppy.h"
#include "XCopyGraphics.h"

/*
   A live streaming session: the USB CDC port in binary mode, speaking
   shared/XCopyLiveProtocol.h to a host that is doing the decoding.

   The console command "live" hands control here and gets it back when the session
   ends. While a session is running nothing else on the device is serviced - no TFT
   redraws, no ESP polling, no audio - which is not an oversight but the point: the
   timing is the product, and every one of those subsystems is put back exactly as it
   was on the way out.

   Five things run in the loop, in this order, every pass:

     pumpUsb()      moves finished bytes out, never blocking
     pollCommands() reads and acts on host frames, including mid stream
     pumpSeek()     advances a head move that is in progress
     drain()        turns captured flux into records
     pollDrive()    watches the media lines

   drain() is bounded to one record per pass - about 160us of PLL work at DD - so the
   worst case from a byte arriving to the command being acted on is that bound plus one
   USB poll, comfortably inside the millisecond the protocol asks for.

   The seek is a state machine rather than the delay()-driven gotoLogicTrack() the rest
   of the device uses, and that is the single most important structural decision in
   this file. A 40 cylinder seek is around 155ms of settling and stepping; the capture
   ring holds 78ms. A blocking seek would overrun the ring every time. So a live seek
   cannot block, and the loop above keeps draining the whole way through it - which is
   also what makes XCL_CAP_INBAND_SEEK true rather than merely claimed.
*/

/*
   Outbound ring. Power of two.

   Carved out of the tail of the 25.5KB track buffer rather than taking bss, for the
   same reason the capture ring borrows the front of it: that buffer is completely idle
   for the whole of a live session, and the free heap block below SRAM_U is only about
   5KB with every console String competing for it.

   The size matters more than it looks. At 1KB only one flux record can ever be in
   flight, so the drain could not start the next one until USB had taken the last -
   which made it break exactly even with the drive and leave it no way to work off a
   backlog. It would drift a whole capture ring behind and trip an overrun a few times
   a second while still, confusingly, delivering 99% of the data. 4KB lets several
   records queue, so the drain can run ahead and catch up, and it costs 10ms off the
   capture ring's stall tolerance to buy it.
*/
#define XCL_TX_RING 4096
#define XCL_TX_MASK (XCL_TX_RING - 1)

/*
   Largest payload the device will emit, and therefore XclHello::maxRecordBytes.

   512 is a flux record of 256 uint16 samples. An MFM record of the maximum 1024 cells
   is only 128 bytes, so flux is what sizes this.
*/
#define XCL_MAX_PAYLOAD 512

//! One record, built contiguously so xclCrc16() can be handed the whole thing.
#define XCL_REC_SCRATCH (sizeof(XclRecordHeader) + XCL_MAX_PAYLOAD + 2)

/*
   Cells per MFM record, and the ceiling on XCL_CFG_RECORD_CELLS.

   The ceiling is not arbitrary. XclRecordHeader::ticks is 16 bits and a record of n
   cells spans up to n * cellTicks * (1 + pull): past 1024 cells at the nominal 48 tick
   cell that no longer fits, and the record would have to misreport its own duration -
   which is the one number the host turns into getMFMSpeed().
*/
/*
   A gap this long with no transition is reported as XCL_EV_NO_FLUX.

   4096 ticks is 170us at DD, twenty times the longest gap a formatted surface produces,
   so this only fires on erased or unformatted media - or on a head that is off the
   surface mid seek.
*/
/*
   How long a drain may spin waiting for the next transition before handing the CPU
   back to the command poll.

   The whole budget, plus one lap of the main loop, is the worst case latency from a
   host byte arriving to the command being acted on - so this is what keeps the
   protocol's sub-millisecond promise, and it is the number to lower if that promise
   ever gets tighter.
*/
#define XCL_DRAIN_BUDGET_US 250

#define XCL_NO_FLUX_TICKS 4096

#define XCL_MAX_RECORD_CELLS 1024
#define XCL_MAX_FLUX_SAMPLES (XCL_MAX_PAYLOAD / 2)

/*
   The session's buffers, at file scope rather than inside the object.

   3,214 bytes of them, and that is why they are here. A 64KB part that has already
   malloc'd a 25.5KB track buffer does not reliably have that much contiguous heap
   left: the free block below SRAM_U is about 5KB and is shared with every String the
   console and config paths allocate, and what is above the track buffer is shared
   with the stack. An object that size is a new that fails on a bad day - and it did,
   silently, because the failure lands as a null dereference before the session has
   printed anything at all. In bss the linker accounts for it once, up front, and it
   cannot fail.
*/
//! Assigned at session start to the tail of the track buffer. Not bss.
extern uint8_t *liveTx;
extern uint8_t liveRec[XCL_REC_SCRATCH];
extern uint8_t liveCellBuf[XCL_MAX_RECORD_CELLS / 8];
extern uint16_t liveFluxBuf[XCL_MAX_FLUX_SAMPLES];

class XCopyLive
{
public:
    XCopyLive(XCopyFloppy *floppy, XCopyGraphics *graphics)
        : _floppy(floppy), _graphics(graphics) {}

    /*
       Runs a session to completion. Blocks.

       Returns on XCL_CMD_BYE, on the watchdog expiring, when USB is unconfigured, or
       when @p cancel goes true - which is how the front panel cancel button gets out.
    */
    void run(volatile bool *cancel);

private:
    XCopyFloppy *_floppy;
    XCopyGraphics *_graphics;
    XCopyLiveCapture _capture;
    XCopyLivePLL _pll;

    volatile bool *_cancel = NULL;
    bool _running = false;

    // --- outbound -----------------------------------------------------------------
    uint32_t _txHead = 0;
    uint32_t _txTail = 0;
    uint32_t _bytesOut = 0;

    uint32_t _recLen = 0;

    inline uint32_t txUsed() const { return (_txHead - _txTail) & XCL_TX_MASK; }
    inline uint32_t txFree() const { return XCL_TX_MASK - txUsed(); }

    void startRecord(uint8_t type, uint8_t flags, uint16_t count, uint16_t ticks, uint32_t index);
    inline void addByte(uint8_t b) { liveRec[_recLen++] = b; }
    void addU16(uint16_t v);
    void addU32(uint32_t v);
    //! Seals the record with its CRC and queues it. False if the ring had no room.
    bool finishRecord();
    void pumpUsb();

    void emitEvent(uint8_t event, uint32_t arg, uint32_t arg2 = 0);
    void emitAck(uint8_t command, uint8_t result, uint16_t detail = 0);
    void emitHello();
    void emitStatus();
    uint8_t driveStatusBits() const;

    // --- inbound ------------------------------------------------------------------
    uint8_t _rxState = 0;
    uint8_t _rxIndex = 0;
    uint8_t _rxCmd = 0;
    uint8_t _rxLen = 0;
    uint8_t _rxPayload[XCL_CMD_MAX_PAYLOAD];
    uint32_t _lastCmdMs = 0;

    void pollCommands();
    void handleCommand();
    uint8_t applyConfig(uint8_t key, uint32_t value);

    // --- stream -------------------------------------------------------------------
    bool _streaming = false;
    bool _selftest = false;
    uint8_t _mode = XCL_MODE_MFM;

    /*
       The two counters the whole protocol rests on. Neither resets for a seek, a side
       change, an index, a mode switch or an overrun - only XCL_CMD_STREAM_START zeroes
       them. A record's header carries whichever of the two its type is indexed by, and
       an event carries both, so the host never has to guess which clock it is looking
       at.
    */
    uint32_t _cellIndex = 0;
    uint32_t _tickIndex = 0;

    uint32_t _tick0 = 0;         //!< absolute capture tick the stream started at
    uint32_t _cellRemainder = 0; //!< nominal cell clock remainder, flux and free-run
    uint32_t _pendingGap = 0;    //!< part of a long interval not yet fed to the PLL
    //! Zero cells decoded but not yet packed, and the 1 cell that closes the run.
    uint32_t _pendingZeros = 0;
    bool _pendingOne = false;
    /*
       Ticks the cell counter has been clocked through with no flux behind them, while
       the head was in flight. Subtracted from the next real interval so the time is
       never counted twice. This is what makes XCL_CAP_SEEK_PHASE true.
    */
    uint32_t _syntheticTicks = 0;
    uint32_t _overruns = 0;
    uint32_t _lastRevCells = 0;
    uint32_t _cellAtLastIndex = 0;
    bool _haveIndexAnchor = false;
    uint8_t _pendingFlags = 0; //!< XCL_RF_* owed to the next data record

    uint32_t _cellBits = 0;
    uint32_t _recordFirstCell = 0;
    uint32_t _recordStartTicks = 0;
    bool _recordUnlocked = false;

    uint32_t _fluxCount = 0;
    uint32_t _fluxTicks = 0;
    uint32_t _fluxFirstTick = 0;
    //! Part of an interval too long for one 16 bit sample, waiting for the next record.
    uint32_t _fluxPending = 0;

    uint32_t _lastCaptureOverruns = 0;
    uint32_t _selftestSeq = 0;

    // An index pulse the capture engine has reported but whose cell index is not known
    // yet, because the decoder has not reached the sample it fell on.
    bool _indexPending = false;
    uint32_t _indexAtTick = 0;
    uint32_t _indexPeriod = 0;

    void streamStart(uint8_t mode);
    void streamStop();
    void drain();
    void drainMfm();
    void drainFlux();
    void drainSelftest();
    void flushMfmRecord();
    void flushFluxRecord();
    void flushPartialRecord();
    void checkOverrun();
    void checkIndex();
    void freeRunCells();
    uint32_t streamTicksConsumed() const;
    void resetPll();
    uint32_t nextInterval();

    // --- head ---------------------------------------------------------------------
    enum SeekState : uint8_t
    {
        seekIdle = 0,
        seekDirWait,   //!< direction line set, waiting out the settle
        seekStepWait,  //!< between step pulses
        seekSettleWait //!< last step done, waiting for the head to stop ringing
    };

    SeekState _seekState = seekIdle;
    uint32_t _seekDeadline = 0;
    uint32_t _seekStartTick = 0;
    uint32_t _seekTicks = 0;
    int _seekTarget = 0;
    int _seekSteps = 0;
    bool _seekRecal = false;
    bool _trackChangePending = false;
    uint32_t _trackChangeAtTick = 0;
    uint8_t _cylinder = 0;
    uint8_t _side = 0;

    void beginSeek(uint8_t cylinder, uint8_t side, bool recal);
    void pumpSeek();
    void finishSeek();

    // --- config -------------------------------------------------------------------
    uint32_t _tickHz = 24000000;
    uint32_t _cellNs = 2000;
    uint32_t _cellTicks = 48;
    uint32_t _pullPermille = 100;
    uint32_t _adjDiv = 10;
    bool _adaptive = true;
    uint32_t _recordCells = 1024;
    uint32_t _fluxSamples = 256;
    uint32_t _watchdogMs = 5000;

    void recomputeTiming();

    // --- housekeeping -------------------------------------------------------------
    bool _lastDiskPresent = false;
    bool _lastWriteProtect = false;
    uint32_t _lastPollMs = 0;
    uint32_t _lastNoFluxMs = 0;

    void pollDrive();
    void showScreen(const char *line1, const char *line2);
};

#endif // XCOPYLIVE_H
