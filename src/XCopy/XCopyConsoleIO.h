#ifndef XCOPYCONSOLEIO_H
#define XCOPYCONSOLEIO_H

#include <Arduino.h>

/*
   The two things the line editor and the completer need from the outside world,
   passed in as function pointers rather than reached for through an include.

   It is a small indirection and it buys something specific: XCopyLineEditor,
   XCopyComplete, XCopyArgs and XCopyCommandTable then depend on nothing but
   Arduino's String. That is what makes them buildable, and therefore testable, on
   a host - the alternative was dragging XCopyLog, XCopyESP8266, XCopySDCard and
   SdFat along behind them, which is most of the firmware.

   It is also honest about what they actually do. The editor does not want the log;
   it wants somewhere to put a line of text. The completer does not want an SD
   card; it wants to know what is in a directory.
*/

//! Where a line of console output goes.
typedef void (*XCopyWriter)(const String &text);

//! One entry of a directory, reported to whoever asked for the listing.
typedef void (*XCopyDirVisit)(void *context, const String &name, bool isDirectory);

/**
 * @brief Lists @p directory, calling @p visit once per entry.
 *
 * Reported rather than returned, so nothing has to hold a list of four hundred
 * filenames on a part with six kilobytes to spare.
 */
typedef void (*XCopyDirLister)(const String &directory, XCopyDirVisit visit, void *context);

#endif // XCOPYCONSOLEIO_H
