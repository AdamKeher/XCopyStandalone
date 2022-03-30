#ifndef XCOPYDRIVETEST_H
#define XCOPYDRIVETEST_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "XCopyFloppy.h"
#include "FloppyDrive.h"
#include "XCopyESP8266.h"

class XCopyDriveTest
{
  public:
    XCopyDriveTest();
    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp);
    void draw();
    void update();
    void cancelOperation();
    void operationCancelled(uint8_t trackNum = -1);

  private:
    XCopyAudio *_audio;
    XCopyGraphics *_graphics;
    XCopyESP8266 *_esp;
    XCopyFloppy *_floppy;
    volatile bool _cancelOperation = false;
};

#endif // XCOPYDRIVETEST_H