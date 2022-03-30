#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "FloppyDrive.h"
#include "XCopyPins.h"
#include "XCopyADFLib.h"
#include "XCopyESP8266.h"
#include "XCopyConfig.h"
#include "XCopyTime.h"
#include "XCopySDCard.h"
#include "XCopyConsole.h"
#include <TimeLib.h>

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  void Update();

private:
  String _command;
  String _version;
  XCopyESP8266 *_esp;
  XCopyConfig *_config;
};

#endif // XCOPYCOMMAND