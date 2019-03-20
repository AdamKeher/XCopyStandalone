#ifndef ESPCOMMAND_H
#define ESPCOMMAND_H

#define ESPVersion "XCopyESP v0.1"

#include <Arduino.h>
#include <Streaming.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

class ESPCommandLine
{
public:
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  void Update();

private:
  String _command;
};

#endif // ESPCOMMAND