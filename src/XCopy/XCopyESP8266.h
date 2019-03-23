#ifndef XCOPYESP8266_H
#define XCOPYESP8266_H

#include <Arduino.h>
#include <Streaming.h>

class XCopyESP8266
{
public:
  XCopyESP8266(HardwareSerial serial, uint32_t baudrate);
  bool begin();
  bool connect(String ssid, String password, uint32_t timeout);
  String sendCommand(String command, bool strip = false, int timeout = 250);
  void sendWebSocket(String command);
  String Version();
  void setEcho(bool status);

private:
  char OK_EOC[5] = "OK\r\n";
  char ER_EOC[5] = "ER\r\n";
  HardwareSerial _serial;
};

#endif // XCOPYESP8266_H