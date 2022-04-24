#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "FloppyDrive.h"
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
#include <TimeLib.h>
#include <MD5.h>

typedef void (*OnWebCommand)(void* obj, const String command);

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config, XCopyDisk* disk);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  bool printDirectory(String directory, bool color = true);
  void processKey(char key);
  void processKeys(String keys);
  void Update();

  void setCallBack(void* caller, OnWebCommand function);

private:
  String _command;
  String _version;
  XCopyESP8266 *_esp;
  XCopyConfig *_config;
  XCopyDisk *_disk;

  void* _caller;
  OnWebCommand _callback;
};

#endif // XCOPYCOMMAND