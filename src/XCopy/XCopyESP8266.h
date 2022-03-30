#ifndef XCOPYESP8266_H
#define XCOPYESP8266_H

#include <Arduino.h>
#include <Streaming.h>

typedef void (*espCallbackFunction)(const String command);

class XCopyESP8266
{
public:
  XCopyESP8266(uint32_t baudrate, int espResetPin, int espProgPin);
  bool begin();
  bool connect(String ssid, String password, uint32_t timeout);
  String sendCommand(String command, bool strip = false, int timeout = 250);
  void sendWebSocket(String command);
  String Version();
  void setEcho(bool status);
  void Update();
  void reset();
  void progMode();
  time_t getTime();

  void setCallBack(espCallbackFunction function);

private:
  char OK_EOC[5] = "OK\r\n";
  char ER_EOC[5] = "ER\r\n";
  // HardwareSerial _serial;
  String _command;
  const String _marker = "xcopyCommand,";
  espCallbackFunction _espCallBack;
  int _espResetPin;
  int _espProgPin;
};

#endif // XCOPYESP8266_H