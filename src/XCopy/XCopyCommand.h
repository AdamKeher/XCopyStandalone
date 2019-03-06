#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "FloppyDrive.h"

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  void Update();

private:
  String _command;
  String _version;
};

#endif // XCOPYCOMMAND