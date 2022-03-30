#include "XCopyTime.h"

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

void XCopyTime::syncTime(bool enable)
{
  if (enable) {
    setSyncProvider(getTeensy3Time);
  } else {
    setSyncProvider(nullptr);
  }
}

void XCopyTime::setTime(long epoch)
{
  Teensy3Clock.set(epoch);
}

time_t XCopyTime::getTime()
{
  return Teensy3Clock.get();
}