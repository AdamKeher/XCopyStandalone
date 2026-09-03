#include "XCopyAdfHost.h"

#include <TimeLib.h>
#include <stdarg.h>
#include <stdio.h>

#include "XCopyLog.h"
#include "XCopySdFat.h"
#include "XCopyAdfSdDriver.h"

extern "C"
{
#include "../adflib/adflib.h"
#include "../adflib/adf_util.h"
#include "../adflib/adf_byteorder.h"
}

/*
   What adflib.c's checkInternals() checks, checked at build time instead.

   Upstream runs these as if statements inside adfLibInit() and prints to stderr if
   one fails, which on this board would be a message nobody is watching for on a
   part that has already been programmed. Every one of them is a property of the
   struct layout, so the compiler can answer it - and adflib.c is then not needed in
   the firmware at all, which is why platformio.ini filters it out.
*/
static_assert(sizeof(short) == 2, "short is not 16 bits");
static_assert(sizeof(int32_t) == 4, "int32_t is not 32 bits");
static_assert(sizeof(struct AdfEntryBlock) == 512, "AdfEntryBlock is not one block");
static_assert(sizeof(struct AdfRootBlock) == 512, "AdfRootBlock is not one block");
static_assert(sizeof(struct AdfDirBlock) == 512, "AdfDirBlock is not one block");
static_assert(sizeof(struct AdfBootBlock) == 1024, "AdfBootBlock is not two blocks");
static_assert(sizeof(struct AdfFileHeaderBlock) == 512, "AdfFileHeaderBlock is not one block");
static_assert(sizeof(struct AdfFileExtBlock) == 512, "AdfFileExtBlock is not one block");
static_assert(sizeof(struct AdfOFSDataBlock) == 512, "AdfOFSDataBlock is not one block");
static_assert(sizeof(struct AdfBitmapBlock) == 512, "AdfBitmapBlock is not one block");
static_assert(sizeof(struct AdfBitmapExtBlock) == 512, "AdfBitmapExtBlock is not one block");
static_assert(sizeof(struct AdfLinkBlock) == 512, "AdfLinkBlock is not one block");

/*
   And the byte order check, which upstream does by reading a union at run time.

   adf_byteorder.h picks LITT_ENDIAN from the target macros, and the whole of the
   block swapping in adf_raw.c turns on it, so getting it wrong would not fail - it
   would quietly read every long on the disk backwards. Included here specifically
   so the decision it made is checked against what the compiler knows.
*/
#ifdef LITT_ENDIAN
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "ADFlib decided LITT_ENDIAN but the target is big endian");
#else
static_assert(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__,
              "ADFlib decided big endian but the target is little endian");
#endif

namespace
{
    bool _started = false;
    bool _echo = true;
    bool _failed = false;

    /*
       One buffer, reused. It is 128 bytes on a part with a 6KB stack, which is
       enough for every format string in the library - the longest is a bitmap
       complaint naming a volume and two sector numbers - and small enough that
       three nested frames holding one would still not be the thing that overflows.
    */
    const size_t kMessage = 128;
    char _message[kMessage] = "";

    void record(const char *prefix, const char *format, va_list args)
    {
        char text[kMessage];
        vsnprintf(text, sizeof(text), format, args);

        // The latch keeps the FIRST message, not the last. A failure usually
        // cascades - a bad checksum becomes a failed mount becomes a null volume -
        // and the first one is the one that says what actually went wrong.
        if (!_failed)
        {
            _failed = true;
            strncpy(_message, text, kMessage - 1);
            _message[kMessage - 1] = '\0';
        }

        if (_echo)
            Log << prefix << text << F("\r\n");
    }

    void onError(const char *const format, ...)
    {
        va_list args;
        va_start(args, format);
        record("adf: ", format, args);
        va_end(args);
    }

    void onWarning(const char *const format, ...)
    {
        va_list args;
        va_start(args, format);
        record("adf: warning: ", format, args);
        va_end(args);
    }

    /*
       Verbose is dropped rather than printed.

       adfEnv.vFct is called per block on some paths, and every line of it would go
       out over the websocket at 6ms a line. It is wired up rather than left null
       because the library calls it unconditionally.
    */
    void onVerbose(const char *const, ...) {}
}

void XCopyAdf::begin()
{
    // The card can be swapped at any time, so re-mount rather than assume - the
    // same reason xcopySdBegin() is safe to call repeatedly.
    xcopySdBegin();

    if (_started)
        return;

    adfEnvInitDefault();
    adfEnvSetFct(onError, onWarning, onVerbose, nullptr);

    adfAddDeviceDriver(&xcopyAdfSdDriver);

    _started = true;
}

void XCopyAdf::end()
{
    if (!_started)
        return;

    adfRemoveDeviceDrivers();
    adfEnvCleanUp();
    _started = false;
}

void XCopyAdf::clearErrors()
{
    _failed = false;
    _message[0] = '\0';
}

bool XCopyAdf::failed()
{
    return _failed;
}

const char *XCopyAdf::message()
{
    return _message;
}

void XCopyAdf::setEcho(bool enabled)
{
    _echo = enabled;
}

/*
   The wall clock.

   TimeLib counts years from 1970 and ADFlib, following struct tm, counts them from
   1900, so the 70 is a unit conversion and not a fudge. adfTime2AmigaTime() then
   turns this into days since 1978 for the on disk format.

   An unset clock reads as 1970 here and lands in the image as a 1970 date, which is
   what the SD card's own FAT timestamps do too (see XCopyDisk::dateTime). Better a
   wrong date than a refused write.
*/
extern "C" void adfHostGiveCurrentTime(struct DateTime *const dt)
{
    tmElements_t local;
    breakTime(now(), local);

    dt->year = local.Year + 70;  // tmElements_t counts from 1970, DateTime from 1900
    dt->mon = local.Month;
    dt->day = local.Day;
    dt->hour = local.Hour;
    dt->min = local.Minute;
    dt->sec = local.Second;
}
