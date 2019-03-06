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

  void setVerify(bool value);
  void setRetryCount(int value);

private:
  String _config;
  bool _verify = false;
  uint8_t _retryCount = 0;
};

#endif // XCOPYCONFIG_H