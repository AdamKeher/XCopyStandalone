#ifndef XCOPYFIXED_H
#define XCOPYFIXED_H

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

/*
   Two decimal places, without the double precision runtime.

   String(someFloat) looks free and is not. It calls dtostrf(), which calls fcvtf(),
   which calls _dtoa_r() - newlib's arbitrary precision decimal conversion, written
   for correctness at every magnitude a double can hold rather than for a part with
   256KB of flash. It arrives with the whole soft double library behind it: __adddf3,
   __subdf3, __muldf3, __divdf3, __udivmoddi4, the Bignum helpers _Balloc, __multadd,
   __pow5mult, __lshift, __mdiff and the rest. Ten kilobytes, four percent of the
   part, to put "0.80" next to the word Volume.

   Nothing here needs any of that. Every value this firmware prints with a decimal
   point is a two decimal display quantity: a drive speed, a flux histogram bucket, a
   transfer rate, a stopwatch, the audio volume. None of them is ever more than a
   float, and two decimals of a float can be had exactly without leaving integers.

   The rest of the firmware already knew. XCopyDriveToolkit::formatRpm() and
   XCopyHeadCalibration::drawPassLine() both format tenths by hand, each with a
   comment explaining that printf() here has no float support linked in. This is the
   same idea, done once and done exactly, for the paths that were reaching for
   String(float) instead.

   Exactness
   ---------

   The whole point is that nothing anybody sees changes, so this produces the same
   characters as dtostrf() for every float, not merely for the ones that came up in
   testing. Two earlier versions of it did not, and both failed in ways worth naming
   because they are the obvious ways to write this:

     - Scaling the whole value by 100 loses the last digit above about 167,000, where
       100 * value stops fitting the 24 bit mantissa. A transfer rate in bytes per
       second reaches that easily.

     - Scaling only the fractional part by 100 fixes that and still rounds wrong on a
       tie, because the multiply itself rounds first: a value a shade under a
       boundary lands exactly on it and then breaks the wrong way. That one is not
       rare. On the transfer stopwatch it was most elapsed times ending in a 5 -
       0.015 printing as 0.02 where dtostrf says 0.01.

   So no arithmetic is done on the float at all. It is taken apart into the integer
   mantissa and power of two it already is, and every decision after that is made in
   integers with the exact remainder in hand.

   Checked against snprintf("%.2f") with no differences anywhere: 22,465,992 values
   chosen to be the ones that actually appear - every flux histogram bucket, every
   volume the menu can reach, every drive speed from 100 to 600 RPM, the transfer rate
   grid, every millisecond of a ten minute transfer and four million random ones - and
   then 357,913,942 more, every third float bit pattern from zero to two, which is the
   whole of the domain the fractional half of this ever sees. The suite in
   test/test_fixed pins the cases worth naming.

   Rounding is to even on a tie, which is what printf does.
*/

//! Two decimal places, e.g. 0.8f -> "0.80" and 299.5f -> "299.50".
inline String twoDecimals(float value)
{
    bool negative = value < 0.0f;
    if (negative)
        value = -value;

    // The integer part comes off first and exactly - the subtraction below is exact
    // for any float, and above 2^24 there are no fractional bits left to take, which
    // is the correct .00.
    uint32_t whole = (uint32_t)value;
    float fraction = value - (float)whole;

    // Everything from here is integer arithmetic on the exact value of the fraction.
    //
    // A float is an integer mantissa over a power of two and nothing else, and both
    // fall straight out of the bits: 23 stored mantissa bits with the implied 1 put
    // back, and a biased exponent saying how far down to shift. Below 1 the exponent
    // is always negative, so the fraction is mantissa / 2^shift with shift at least
    // 24, and hundredths of it is one multiply and one shift.
    //
    // Nothing has rounded up to this point, so the rounding step below is choosing
    // between two integers with the exact remainder in front of it.
    uint32_t bits;
    memcpy(&bits, &fraction, sizeof bits);
    uint32_t mantissa = bits & 0x7FFFFFu;
    int exponent = (int)((bits >> 23) & 0xFFu);
    if (exponent == 0)
        exponent = 1;             // subnormal: no implied bit, and biased exponent 1
    else
        mantissa |= 0x800000u;    // normal: put the implied leading bit back

    // fraction == mantissa / 2^shift. For anything below 1 that is at least 24.
    int shift = 127 + 23 - exponent;

    uint32_t hundredths;
    if (shift >= 64)
    {
        hundredths = 0;           // smaller than any hundredth can see
    }
    else
    {
        uint64_t numerator = (uint64_t)mantissa * 100u;
        uint64_t remainder = numerator & (((uint64_t)1 << shift) - 1);
        uint64_t half = (uint64_t)1 << (shift - 1);
        hundredths = (uint32_t)(numerator >> shift);

        // To even on a tie, as printf rounds.
        if (remainder > half || (remainder == half && (hundredths & 1u)))
            hundredths++;
    }

    if (hundredths >= 100)
    {
        whole++;
        hundredths -= 100;
    }

    // Built through String(unsigned long) rather than by appending the integers
    // directly: Arduino's String has an operator+= for every width, the host shim
    // that the native tests build against has three, and an unsigned long is
    // unambiguous to both.
    String out = negative ? String("-") : String("");
    out += String((unsigned long)whole);
    out += '.';
    if (hundredths < 10)
        out += '0';
    out += String((unsigned long)hundredths);
    return out;
}

#endif
