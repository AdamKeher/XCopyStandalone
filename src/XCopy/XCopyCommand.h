#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "FloppyDrive.h"
#include "XCopyADFLib.h"
#include "XCopyESP8266.h"
#include "XCopyConfig.h"

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version, XCopyADFLib *adfLib, XCopyESP8266 *esp, XCopyConfig *config);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  void Update();

private:
  String _command;
  String _version;
  XCopyADFLib *_adfLib;
  XCopyESP8266 *_esp;
  XCopyConfig *_config;
};

#endif // XCOPYCOMMAND