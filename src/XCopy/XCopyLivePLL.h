#ifndef XCOPYLIVEPLL_H
#define XCOPYLIVEPLL_H

#include <Arduino.h>

/*
   An adaptive phase locked loop, flux intervals in, bit cells out.

   The MFM read path in XCopyFloppy thresholds each interval against four fixed
   boundaries (low2/high2/high3/high4, auto tuned from a histogram once per track).
   That is enough for an AmigaDOS disk written by a healthy drive and it is not enough
   here, because this feature exists for the disks that are not that: custom formats,
   long tracks, deliberate speed variation, weak and fuzzy bits. A fixed threshold has
   no way to follow a track whose data rate drifts within a revolution, and no way to
   tell a genuinely late transition from one that arrived on time on a slow track.

   So this tracks instead. The window is a cell clock that follows the incoming flux
   inside a configurable pull range, the same shape as the PLL in Keir Fraser's scp.c
   that the host's ScpConverter is a port of, and with the same deliberate omission:

     the window is NOT snapped onto each transition.

   Snapping looks like the obvious thing to do and it is wrong. It throws away the
   information that the transition was early or late, which is the only evidence the
   loop has about where the next one will be. Carrying half the residual forward -
   flux = flux / 2 - halves the mean residual error against a snapping loop, and it is
   the difference between reading a marginal track and not.

   Fixed point, 8 fractional bits, in FTM0 ticks. Both densities put the nominal cell
   at 48 ticks because the prescaler follows the density, so one set of constants
   covers DD and HD.

   Usage:

     while (want more bits) {
         int bit = pll.nextBit();
         if (bit < 0) { pll.feed(nextFluxInterval()); continue; }
         emit(bit);
     }
*/

#define LIVE_PLL_FP 8

/*
   Largest interval handed to the loop in one go, in ticks.

   4,000,000 ticks is 167ms at DD, and shifted up by the fixed point that is still
   comfortably inside int32. A longer gap - an erased or unformatted region - is fed in
   chunks by the caller, which is equivalent because feed() accumulates. What is not
   acceptable is clamping it: a gap is data.
*/
#define LIVE_PLL_MAX_FEED 4000000UL

/*
   Consecutive zero cells that mean the loop is no longer locked to anything.

   MFM never produces a run this long - the longest legal gap between transitions is
   three zero cells - so 16 is well past anything a formatted surface can do, and short
   enough that an erased region is reported promptly rather than after the loop has
   spent a revolution free running.
*/
#define LIVE_PLL_UNLOCK_ZEROS 16

class XCopyLivePLL
{
public:
    /*
       @param cellTicks    nominal cell width in FTM0 ticks. 48 at both densities.
       @param pullPermille how far either side of nominal the clock may be pulled, in
                           parts per thousand. 100 - ten percent - is the usual figure
                           and is XCL_CFG_PLL_PULL_PERMILLE's default.
       @param adjDiv       phase adjustment divisor. The clock moves by one adjDiv'th
                           of the measured phase error per transition, so larger is
                           slower to follow and steadier once it has.
       @param adaptive     false pins the clock at nominal and never moves it, which is
                           XCL_CFG_PLL_MODE's fixed window. The window still slides with
                           the data - what stops is the frequency tracking - so a host
                           can compare a tracking read against a non tracking one on the
                           same disk without reflashing anything.
    */
    void begin(uint32_t cellTicks, uint32_t pullPermille, uint32_t adjDiv, bool adaptive)
    {
        _centre = (int32_t)(cellTicks << LIVE_PLL_FP);
        if (_centre < (8 << LIVE_PLL_FP))
            _centre = 8 << LIVE_PLL_FP;

        int32_t span = (int32_t)((int64_t)_centre * (int32_t)pullPermille / 1000);
        _min = _centre - span;
        _max = _centre + span;
        _adjDiv = adjDiv ? (int32_t)adjDiv : 1;
        _adaptive = adaptive;
        reset();
    }

    /*
       Back to the nominal clock with an empty window.

       Called at stream start, after a track change, and whenever lock is lost. The
       caller reports PLL_RESET when it does this, so the host knows the cells either
       side of that point were decoded by two different loops.
    */
    void reset()
    {
        _clock = _centre;
        _flux = 0;
        _fed = 0;
        _zeroRun = 0;
        _armed = false;
        _locked = true;
    }

    //! True when the window has no transition loaded and cannot decide the next cell.
    inline bool needsFlux() const { return !_armed; }

    /*
       Loads the time to the next transition.

       @return the part of @p ticks that did not fit, to be fed again next time round.
               Zero for any interval a disk actually produces; non-zero only for gaps
               past LIVE_PLL_MAX_FEED, which are split rather than clamped.
    */
    inline __attribute__((always_inline)) uint32_t feed(uint32_t ticks)
    {
        uint32_t take = ticks;
        uint32_t left = 0;

        if (take > LIVE_PLL_MAX_FEED)
        {
            left = take - LIVE_PLL_MAX_FEED;
            take = LIVE_PLL_MAX_FEED;
        }

        _flux += (int32_t)(take << LIVE_PLL_FP);
        _fed += take; // whole ticks: scaled, this would overflow int32 in 0.35s
        _armed = true;
        return left;
    }

    /*
       Consumes the loaded interval and returns the zero cells that precede its
       transition. The transition's own 1 cell is implied and is the caller's to emit.

       Decoding a transition at a time rather than a cell at a time is what makes this
       fast enough. Per cell, the loop below is a compare, a subtract and an increment;
       everything else - the phase error, the clock adjustment, the residual - is paid
       once per transition, which at DD is once every two and a half cells. Decoding
       cell by cell instead measured 222 cycles a cell and ran 16% slower than the
       drive, so it could never work off a backlog and tripped the capture ring every
       few hundred milliseconds.
    */
    inline __attribute__((always_inline)) uint32_t consume()
    {
        uint32_t zeros = 0;
        int32_t clock = _clock;
        int32_t flux = _flux;
        int32_t step = clock + (clock >> 1);

        while (flux >= step)
        {
            flux -= clock;
            zeros++;
        }

        // The transition is inside this cell. How far it sits from where the window
        // expected it is the phase error, and it is signed - see the note above.
        int32_t err = flux - clock;

        if (_adaptive)
        {
            clock += err / _adjDiv;
            if (clock < _min)
                clock = _min;
            else if (clock > _max)
                clock = _max;
            _clock = clock;
        }

        // Half the residual is carried into the next window rather than being zeroed.
        _flux = err / 2;
        _armed = false;

        if (zeros >= LIVE_PLL_UNLOCK_ZEROS)
        {
            // Nothing has been in step for longer than any format allows. Let the clock
            // fall back to nominal rather than hold a lock it does not have.
            _locked = false;
            _clock = _centre;
        }
        else
        {
            _locked = true;
        }

        return zeros;
    }

    /*
       Zero cells only, for a gap being fed in chunks.

       A gap too long for one feed() has no transition at the end of its early chunks,
       so those must not be credited with one.
    */
    inline __attribute__((always_inline)) uint32_t consumeZeros()
    {
        uint32_t zeros = 0;
        int32_t clock = _clock;
        int32_t flux = _flux;
        int32_t step = clock + (clock >> 1);

        while (flux >= step)
        {
            flux -= clock;
            zeros++;
        }

        _flux = flux;
        _armed = false;
        if (zeros)
            _locked = false;
        return zeros;
    }

    //! The cell clock now, in whole ticks. Reported in PLL_RESET and in STATUS.
    uint32_t clockTicks() const { return (uint32_t)(_clock >> LIVE_PLL_FP); }

    bool locked() const { return _locked; }

    /*
       Ticks the loop has turned into cells so far.

       Flux fed, less the part still sitting in the window. This is real elapsed disk
       time measured from the flux edges, which is what LiveDataHeader::elapsedTicks
       carries and what lets the host work out true bit cell density per record. It is
       deliberately not "cells times nominal clock", which would be a restatement of
       the assumption rather than a measurement of the disk.
    */
    uint32_t consumedTicks() const { return _fed - (uint32_t)(_flux >> LIVE_PLL_FP); }

private:
    int32_t _centre = 0;
    int32_t _clock = 0;
    int32_t _min = 0;
    int32_t _max = 0;
    int32_t _adjDiv = 10;
    int32_t _flux = 0;
    uint32_t _fed = 0;
    uint32_t _zeroRun = 0;
    bool _locked = true;
    bool _adaptive = true;
    bool _armed = false;
};

#endif // XCOPYLIVEPLL_H
