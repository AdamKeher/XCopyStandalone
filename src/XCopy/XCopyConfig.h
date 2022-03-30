#ifndef XCOPYCONFIG_H
#define XCOPYCONFIG_H

#define configFilename "CONFIG.TXT"
#include <Arduino.h>
#include <SerialFlash.h>
#include <ArduinoJson.h>
#include <Streaming.h>

class XCopyConfig
{
public:
  XCopyConfig(bool readConfig = true);
  void createConfig();
  bool readConfig();
  bool writeConfig();
  void parseConfig();
  String getConfig() { return _config; }
  void dumpConfig() { Serial << "Config: '" << _config << "'\r\n"; }

  bool getVerify() { return _verify; }
  uint8_t getRetryCount() { return _retryCount; }
  float getVolume() { return _volume; }
  String getSSID() { return _ssid; }
  String getPassword() { return _password; }
  uint16_t getDiskDelay() { return _diskDelay; }
  int getTimeZone() { return _timeZone; }

  void setVerify(bool value);
  void setRetryCount(int value);
  void setVolume(float value);
  void setSSID(String value);
  void setPassword(String value);
  void setDiskDelay(uint16_t delayMs);
  void setTimeZone(int timeZone);

private:
  String _config;
  bool _verify = false;
  uint8_t _retryCount = 0;
  float _volume = 0.8;
  String _ssid;
  String _password;
  uint16_t _diskDelay = 200;
  int _timeZone = 0;
};

#endif // XCOPYCONFIG_H