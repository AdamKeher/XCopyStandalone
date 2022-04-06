#ifndef XCOPYSDCARD_H
#define XCOPYSDCARD_H

#include <Arduino.h>
#include <Streaming.h>
#include <SdFat.h>
#include <sdios.h>
#include "GenericList.h"
#include "XCopyPins.h"
#include "XCopyConsole.h"
#include "XCopyFile.h"

class XCopySDCard
{
  public:
    bool begin();
    bool cardDetect();
    bool printDirectory(String directory, bool color = true);
    
    bool open(String directory);
    bool open() { return open(_directory); }
    bool next();
    XCopyFile getfile() { return _xfile; }
    String getError() { return _error; }

    // TODO: These functions have a maxItems parameter as they is not currently enough memory
    //       change them to some sort of getNext() type arrangement
    GenericList<String> *getFiles(String directory, int maxItems);
    
  private:
    String _error;
    String _directory;
    SdFat _sd;
    SdFile _root;
    SdFile _file;
    XCopyFile _xfile;
};


#endif // XCOPYSDCARD_H