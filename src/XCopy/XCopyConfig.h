#ifndef XCOPYCONFIG_H
#define XCOPYCONFIG_H

#define configFilename "CONFIG.TXT"
#include <Arduino.h>
#include <SerialFlash.h>
#include <ArduinoJson.h>
#include <Streaming.h>
#include "XCopyLog.h"

class XCopyConfig
{
public:
  XCopyConfig(bool readConfig = true);
  void createConfig();
  bool readConfig();
  bool writeConfig();
  void parseConfig();
  String getConfig() { return _config; }
  void dumpConfig() { Log << "Config: '" + _config + "'\r\n"; }

  bool getVerify() { return _verify; }
  uint8_t getRetryCount() { return _retryCount; }
  float getVolume() { return _volume; }
  String getSSID() { return _ssid; }
  String getPassword() { return _password; }
  uint16_t getDiskDelay() { return _diskDelay; }
  int getTimeZone() { return _timeZone; }
  uint8_t getScpRevolutions() { return _scpRevolutions; }
  uint8_t getScpEndCylinder() { return _scpEndCylinder; }

  void setVerify(bool value);
  void setRetryCount(int value);
  void setVolume(float value);
  void setSSID(String value);
  void setPassword(String value);
  void setDiskDelay(uint16_t delayMs);
  void setTimeZone(int timeZone);
  void setScpRevolutions(uint8_t revolutions);
  void setScpEndCylinder(uint8_t cylinder);

private:
  String _config;
  bool _verify = false;
  uint8_t _retryCount = 0;
  float _volume = 0.8;
  String _ssid;
  String _password;
  uint16_t _diskDelay = 200;
  int _timeZone = 0;
  // Three revolutions is the preservation standard: enough to tell a weak bit from a
  // clean one, and to pick a good revolution when they disagree.
  uint8_t _scpRevolutions = 3;
  // Last cylinder an SCP capture reads. AmigaDOS ends at 79; the cylinders past it
  // are where long track and out of band protections live, but not every drive can
  // reach them, so going further is opt in.
  uint8_t _scpEndCylinder = 79;
};

#endif // XCOPYCONFIG_H