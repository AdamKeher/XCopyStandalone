#ifndef XCOPYSDCARD_H
#define XCOPYSDCARD_H

#include <Arduino.h>
#include <Streaming.h>
#include <SdFat.h>
#include <sdios.h>
#include "GenericList.h"
#include "XCopyPins.h"
#include "XCopyLog.h"
#include "XCopyConsole.h"
#include "XCopyFile.h"

class XCopySDCard
{
  public:
    bool begin();
    bool cardDetect();
    bool printDirectory(String directory, bool color = true);
    // GenericList<XCopyFile> *getFiles(String directory);
    void getFiles(String directory);
  private:
    SdFat _sd;
};


#endif // XCOPYSDCARD_H