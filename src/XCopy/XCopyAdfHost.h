#ifndef XCOPYADFHOST_H
#define XCOPYADFHOST_H

#include <Arduino.h>

/*
   Everything ADFlib needs from this firmware, and nothing this firmware needs from
   ADFlib.

   The vendored library is a clean copy of upstream 0.10.7 (see src/adflib/PATCHES.md)
   and knows nothing about Teensys, SD cards or terminals. Three things have to be
   supplied from outside it, and they all live here:

     - where its diagnostics go, which is XCopyLog, not stderr;
     - what the time is, which is TimeLib, not time();
     - which device drivers exist, which on this board is the SD image driver and,
       later, the live floppy.

   The previous integration answered the first two by editing the library - roughly
   560 changed lines, mostly printf rewrites - because 0.7.11a had no callback that
   took a format string and no driver registry to add to. Both exist now. Keep the
   answers here.
*/
namespace XCopyAdf
{
    /**
     * @brief Initialise the library and register the drivers this board has.
     *
     * Safe to call repeatedly; every entry point that is about to use ADFlib calls
     * it, the same way xcopySdBegin() re-mounts the card rather than assuming.
     * Mounts a card too, since every driver here needs one.
     */
    void begin();

    /**
     * @brief Drop the drivers and the environment.
     *
     * Not needed between commands - begin() is idempotent - but it exists so a
     * caller that has finished with ADFlib can prove it left nothing behind.
     */
    void end();

    /*
       The error latch.

       ADFlib reports failures by calling adfEnv.eFct and returning a code, and for
       a good few paths the code is all a caller gets - adfDevOpen() returns NULL
       whether the file was missing, unreadable or not an image. The message says
       which. Latching it lets a command print the reason without the library
       having to be told who is asking.

       clear() before the operation, failed() and message() after it.
    */
    void clearErrors();
    bool failed();
    //! The first error or warning since clearErrors(), or "" if there was none.
    const char *message();

    /**
     * @brief Whether diagnostics are echoed to the console as they happen.
     *
     * On by default. Turn it off around a probe that is expected to fail - working
     * out whether a file is an ADF by trying to mount it, say - so the operator is
     * not shown an error for something that was only a question. The latch still
     * records it either way.
     */
    void setEcho(bool enabled);
}

/*
   The wall clock, called from the library. See src/adflib/PATCHES.md.

   C linkage: adf_util.c calls it, and adf_util.c is compiled as C.
*/
struct DateTime;
extern "C" void adfHostGiveCurrentTime(struct DateTime *const dt);

#endif // XCOPYADFHOST_H
