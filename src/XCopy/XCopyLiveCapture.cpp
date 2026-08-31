#include "XCopyLiveCapture.h"
#include <DMAChannel.h>

/*
   Register addresses and timer settings owned by XCopyFloppy.

   registerSetup() works these out from the board revision and setMode() sets the
   prescaler from the density, so they are read from there rather than duplicated -
   the capture here has to be on exactly the same channel, at exactly the same tick
   rate, as the MFM read path it is borrowing the timer from. XCopyFloppy.cpp keeps
   them at file scope with external linkage on purpose; the comment at the top of that
   file explains why.
*/
extern uint32_t FTChannelValue, FTStatusControlRegister, FTPinMuxPort;
extern word timerMode;
extern word filterSetting;
extern "C" void ftm0_isr(void);

// --- producer state -----------------------------------------------------------

uint16_t *liveRing = NULL;
volatile uint32_t liveRingSamples = 0;

volatile uint32_t liveWriteIdx = 0;
volatile uint32_t liveWriteTotal = 0;
volatile uint32_t liveLast32 = 0;
volatile uint32_t liveReadTotal = 0;

volatile uint32_t liveDmaWraps = 0;

volatile uint32_t liveTofTotal = 0;
volatile uint32_t liveOverruns = 0;
volatile uint32_t liveDroppedSamples = 0;
volatile uint32_t liveIndexTick[LIVE_INDEX_SLOTS];
volatile uint32_t liveIndexAt[LIVE_INDEX_SLOTS];
volatile uint8_t liveIndexHead = 0;
volatile uint8_t liveIndexTail = 0;
volatile uint32_t liveLastIndexTick = 0;
volatile uint32_t liveLastIndexPeriod = 0;
volatile uint32_t liveIsrMaxLatency = 0;

volatile uint32_t *liveDmaDaddr = NULL;
//! Capture ticks per microsecond: 24 at DD, 48 at HD. Set by begin().
volatile uint32_t liveTicksPerUs = 24;
volatile uint32_t liveDmaIntMask = 0;

// The active capture, so the interrupt handlers can reach the write cursor without
// an instance pointer. There is one drive and therefore one session.
static XCopyLiveCapture *liveActive = NULL;
static DMAChannel *liveDmaChannel = NULL;

extern "C" void ftm0_live_isr(void);
static void liveIndexISR();

/*
   Records an index pulse against the sample it fell between.

   Sampling the index from its own interrupt rather than from inside the capture
   handler costs a little jitter - this runs at the default interrupt priority and the
   capture runs at 0, so it can be held off by one capture, about a microsecond out of
   a 200ms revolution - and buys the DMA back end, which has no capture handler to
   sample it from.

   The sample position is what matters. An index event that says "somewhere around
   here" is no use for reconstructing angular position; one that names the exact
   sample the pulse fell on is, and that is what the consumer turns into a cell index.
*/
static void liveIndexISR()
{
    uint32_t now = XCopyLiveCapture::tickNow();
    uint32_t period = liveLastIndexTick ? (now - liveLastIndexTick) : 0;
    liveLastIndexTick = now;
    liveLastIndexPeriod = period;

    uint8_t head = liveIndexHead;
    uint8_t next = (head + 1) & (LIVE_INDEX_SLOTS - 1);
    if (next == liveIndexTail)
        return; // consumer is not draining; the period above is still current

    liveIndexTick[head] = period;
    liveIndexAt[head] = now;
    liveIndexHead = next;
}

/*
   Interrupt back end: one entry per flux transition.

   The same shape as XCopyFloppy's SCP capture handler, with the two differences a
   live stream needs. It never turns the timer off - not on an overrun, not on a
   revolution boundary - and when the ring is full it drops the sample and keeps
   going, because stalling the capture to make room would corrupt the angular mapping
   and that is the one thing that must never happen.
*/
extern "C" void ftm0_live_isr(void)
{
    volatile uint32_t *csc = (volatile uint32_t *)FTStatusControlRegister;
    uint32_t status = *csc;

    if (status & 0x80) // CHF: a transition was captured
    {
        uint16_t capture = (uint16_t)(*(volatile uint32_t *)FTChannelValue);
        uint16_t entry = (uint16_t)FTM0_CNT;
        *csc = status & ~0x80;

        /*
           How long it took to get in here, measured rather than assumed: the counter
           is free running, so the distance from the captured edge to the counter now
           is the entry latency including anything that held the handler off. This is
           the number that answers whether servicing USB can cost a transition - if it
           ever approaches the shortest flux interval the drive produces, it can.
        */
        uint16_t latency = entry - capture;
        if (latency > liveIsrMaxLatency && latency < 0x8000)
            liveIsrMaxLatency = latency;

        uint32_t tof = liveTofTotal;
        if ((FTM0_SC & 0x80) && (capture < 0x8000))
            tof++;

        uint32_t now = (tof << 16) | capture;
        uint32_t delta = now - liveLast32;
        liveLast32 = now;

        /*
           A gap too long for one 16 bit sample is split rather than clamped, so the
           samples still add up to the time that actually passed.

           When the ring is full the sample is dropped and the handler returns. It does
           not stop the timer and it does not wait: stalling the capture to make room
           would break the mapping from sample to angular position, and that mapping is
           the one thing in a live stream that must never be wrong. The consumer emits
           an OVERRUN carrying the gap and the host resynchronises from the next
           record's own cell index.
        */
        for (;;)
        {
            if (liveWriteTotal - liveReadTotal >= liveRingSamples - 1)
            {
                liveOverruns++;
                liveDroppedSamples++;
                return;
            }

            uint32_t next = liveWriteIdx + 1;
            if (next >= liveRingSamples)
                next = 0;

            liveRing[liveWriteIdx] = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;
            liveWriteIdx = next;
            liveWriteTotal++;

            if (delta <= 0xFFFF)
                break;

            delta -= 0xFFFF;
        }
    }

    if (FTM0_SC & 0x80) // TOF
    {
        FTM0_SC &= ~0x80;
        liveTofTotal++;
    }
}

/*
   DMA back end: the only thing left for the CPU is the timer overflow.

   Two jobs. It extends the 16 bit counter so tickNow() keeps working, and it records
   how many times the counter wrapped between one stored sample and the next, which is
   what lets the consumer tell a 2.7ms gap from a 200ms one. Without that the DMA path
   would silently clamp every long gap to 16 bits, and an unformatted track - the
   thing a flux tool exists to see - would come back looking formatted.

   An overflow belongs to the interval that ends at the sample the write cursor is
   pointing at now, because everything before that cursor is already stored.
*/
//! DMA major loop wrapped the ring. Every 13,053 samples, about 78ms of DD flux.
static void liveDmaCompleteISR()
{
    liveDmaWraps++;
    if (liveDmaChannel)
        liveDmaChannel->clearInterrupt();
}

/*
   The session's wall clock, in capture ticks.

   Derived from micros() rather than from the FTM overflow, and that is deliberate.

   Extending the 16 bit capture counter needs every overflow to be seen. The interrupt
   back end sees them because it is an interrupt; the DMA back end has no interrupt to
   spare - enabling one storms the CPU, see startDma() - so its overflows are polled,
   and a poll can only ever report that at least one happened, never how many. Missed
   ones cannot be recovered, and the errors do not cancel: the measured revolution came
   out at 101ms against a true 200ms, and a 40 cylinder seek timed itself at 27ms
   against a real 191ms.

   micros() is maintained by SysTick, cannot be missed, and is off by at most a
   microsecond - 0.0005% of a revolution. Nothing on this clock needs better than that:
   the flux intervals themselves, which are the measurement that matters, come from the
   16 bit capture deltas and are exact to the tick regardless.

   Everything here is used as a difference, so the phase offset against the FTM counter
   does not matter and the 179 second wrap is handled by unsigned arithmetic.
*/
uint32_t XCopyLiveCapture::tickNow()
{
    return micros() * liveTicksPerUs;
}

bool XCopyLiveCapture::begin(uint16_t *ring, uint32_t ringSamples, int indexPin, bool tryDma)
{
    if (ring == NULL || ringSamples < 1024)
        return false;

    liveRing = ring;
    liveRingSamples = ringSamples;
    _ring = ring;
    _ringSamples = ringSamples;
    liveWriteIdx = 0;
    liveWriteTotal = 0;
    liveReadTotal = 0;
    liveLast32 = 0;
    liveDmaWraps = 0;
    liveTofTotal = 0;
    liveOverruns = 0;
    liveDroppedSamples = 0;
    liveIndexHead = liveIndexTail = 0;
    liveLastIndexTick = 0;
    liveLastIndexPeriod = 0;
    liveIsrMaxLatency = 0;

    _consumed = 0;
    _readIdx = 0;
    _writeBase = 0;
    _lastIdx = 0;
    _prevCapture = 0;
    _indexPin = indexPin;
    _dma = false;
    _dmaReason = "";
    liveActive = this;

    // Input filter and prescaler exactly as the MFM path would set them, so a tick
    // means the same thing here as it does everywhere else on the device.
    /*
       Capture ticks per microsecond, taken from the prescaler rather than from the
       density flag, so it stays right if either ever changes. The low three bits of
       the timer mode are the FTM PS field and the divider is two to that power: HD
       runs PS=0 for 48MHz, DD runs PS=1 for 24MHz.
    */
    liveTicksPerUs = (uint32_t)((F_BUS / 1000000UL) >> (timerMode & 0x07));

    FTM0_FILTER = filterSetting;
    FTM0_MODE = 0x05;
    FTM0_SC = 0x00;
    FTM0_CNT = 0x0000;
    FTM0_MOD = 0xFFFF;
    (*(volatile uint32_t *)FTPinMuxPort) = 0x403;

    attachInterrupt(digitalPinToInterrupt(indexPin), liveIndexISR, FALLING);

    if (tryDma && startDma())
    {
        if (probeDma())
        {
            _dma = true;
            return true;
        }

        stopDma();
        _dmaReason = _dmaReason[0] ? _dmaReason : "no transfers observed";
    }
    else if (tryDma)
    {
        _dmaReason = "could not allocate a DMA channel";
    }
    else
    {
        _dmaReason = "not requested";
    }

    startIsr();
    return true;
}

/*
   Arms the eDMA path.

   The channel is worked out from the address registerSetup() chose rather than
   hard coded: FTChannelValue is C(n)V and C(n)SC is eight bytes below it, so the
   channel number falls out of the offset. On the board this firmware is built for
   that is FTM0 channel 7, Teensy pin 5 / PTD7 - the comment in XCopyFloppy.cpp that
   calls it FTM0_CH1 is wrong about the channel and right about the pin.

   destinationBuffer() wraps with the major loop rather than with modulo addressing,
   so the ring needs no particular alignment and DADDR reads back as a plain write
   cursor.
*/
bool XCopyLiveCapture::startDma()
{
    if (liveDmaChannel == NULL)
        liveDmaChannel = new DMAChannel();

    // begin() sets TCD to null when every channel is already spoken for, and using
    // the object after that hard faults.
    if (liveDmaChannel == NULL || liveDmaChannel->TCD == NULL)
        return false;

    uint32_t channel = (FTChannelValue - 0x40038010) / 8;
    if (channel > 7)
    {
        _dmaReason = "capture channel is not an FTM0 channel";
        return false;
    }

    liveDmaChannel->disable();
    liveDmaChannel->source(*(volatile uint16_t *)FTChannelValue);
    liveDmaChannel->destinationBuffer((volatile uint16_t *)liveRing, liveRingSamples * 2);
    liveDmaChannel->triggerAtHardwareEvent(DMAMUX_SOURCE_FTM0_CH0 + channel);
    liveDmaChannel->interruptAtCompletion();
    liveDmaChannel->attachInterrupt(liveDmaCompleteISR);

    // DADDR is the live write cursor. Read as a plain word: it is a 32 bit pointer
    // into a ring this code owns, and the seqlock in writeTotal() is what makes the
    // pair of it and the wrap count consistent.
    liveDmaDaddr = (volatile uint32_t *)(void *)&liveDmaChannel->TCD->DADDR;
    liveDmaIntMask = (uint32_t)1 << liveDmaChannel->channel;

    // The overflow handler and the DMA completion handler both touch the write
    // cursor, so they run at the same priority and cannot preempt each other.
    NVIC_SET_PRIORITY(IRQ_FTM0, 0);
    NVIC_SET_PRIORITY(IRQ_DMA_CH0 + liveDmaChannel->channel, 0);

    /*
       No FTM0 interrupt at all on this path, and that is the single most important
       line in this function.

       CHIE has to be set for the channel to raise a DMA request, and on this part
       setting it also asserts the NVIC line on every capture - the DMA request and the
       CPU request are the same flag. A handler that does not clear the flag therefore
       re-enters continuously until eDMA gets round to servicing it, and one that does
       clear it throws the transition away. Measured, the first spelling burned roughly
       500 cycles per transition at 195k transitions a second: the entire CPU, which is
       why the decoder ran at 92% of the drive and could never work off a backlog.

       Disabling the line removes both horns. eDMA does not need the NVIC to move a
       sample, so capture is untouched, and the only thing the interrupt was still good
       for - extending the 16 bit counter on overflow - is polled from the main loop by
       poll() instead. That is what "no per-transition interrupt" in XCL_CAP_DMA is
       supposed to mean, and now it is literally true.
    */
    NVIC_DISABLE_IRQ(IRQ_FTM0);

    // CHIE | ELSB | DMA: input capture on the falling edge, delivered to eDMA.
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x49;

    liveDmaChannel->enable();

    FTM0_CNT = 0x0000;
    FTM0_SC = timerMode | 0x40; // clock source and prescaler, plus TOIE

    return true;
}

void XCopyLiveCapture::stopDma()
{
    if (liveDmaChannel)
    {
        liveDmaChannel->disable();
        liveDmaChannel->detachInterrupt();
    }
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x48; // back to interrupt mode
    FTM0_SC = 0x00;
    liveDmaDaddr = NULL;
    liveDmaIntMask = 0;
}

/*
   Decides whether the DMA back end actually works on this silicon.

   The reference manual says the FTM clears its channel flag when the DMA transfer
   completes. If that is not what the part in front of us does, the first transition
   arms the flag, DMA moves one sample, the flag stays set and nothing else ever
   happens - a failure mode that looks exactly like a drive with no disk in it. So
   this does not trust the manual: it watches.

   The motor is already running when a stream starts, so a formatted disk delivers
   around 3,300 transitions in 20ms. Anything past a couple proves the request line,
   the flag clearing and the ring addressing are all working. One or none means either
   the flag is stuck or there is no disk, and the interrupt back end is correct in
   both cases - it is the conservative answer, and it is what ships today.
*/
bool XCopyLiveCapture::probeDma()
{
    uint32_t start = millis();
    uint32_t base = liveDmaWraps * liveRingSamples +
                    (((uint32_t)*liveDmaDaddr - (uint32_t)liveRing) >> 1);

    /*
       Bounded by a spin count as well as by the clock. If the channel flag is not
       being cleared for us the interrupt line is asserted solidly, SysTick never gets
       in, and millis() stops advancing - so a wait written only against millis() would
       never come back. The spin count is the escape, and the handler shutting itself
       down after 64 stuck flags is what lets the clock start again.
    */
    for (uint32_t spins = 0; spins < 4000000UL && (millis() - start) < 20; spins++)
    {
        uint32_t now = liveDmaWraps * liveRingSamples +
                       (((uint32_t)*liveDmaDaddr - (uint32_t)liveRing) >> 1);
        if (now - base >= 64)
            return true;

    }

    uint32_t moved = liveDmaWraps * liveRingSamples +
                     (((uint32_t)*liveDmaDaddr - (uint32_t)liveRing) >> 1) - base;

    if (moved <= 1)
        _dmaReason = "the DMA request line never fired";
    else
        _dmaReason = "too few transitions to confirm DMA - no disk?";

    return false;
}

void XCopyLiveCapture::startIsr()
{
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x48; // CHIE, falling edge

    // Above the USB handler, which the core leaves at 112, so servicing a USB packet
    // can never hold off a flux transition.
    NVIC_SET_PRIORITY(IRQ_FTM0, 0);
    attachInterruptVector(IRQ_FTM0, ftm0_live_isr);
    NVIC_ENABLE_IRQ(IRQ_FTM0);

    FTM0_CNT = 0x0000;
    FTM0_SC = timerMode | 0x40; // clock source and prescaler, plus TOIE
    liveLast32 = 0;
}

/*
   Gives FTM0 back.

   Safe on every exit path including cancellation, and safe to call twice. Leaving
   either live vector installed would break the next ADF read in a way that looks like
   a drive fault, which is exactly the trap XCopyFloppy::endFluxCapture() documents.
*/
void XCopyLiveCapture::end()
{
    FTM0_SC = 0x00;

    if (_dma)
        stopDma();

    if (_indexPin >= 0)
    {
        detachInterrupt(digitalPinToInterrupt(_indexPin));
        _indexPin = -1;
    }

    attachInterruptVector(IRQ_FTM0, ftm0_isr);
    (*(volatile uint32_t *)FTStatusControlRegister) = 0x48;

    liveActive = NULL;
    liveRing = NULL;
    _ring = NULL;
    _ringSamples = 0;
    // Cleared together with the pointer: a non-zero size against a null ring would let
    // a late consumer call index straight through it.
    liveRingSamples = 0;
    _dma = false;
}

bool XCopyLiveCapture::nextIndex(uint32_t *atTick, uint32_t *period)
{
    uint8_t tail = liveIndexTail;
    if (tail == liveIndexHead)
        return false;

    *period = liveIndexTick[tail];
    *atTick = liveIndexAt[tail];
    liveIndexTail = (tail + 1) & (LIVE_INDEX_SLOTS - 1);
    return true;
}

uint32_t XCopyLiveCapture::ticksSinceIndex() const
{
    uint32_t last = liveLastIndexTick;
    if (last == 0)
        return 0;
    return tickNow() - last;
}

uint32_t XCopyLiveCapture::discardToNow()
{
    if (_ringSamples == 0)
        return 0; // begin() failed or end() has run; there is no ring to discard

    uint32_t write = writeTotal();
    uint32_t skipped = write - _consumed;

    _readIdx = write % _ringSamples;
    _consumed = write;
    liveReadTotal = write;

    // The captures the skipped samples were measured against are gone, so the first
    // interval after this would be nonsense. Anchor on what the ring holds now.
    if (_dma && liveRingSamples)
    {
        uint32_t prev = (_readIdx == 0) ? _ringSamples - 1 : _readIdx - 1;
        _prevCapture = _ring[prev];
    }

    return skipped;
}
