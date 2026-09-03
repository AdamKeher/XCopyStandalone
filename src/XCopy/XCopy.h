#ifndef XCOPY_H
#define XCOPY_H

#define XCOPYVERSION "v731.26"
// #define XCOPY_DEBUG = 1

// #define PCBVERSION 1 // expensive adafruit screen and joystick
#define PCBVERSION 2 // cheap screen and joystick

#if PCBVERSION == 1
    #define TFT_ROTATION 3
#else
    #define TFT_ROTATION 1
#endif


#define ESPSerial Serial1

// Baud rates, timeouts and the file transfer wire format are the ESP link contract and
// live in shared/XCopyProtocol.h, which both firmware trees compile.

#include <Arduino.h>
#include <SPI.h>
#include <SerialFlash.h>
#include <Wire.h>
#include <Streaming.h>
#include <SdFat.h>
#include "XCopyProtocol.h"
#include "XCopyState.h"
#include "XCopyAction.h"
#include "XCopyLog.h"
#include "XCopyPins.h"
#include "XCopyMenu.h"
#include "XCopyCommand.h"
#include "XCopyDisk.h"
#include "XCopyAudio.h"
#include "XCopyConfig.h"
#include "XCopyDirectory.h"
#include "XCopyGraphics.h"
#include "XCopyDebug.h"
#include "XCopyTime.h"
#include "XCopyESP8266.h"
#include "XCopyFloppy.h"
#include "XCopyHeadCalibration.h"
#include "XCopyConsole.h"
#include "XCopyBrainFile.h"
#include "XCopyTransfer.h"
#include "XCopyLive.h"

#ifdef XCOPY_DEBUG
#include "RamMonitor.h"
#endif

class XCopy {
public:
  XCopy(TFT_ST7735 *tft);

  void begin();
  void update();
  //! @param repeat true when the joystick is being held rather than pressed.
  void navigateUp(bool repeat = false);
  void navigateDown(bool repeat = false);
  void navigateSelect();

  //! Restart the Teensy. Does not return. See the note on it.
  void reboot();
  void navigateLeft();
  void navigateRight();
  void processState();
  void intro();
  // Draws the top level menu, with the firmware version bottom right.
  void drawMenuScreen();
  //! Repaint only the two entries whose colour changed. See the note on it.
  void drawMenuSelection(XCopyMenuItem *previous);
  //! The version string, bottom right.
  void drawVersion();
  //! Work out where it starts. Destructive, and a no-op after the first call.
  void measureVersion();
  //! The row it sits on, which is also the last menu row of a long level.
  uint8_t versionRow();
  //! Not yet measured. See measureVersion().
  static const uint8_t kVersionXUnknown = 0xff;
  uint8_t _versionX = kVersionXUnknown;
  void cancelOperation();
  bool detectCancelPin();
  void setBusy(bool busy);
  bool getBusy() { return digitalRead(PIN_BUSYPIN); }
  void refreshTimeNtp();
  void startFunction(XCopyAction action, String param = "");
  void startCopyADFtoDisk(String path  = "");
  void sendFile(String path);
  void getFile(String path, size_t size, bool overwrite = false);
  void processKeys(String keys);
  // The single way out of a head calibration session, so no path can leave it
  // with the drive still spinning. See processState().
  void exitHeadCalibration();
  //! Raw keystroke sink while the calibration screen is up, registered with
  //! XCopyCommandLine so the USB console and the browser terminal both drive it.
  static void onHeadCalKey(void *obj, char key);
  void sendBlock(int block);
  void cardChange();
  XCopyDisk* getDisk() { return &_disk; }
  XCopyConfig* getConfig() { return _config; }
  static void onWebCommand(void* obj, const String command);
  XCopyState _xcopyState = menus;
  #ifdef XCOPY_DEBUG
  void ramReport();
  #endif
  String _searchText;

private:
  TFT_ST7735 *_tft;
  XCopyMenu _menu;
  XCopyCommandLine *_command;
  XCopyAudio _audio;
  XCopyFloppy _floppy;
  XCopyDisk _disk;
  XCopyDirectory _directory;
  XCopyGraphics _graphics;
  XCopyConfig *_config;
  XCopyESP8266 *_esp = nullptr;
  XCopyTransfer _transfer;
  /*
     A value member rather than something new'd per session. It holds live drive
     state, and the feature it replaces was deleted while its interrupts were
     still armed - a lifetime this object simply does not have.
  */
  XCopyHeadCalibration _headCal;

#ifdef XCOPY_DEBUG
  RamMonitor _ram;
  uint32_t _lastRam = 0;
#endif
  XCopyMenuItem *verifyMenuItem;
  XCopyMenuItem *retryCountMenuItem;
  XCopyMenuItem *volumeMenuItem;
  XCopyMenuItem *ssidMenuItem;
  XCopyMenuItem *passwordMenuItem;
  XCopyMenuItem *diskDelayMenuItem;
  XCopyMenuItem *timeZoneMenuItem;
  XCopyMenuItem *scpRevolutionsMenuItem;
  XCopyMenuItem *scpCylindersMenuItem;

  // Destination for the next disk to ADF read. Empty means diskToADF() picks the
  // name from the disk label and the clock.
  String _adfFilePath;

  // Same for the next disk to SCP capture, plus the range and revolution count the
  // CLI may override for one run. Zero cylinders means "use the configured range".
  String _scpFilePath;
  uint8_t _scpStartCylinder = 0;
  uint8_t _scpEndCylinder = 0;
  uint8_t _scpRevolutions = 0;
  // Separate from the values because "0-0" - capture cylinder 0 alone - is a
  // legitimate range that a zero end cylinder cannot be told apart from.
  bool _scpRangeGiven = false;

  // Cylinder the next calibration session opens on, carried from the console or
  // the web command the same way _adfFilePath is.
  uint8_t _headCalCylinder = XCopyHeadCalibration::kDefaultCylinder;

  bool _drawnOnce = false;
  // True while the passthrough loop is servicing ESP *programming* mode, where
  // Serial1 runs at ESPProgBaudRate and the ESP is in its ROM bootloader.
  bool _espProgMode = false;
  // Set from ISR_CANCEL (XCopyStandalone.ino) and polled by processState()'s
  // passthrough loop. Without volatile the compiler is free to hoist the load out
  // of that loop, and passthrough mode can then never be exited.
  volatile bool _cancelOperation = false;
  int _prevSeconds = -1;
  // Set from ISR_CARD_DETECT, consumed in update().
  volatile bool _playCardSound = false;
  volatile uint32_t _cardChangeMs = 0;
};

#endif // XCOPY_H