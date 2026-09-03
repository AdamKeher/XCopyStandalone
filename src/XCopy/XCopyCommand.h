#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyFloppy.h"
#include "GenericList.h"
#include "XCopyPins.h"
#include "XCopyLog.h"
#include "XCopyADFLib.h"
#include "XCopyESP8266.h"
#include "XCopyConfig.h"
#include "XCopyTime.h"
#include "XCopySDCard.h"
#include "XCopyConsole.h"
#include "XCopyDisk.h"
#include "XCopyBrainFile.h"
#include <TimeLib.h>
#include <MD5.h>

typedef void (*OnWebCommand)(void* obj, const String command);

/*
   A sink for single keystrokes, for a screen that is driven by keys rather than
   by typed lines. doCommand() only fires on Enter, which is no use to something
   the operator is adjusting live.
*/
typedef void (*OnRawKey)(void* caller, char key);

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config, XCopyDisk* disk, XCopyFloppy* floppy);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  bool printDirectory(String directory, bool color = true);
  void processKey(char key);
  void processKeys(String keys);
  void Update();

  void setCallBack(void* caller, OnWebCommand function);

  /**
   * @brief Divert every keystroke to @p function instead of the line editor.
   *
   * Both entry points funnel through processKey(): the USB console via Update()
   * and the browser terminal via processKeys(), so one hook serves both and a
   * key screen is drivable from either without knowing which it is talking to.
   *
   * @param function nullptr restores normal line editing.
   */
  void setRawKeys(void* caller, OnRawKey function);

private:
  String _command;
  String _version;
  XCopyESP8266 *_esp;
  XCopyConfig *_config;
  XCopyDisk *_disk;
  XCopyFloppy *_floppy;

  void* _caller;
  OnWebCommand _callback;

  void* _rawCaller = nullptr;
  OnRawKey _rawKeys = nullptr;
  void setBusy(bool state);
};

#endif // XCOPYCOMMAND