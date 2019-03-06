#include "XCopyTime.h"

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

void XCopyTime::setTime()
{
  setSyncProvider(getTeensy3Time);
}