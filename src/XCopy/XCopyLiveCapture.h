#ifndef XCOPYLIVECAPTURE_H
#define XCOPYLIVECAPTURE_H

#include <Arduino.h>

/*
   Continuous flux capture for a live streaming session.

   This is the third consumer of the same FTM0 input capture that XCopyFloppy uses for
   MFM reads and for SCP imaging, and like the SCP one it installs its own handler over
   IRQ_FTM0 for the duration and puts the original back afterwards. What makes it a
   third one rather than a reuse of the second is a single requirement: it must never
   stop.

   XCopyFloppy's flux capture stops on a revolution count and stops on an overrun,
   because an SCP track that has holes in it is worthless and a retry is cheap. A live
   stream has no retry. It has to survive a seek, a side change, an index, a USB stall
   and a host that has stopped reading, and to keep the free running counters intact
   across all of them - so an overrun here drops samples and carries on, and nothing
   turns the timer off until the session ends.

   Two capture back ends
   ---------------------

   eDMA (preferred). FTM0 channel 7 raises a DMA request per transition and the eDMA
   engine writes the 16 bit capture register straight into the ring with no CPU
   involvement at all. At the 125-250k transitions/s a real disk produces, that is the
   difference between spending a fifth of the CPU inside an interrupt handler and
   spending none of it.

   Interrupt (fallback). The classic per transition handler, the same shape as
   XCopyFloppy::ftm0_flux_isr, storing deltas rather than absolute captures.

   Which one runs is decided at begin() by actually trying DMA and measuring whether it
   delivers - see probeDma(). It is not decided by a build flag or by trusting the
   reference manual, because the one thing that could make the DMA path silently
   useless is whether the FTM clears its channel flag on DMA completion, and the honest
   way to find that out on this silicon is to look. usingDma() reports the answer and
   it reaches the host in LiveHello::capabilities.

   What the consumer sees is identical either way: next() hands back one flux interval
   in FTM0 ticks, as a full 32 bit value, with long gaps intact.
*/

//! Index pulse slots. Power of two. One index every ~200ms, drained every loop pass.
#define LIVE_INDEX_SLOTS 8

/*
   Producer state, written by the capture interrupts.

   File scope and external linkage for the same reason XCopyFloppy's is: these are
   touched from handlers that fire every few microseconds, and reaching them through
   an instance pointer would add a load per access to the hottest code on the device.
   The consumer side reads them through the inline accessors below.
*/
extern uint16_t *liveRing;
extern volatile uint32_t liveRingSamples;

// interrupt back end
extern volatile uint32_t liveWriteIdx;   //!< producer cursor, ISR path only
extern volatile uint32_t liveWriteTotal; //!< samples ever stored, ISR path only
extern volatile uint32_t liveLast32;     //!< absolute tick of the previous transition
/*
   The consumer's cursor, published for the interrupt back end.

   The handler needs it to know when the ring is full, and it cannot reach a private
   member. One store per consumed sample is the whole cost, and it buys the handler a
   ring-full test that does not have to be conservative.
*/
extern volatile uint32_t liveReadTotal;

// DMA back end
extern volatile uint32_t liveDmaWraps; //!< times the DMA major loop has wrapped the ring

// shared
extern volatile uint32_t liveTofTotal;      //!< FTM0 overflows since the session began
extern volatile uint32_t liveOverruns;      //!< times the ring has been filled
extern volatile uint32_t liveDroppedSamples;//!< samples the producer could not store
extern volatile uint32_t liveIndexTick[LIVE_INDEX_SLOTS];
//! Absolute tick each index pulse fell on. NOT a ring position - see nextIndex().
extern volatile uint32_t liveIndexAt[LIVE_INDEX_SLOTS];
extern volatile uint8_t liveIndexHead;
extern volatile uint8_t liveIndexTail;
extern volatile uint32_t liveLastIndexTick;
extern volatile uint32_t liveLastIndexPeriod;
extern volatile uint32_t liveIsrMaxLatency; //!< worst case entry latency, ticks, ISR path
//! Capture ticks per microsecond, for tickNow(). 24 at DD, 48 at HD.
extern volatile uint32_t liveTicksPerUs;
//! The eDMA destination address register, read as the DMA write cursor.
extern volatile uint32_t *liveDmaDaddr;
//! DMA_INT bit for the capture channel, so writeTotal() can see an unserviced wrap.
extern volatile uint32_t liveDmaIntMask;

class XCopyLiveCapture
{
public:
    /*
       Takes FTM0 over and starts capturing.

       @param ring        sample ring. In practice XCopyFloppy::getStream(), the only
                          block of RAM on a 64KB part big enough to be a useful
                          backpressure margin, and idle for the whole session.
       @param ringSamples ring length in uint16 samples.
       @param indexPin    index pin, sampled by its own interrupt.
       @param tryDma      false forces the interrupt back end, for comparison.

       The motor must already be running: probeDma() decides the back end by watching
       for real transitions, and a stationary disk produces none.
    */
    bool begin(uint16_t *ring, uint32_t ringSamples, int indexPin, bool tryDma = true);

    //! Stops the capture and gives FTM0 back to XCopyFloppy's MFM handler.
    void end();

    bool usingDma() const { return _dma; }
    //! Why DMA was not used, or "" when it was. Reported by the CLI, not the protocol.
    const char *dmaReason() const { return _dmaReason; }

    // --- consumer -----------------------------------------------------------------

    //! Absolute index of the next sample the consumer will take.
    uint32_t consumed() const { return _consumed; }

    //! Samples stored but not yet consumed.
    inline __attribute__((always_inline)) uint32_t available() { return writeTotal() - _consumed; }

    /*
       The next flux interval, in FTM0 ticks.

       Full 32 bit, so a gap longer than the 16 bit counter arrives as the length it
       actually was rather than clamped - an unformatted region is data, and losing it
       is how a flux tool quietly stops being one. Only call when available() is
       non-zero.
    */
    inline __attribute__((always_inline)) uint32_t next();

    /*
       Oldest unread index pulse, if any.

       @param atTick absolute tick the pulse fell on, on the same free running counter
                     tickNow() reads. The consumer holds the event back until its own
                     decode position reaches that tick, which is what puts the event on
                     the exact cell rather than wherever the decoder happened to be
                     when the pulse arrived.
       @param period ticks since the previous index, 0 for the first of a session.

       Anchored in time rather than by ring position on purpose. A position would have
       to be sampled inside the index interrupt, and on the DMA back end the write
       cursor cannot be sampled atomically with its wrap count from an interrupt - that
       is what produced a phantom overrun every 56ms, each one reporting a whole ring
       dropped while the tick accounting showed nothing lost at all. Time has no such
       problem: it is one free running counter with one reader.
    */
    bool nextIndex(uint32_t *atTick, uint32_t *period);

    //! Ticks since the last index pulse, right now.
    uint32_t ticksSinceIndex() const;
    uint32_t lastIndexPeriod() const { return liveLastIndexPeriod; }

    //! Absolute tick now, on the same free running counter the intervals come from.
    static uint32_t tickNow();

    uint32_t overruns() const { return liveOverruns; }
    uint32_t droppedSamples() const { return liveDroppedSamples; }
    uint32_t worstIsrLatency() const { return liveIsrMaxLatency; }

    //! Samples the producer has stored. Consumer side only - see writeTotal().
    uint32_t writeCursor() { return writeTotal(); }

    /*
       Discards everything captured but not yet consumed and re-anchors on the ring as
       it stands now.

       Used after an overrun, and after a seek where the flux still in the ring came
       off the surface the head has just left. Returns the number of samples dropped,
       which is what the OVERRUN event reports. The counters the host sees - cells and
       ticks - are deliberately untouched: they are what the host resynchronises on.
    */
    uint32_t discardToNow();

private:
    bool _dma = false;
    const char *_dmaReason = "";
    uint32_t _consumed = 0;
    uint32_t _readIdx = 0;
    uint16_t _prevCapture = 0;
    int _indexPin = -1;

    bool startDma();
    void stopDma();
    bool probeDma();
    void startIsr();

    /*
       Absolute samples stored, whichever back end is running.

       NOT const and NOT callable from an interrupt: on the DMA back end it owns the
       wrap detection itself, by noticing the cursor going backwards. That is only
       sound with a single caller, which is why nothing in an interrupt handler uses it
       any more.
    */
    inline uint32_t writeTotal();

    uint32_t _writeBase = 0; //!< rings the DMA cursor has completed, counted here
    uint32_t _lastIdx = 0;   //!< previous DMA cursor, for spotting the wrap

    /*
       Plain copies of the ring and its length, for the hot path.

       The file scope originals have to be volatile because the interrupt back end
       writes them, and a volatile is a memory load the compiler may never keep in a
       register - three of them per sample, at 195,000 samples a second. Neither value
       changes for the life of a session, so the consumer takes its own copy once and
       the inner loop stops paying for someone else's synchronisation.
    */
    uint16_t *_ring = NULL;
    uint32_t _ringSamples = 0;
};

/*
   The DMA write cursor, as an absolute sample count.

   DADDR and the wrap counter are updated by two different interrupts, so they are read
   as a sequence lock: take the wrap count, take the address, take the wrap count again,
   and retry if the ring wrapped in between. Both handlers run at NVIC priority 0 so
   neither can preempt the other, but this can be called from the index handler, which
   is lower and can be preempted by both.
*/
inline uint32_t XCopyLiveCapture::writeTotal()
{
    if (!_dma)
        return liveWriteTotal;

    /*
       One register read, and the wrap counted here rather than in an interrupt.

       The eDMA engine resets DADDR to the base itself at the end of the major loop and
       the completion interrupt increments a counter a moment later, so no reader can
       sample the pair atomically: catch the gap and the cursor appears to move a whole
       ring, in one direction or the other, depending on which half you believe. Both
       spellings of that bug were tried and both produced phantom overruns - a whole
       ring reported dropped every 56ms while the tick accounting showed no time lost
       at all.

       There is no race left to lose once a single caller owns the whole thing. The
       cursor only ever moves forward within a ring, so a value below the previous one
       IS the wrap, and nothing else has to agree about when it happened. The one
       requirement is that this is called more often than the ring wraps - every 79ms
       at DD - and the drain calls it several times a millisecond.
    */
    uint32_t idx = ((uint32_t)*liveDmaDaddr - (uint32_t)_ring) >> 1;

    if (idx < _lastIdx)
        _writeBase += _ringSamples;
    _lastIdx = idx;

    return _writeBase + idx;
}

inline __attribute__((always_inline)) uint32_t XCopyLiveCapture::next()
{
    uint32_t delta;

    if (_dma)
    {
        /*
           The ring holds absolute 16 bit captures, so an interval is a subtraction and
           the mask covers exactly one counter wrap - which makes every interval up to
           2.73ms at DD exact, and that is every interval a formatted surface produces.

           A single interval LONGER than that is reported modulo 2.73ms on this back
           end, and that is a real limitation rather than an oversight. Resolving it
           needs to know how many times the counter wrapped between two particular
           samples, and there is no way to establish that here: the overflow is polled
           from the main loop rather than interrupt driven - it has to be, or the FTM
           storms the CPU, see startDma() - so by the time it is noticed the write
           cursor has moved on and any attribution to a sample is guesswork. Attempted
           and measured: it injected spurious 65,536 tick gaps into ordinary intervals
           and inflated the track to 158,000 cells a revolution against a true 101,400.

           Nothing is silently swallowed by this. A gap that long leaves the decoder
           starved, and XCopyLive reports it from the free running tick counter as
           XCL_EV_NO_FLUX with its true length. The interrupt back end reconstructs the
           full 32 bit interval directly and has no such limit, so a host that needs
           exact flux across erased media can compare the two - LiveHello reports which
           one is running.
        */
        uint16_t cur = _ring[_readIdx];
        uint16_t prev = _prevCapture;
        delta = (uint16_t)(cur - prev);
        _prevCapture = cur;
        if (++_readIdx >= _ringSamples)
            _readIdx = 0;
        // liveReadTotal is only there for the interrupt back end's ring-full test.
        // There is no handler on this path, so the volatile store is pure cost.
        _consumed++;
        return delta;
    }

    /*
       The interrupt back end stores deltas and splits anything past 16 bits into a run
       of 0xffff entries, so a run is summed back into the interval it came from. A run
       that is still being written is left alone until the rest of it arrives.
    */
    delta = 0;
    for (;;)
    {
        uint16_t v = liveRing[_readIdx];
        if (++_readIdx >= liveRingSamples)
            _readIdx = 0;
        liveReadTotal = ++_consumed;
        delta += v;

        if (v != 0xFFFF)
            break;
        if (liveWriteTotal == _consumed)
            break; // truncated run, take what is here rather than block
    }

    return delta;
}

#endif // XCOPYLIVECAPTURE_H
