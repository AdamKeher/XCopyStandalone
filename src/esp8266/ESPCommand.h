#ifndef ESPCOMMAND_H
#define ESPCOMMAND_H

#define ESPVersion "XCopyESP v0.2"

#include <Arduino.h>
#include <Streaming.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>

// XCOPY_ESP_LINE_MAX -- how long a line Update() must be able to accumulate before
// it decides it is looking at something that is not a line at all.
#include "XCopyProtocol.h"

class ESPCommandLine
{
public:
  void begin(WebSocketsServer *webSocket);
  void doCommand(String command);
  String getCommand() { return _command; }
  void printPrompt();
  void Update();
  // Signal strength, out to the browser. sendWifiStatus() greets one client with
  // it; wifiUpdate() watches for it moving and is called from loop().
  void sendWifiStatus(uint8_t num);
  void wifiUpdate();

private:
  String _command;
  // Set when a line runs past XCOPY_ESP_LINE_MAX, cleared by the newline that ends
  // it. Without it the accumulator restarts mid line and the tail is run as a
  // command of its own.
  bool _overflow = false;
  bool _localecho = true;
  WebSocketsServer *_webSocket;
  const int cancelPin = 13;

  String wifiStatus();
  void broadcastWifiStatus();
  // What the browser was last told, so a radio that drifts a decibel at a time
  // does not redraw the pill for every wobble. See wifiUpdate().
  uint32_t _wifiSampledAt = 0;
  int32_t _wifiRssi = 0;
  uint8_t _wifiBars = 0;
  String _wifiSsid;

  char OK_EOC[5] = "OK\r\n";
  char ER_EOC[5] = "ER\r\n";
};

#endif // ESPCOMMAND_H