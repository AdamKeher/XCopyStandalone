#ifndef XCOPYTRANSFER_H
#define XCOPYTRANSFER_H

#include <Arduino.h>
#include <SdFat.h>
#include "XCopyProtocol.h"
#include "XCopyGraphics.h"

/**
 * @brief The Teensy end of the file transfer protocol.
 *
 * The wire format both ends implement is documented in XCopyProtocol.h, which is
 * compiled into this tree and into the ESP8266 tree.
 *
 * This class owns nothing but the link. The SD card is the shared instance from
 * XCopySdFat.h, the screen is borrowed for a progress splash, and busy signalling and
 * the menu redraw stay with the caller -- these transfers are driven from
 * XCopy::onWebCommand() while the UI is otherwise idle.
 */
class XCopyTransfer
{
public:
  /**
   * @brief Bind the protocol to a link and a screen.
   *
   * @param link      the serial port the ESP is on (Serial1)
   * @param graphics  screen used for the progress splash, may be null
   */
  void begin(Stream *link, XCopyGraphics *graphics);

  /**
   * @brief Stream a file off the SD card to the ESP. Download half of the protocol.
   *
   * @param path  full path on the SD card
   * @result true if the whole file was sent
   */
  bool sendFile(const String &path);

  /**
   * @brief Receive a file from the ESP onto the SD card. Upload half of the protocol.
   *
   * @param path      full path on the SD card
   * @param filesize  byte count announced in the getFile command; the transfer is
   *                  length delimited, so this has to be right
   * @param overwrite replace an existing file rather than failing with "exists"
   * @result true if the whole file arrived and was written
   */
  bool getFile(const String &path, size_t filesize, bool overwrite);

private:
  // Sends "error,<reason>\n" and logs it. Exactly one status line ever goes back to
  // the ESP: it reads one, and anything further would be misread as file data or as a
  // console command.
  bool fail(const char *reason);

  Stream *_link = nullptr;
  XCopyGraphics *_graphics = nullptr;
};

#endif // XCOPYTRANSFER_H
