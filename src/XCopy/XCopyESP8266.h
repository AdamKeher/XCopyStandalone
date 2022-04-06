#ifndef XCOPYESP8266_H
#define XCOPYESP8266_H

#include <Arduino.h>
#include <Streaming.h>
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
  void sendWebSocket(String command);


  void setMode(String text) { sendWebSocket("setMode," + text); }
  void setStatus(String text) { sendWebSocket("setStatus," + text); }
  void setDiskName(String name) { sendWebSocket("setDiskname," + name); }
  void setTrack(int trackNumber, String color) { sendWebSocket("setTrack," + String(trackNumber) + "," + color); }
  void resetDisk() { sendWebSocket("resetDisk"); }
  void setState(XCopyState state) { sendWebSocket("setState," + String(state)); }
  void log(String text) { sendWebSocket("log," + text); };
  bool updateWebSdCardFiles(String directory);

  String Version();
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
  const String _marker = "xcopyCommand,";
  
  void* _caller;
  OnWebCommand _callback;

  int _espResetPin;
  int _espProgPin;
};

#endif // XCOPYESP8266_H