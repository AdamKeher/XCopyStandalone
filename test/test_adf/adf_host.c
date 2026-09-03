/*
   The one thing ADFlib asks of whoever is embedding it: what time is it.

   See src/adflib/PATCHES.md. On the device XCopyAdfHost.cpp answers this from
   TimeLib. Here it answers with a fixed date, which is what makes an image the
   tests write reproducible - a real clock would put a different timestamp in every
   file header and a byte for byte comparison would never hold.

   The date is the one on the XCopy Standalone's first public release, which is as
   good as any and better than an epoch that reads as a bug.
*/

#include "adf_util.h"

void adfHostGiveCurrentTime(struct DateTime *const dt)
{
    dt->year = 119; /* since 1900, as struct tm counts it: 2019 */
    dt->mon = 3;
    dt->day = 8;
    dt->hour = 0;
    dt->min = 53;
    dt->sec = 41;
}
