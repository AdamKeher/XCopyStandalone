#include "XCopySdFat.h"
#include "XCopyPins.h"

// File scope rather than a function local static: a function local would pull in the
// __cxa_guard_* thread safe initialisation helpers for no benefit on a single core part.
static SdFat _sd;

SdFat &xcopySd()
{
    return _sd;
}

bool xcopySdBegin()
{
    // Highest speed supported by the board that is not over 50 MHz. Try a lower speed
    // if SPI errors occur. SPI_FULL_SPEED, the SdFat default, is the same value.
    return _sd.begin(PIN_SDCS, SD_SCK_MHZ(50));
}
