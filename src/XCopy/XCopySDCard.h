#ifndef XCOPYSDCARD_H
#define XCOPYSDCARD_H

#include <Arduino.h>
#include <Streaming.h>
#include <SdFat.h>
#include <sdios.h>
#include "XCopyPins.h"
#include "XCopyConsole.h"

class XCopySDCard
{
  public:
    bool begin();
    bool cardDetect();
    bool printDirectory(String directory, bool color = true);

  private:
    SdFat _sd;
};


#endif // XCOPYSDCARD_H