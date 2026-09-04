#ifndef XCOPYESP8266_H
#define XCOPYESP8266_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyProtocol.h"
#include "XCopyState.h"
#include "XCopySDCard.h"

typedef void (*OnWebCommand)(void* obj, const String command);

class XCopyESP8266
{
public:
  XCopyESP8266(uint32_t baudrate, int espResetPin, int espProgPin);
  bool begin();
  bool connect(String ssid, String password, uint32_t timeout);
  String sendCommand(String command, bool strip = false, int timeout = 250);
  void sendWebSocket(const String &command);
  // By reference: taking this by value copied the whole string on every
  // call, and drawFlux() sends ~620 bytes per track through here.
  void print(const String &text) { Serial1.print(text); };
  // For callers that already hold a fixed buffer. The String overload would
  // construct and destroy a temporary around every one of them, and the head
  // calibration panel sends several a second for as long as it is open.
  void print(const char *text) { Serial1.print(text); };

  void setMode(const String &text) { sendWebSocket("setMode," + text); }
  void setStatus(const String &text) { sendWebSocket("setStatus," + text); }
  void setDiskName(const String &name) { sendWebSocket("setDiskname," + name); }
  void setTrack(int trackNumber, const String &color, const String &text = "") { 
    String command = "setTrack," + String(trackNumber) + "," + color;
    if (text != "") command += "," + text;
    sendWebSocket(command);
  }
  void resetDisk() { sendWebSocket("resetDisk"); }
  void setState(XCopyState state) { sendWebSocket("setState," + String(state)); }
  void log(const String &text) { sendWebSocket("log," + text); };
  bool updateWebSdCardFiles(String directory);
  void setTab(String tab) { sendWebSocket("setTab," + tab); }
  void highlightBlock(int track, int side, int sector, int count, boolean highlight) { sendWebSocket("highlightEmptyBlock," + String(track) + "," + String (side) + "," + String(sector) + "," + count + "," + (highlight ? "true" : "false")); }
  void clearHighlightedBlocks() { sendWebSocket("clearHighlightedBlocks"); }

  String Version();
  // Signal strength of the network joined, worded by the ESP. The thresholds and
  // the names for them are the radio's own knowledge, so this side asks rather
  // than holding a second opinion about what a given number means.
  String signal() { return sendCommand("rssi", true); }
  void setEcho(bool status);
  void Update();
  void reset();
  void progMode();
  time_t getTime();

  void setCallBack(void* caller, OnWebCommand function);

private:
  char OK_EOC[5] = "OK\r\n";
  char ER_EOC[5] = "ER\r\n";
  // HardwareSerial _serial;
  String _command;
  const String _marker = XCOPY_COMMAND_MARKER;
  
  void* _caller;
  OnWebCommand _callback;

  int _espResetPin;
  int _espProgPin;
};

#endif // XCOPYESP8266_H