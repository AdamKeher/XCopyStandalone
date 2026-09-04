/*
   twoDecimals(), tested on the host.

       pio test -e native

   The point of XCopyFixed.h is that removing the double precision runtime changed
   nothing anybody sees, so what is worth asserting is the output itself, character
   for character, against the values the firmware actually formats.

   The reference these were taken from is snprintf("%.2f"), which is what String(float)
   reached through dtostrf() and _dtoa_r() before this existed. Equivalence was
   established with no differences at all, over 22,465,992 values chosen to be the ones
   that actually appear - every flux histogram bucket, every volume the menu can reach,
   every drive speed from 100 to 600 RPM, the transfer rate grid, every millisecond of a
   ten minute transfer and four million random ones - and then 357,913,942 more, every
   third float bit pattern from zero to two.

   That sweep is not repeated here. It needs the double runtime to have something to
   compare against, which is the thing being removed. What is here instead is one case
   for each display that calls this, and one for each way the two earlier versions of
   twoDecimals() got it wrong - large values, and ties - so that a future edit that
   reintroduces either is caught rather than shipped.
*/

#include <unity.h>
#include <Arduino.h>
#include "XCopyFixed.h"

// Unity calls these around every case. Nothing here holds state between them.
void setUp(void) {}
void tearDown(void) {}

static void assertFormats(const char *expected, float value)
{
    String actual = twoDecimals(value);
    TEST_ASSERT_EQUAL_STRING(expected, actual.c_str());
}

// --- the volume menu ------------------------------------------------------------

void test_volume_steps(void)
{
    // Every value XCopy::navigateSelect can reach, stepping 0.2f and wrapping at 1.2f.
    assertFormats("0.00", 0.0f);
    assertFormats("0.20", 0.2f);
    assertFormats("0.40", 0.4f);
    assertFormats("0.60", 0.6f);
    assertFormats("0.80", 0.8f);
    assertFormats("1.00", 1.0f);
    assertFormats("1.20", 1.2f);
}

void test_volume_accumulates_without_drifting(void)
{
    // The menu does not recompute from a step count, it adds 0.2f to what it had.
    float volume = 0.0f;
    const char *expected[] = {"0.00", "0.20", "0.40", "0.60", "0.80", "1.00", "1.20"};
    for (int i = 0; i < 7; i++)
    {
        assertFormats(expected[i], volume);
        volume += 0.2f;
    }
}

// --- the flux histogram ---------------------------------------------------------

void test_histogram_buckets(void)
{
    // XCopy::sendBlock and XCopyFloppy::printHist both label buckets with
    // (i * 0.04166667f) + 0.25f microseconds.
    assertFormats("0.25", (0 * 0.04166667f) + 0.25f);
    assertFormats("0.29", (1 * 0.04166667f) + 0.25f);
    assertFormats("1.29", (25 * 0.04166667f) + 0.25f);
    assertFormats("4.42", (100 * 0.04166667f) + 0.25f);
    assertFormats("10.88", (255 * 0.04166667f) + 0.25f);
}

// --- drive speed ----------------------------------------------------------------

void test_rpm(void)
{
    assertFormats("300.00", 300.0f);
    assertFormats("299.50", 299.5f);
    assertFormats("301.25", 301.25f);
    assertFormats("0.00", 0.0f);
}

// --- transfer rate and stopwatch ------------------------------------------------

void test_transfer_rate(void)
{
    // XCopyDebug prints size * 1000 / elapsed_us as kbytes/sec.
    assertFormats("1000.00", 1000000.0f * 1000.0f / 1000000.0f);
    assertFormats("512.00", 512000.0f * 1000.0f / 1000000.0f);
}

void test_elapsed_seconds(void)
{
    // XCopyTransfer prints (millis() - start) / 1000.0f.
    assertFormats("0.00", 0.0f / 1000.0f);
    assertFormats("1.50", 1500.0f / 1000.0f);
    assertFormats("12.34", 12340.0f / 1000.0f);
    assertFormats("600.00", 600000.0f / 1000.0f);
}

// --- rounding -------------------------------------------------------------------

void test_rounds_to_even_on_a_tie(void)
{
    // What printf does. Rounding half up instead would disagree with dtostrf on
    // every exact tie, which at two decimals is not a rare shape of number.
    assertFormats("0.12", 0.125f);   // exactly on the tie, and 12 is even, so it stays
    assertFormats("0.14", 0.135f);   // 0.135f is actually a shade above, so it goes up
    assertFormats("2.50", 2.5f);
}

void test_rounds_up_away_from_a_tie(void)
{
    assertFormats("0.13", 0.126f);
    assertFormats("0.12", 0.124f);
}

void test_elapsed_times_that_land_near_a_tie(void)
{
    /*
       These are the ones a float multiply gets wrong. Every one of them is an
       elapsed time ending in a 5, where dividing by 1000 lands a hair to one side of
       a rounding boundary or exactly on it, and scaling the fraction by 100 in float
       would round it onto the boundary and lose which side it came from. The
       expected values here are dtostrf's.
    */
    assertFormats("0.01", 15.0f / 1000.0f);
    assertFormats("0.03", 25.0f / 1000.0f);
    assertFormats("0.05", 45.0f / 1000.0f);
    assertFormats("0.05", 55.0f / 1000.0f);
    assertFormats("0.09", 85.0f / 1000.0f);
    assertFormats("0.09", 95.0f / 1000.0f);
    assertFormats("0.17", 165.0f / 1000.0f);
    assertFormats("0.17", 175.0f / 1000.0f);
    assertFormats("0.19", 185.0f / 1000.0f);
    assertFormats("0.19", 195.0f / 1000.0f);
    assertFormats("0.23", 235.0f / 1000.0f);
    assertFormats("0.25", 245.0f / 1000.0f);
}

// --- magnitude ------------------------------------------------------------------

void test_large_values_keep_their_last_digit(void)
{
    // Scaling the whole value by 100 loses the mantissa above ~167,000. Splitting
    // the integer part off first is what keeps these right.
    assertFormats("1000000.00", 1000000.0f);
    assertFormats("123456.75", 123456.75f);
    assertFormats("16777216.00", 16777216.0f); // 2^24, no fractional bits left
}

void test_negatives(void)
{
    assertFormats("-1.50", -1.5f);
    assertFormats("-0.25", -0.25f);
}

void test_carry_out_of_the_fraction(void)
{
    // Rounding 0.999 up must carry into the integer part rather than print "0.100".
    assertFormats("1.00", 0.999f);
    assertFormats("300.00", 299.999f);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_steps);
    RUN_TEST(test_volume_accumulates_without_drifting);
    RUN_TEST(test_histogram_buckets);
    RUN_TEST(test_rpm);
    RUN_TEST(test_transfer_rate);
    RUN_TEST(test_elapsed_seconds);
    RUN_TEST(test_rounds_to_even_on_a_tie);
    RUN_TEST(test_rounds_up_away_from_a_tie);
    RUN_TEST(test_elapsed_times_that_land_near_a_tie);
    RUN_TEST(test_large_values_keep_their_last_digit);
    RUN_TEST(test_negatives);
    RUN_TEST(test_carry_out_of_the_fraction);
    return UNITY_END();
}
