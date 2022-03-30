#ifndef XCOPYTIME_H
#define XCOPYTIME_H

#include <TimeLib.h>
#include <Arduino.h>

class XCopyTime
{
public:
  static void syncTime(bool enable = true);
  static void setTime(long epoch);
  static time_t getTime();
private:

};

#endif // XCOPYTIME_H