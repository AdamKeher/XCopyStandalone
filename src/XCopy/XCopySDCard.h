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
#include "XCopySdFat.h"

class XCopySDCard
{
  public:
    bool begin();
    bool cardDetect();
    bool printDirectory(String directory, bool color = true);
    
    bool open(String directory);
    bool open() { return open(_directory); }
    bool next();
    // By const reference. Returning by value deep copied three Strings on every
    // call, and the directory loops call it six times per file. The reference is
    // only valid until the next next() call, which is all any caller needs.
    const XCopyFile &getfile() { return _xfile; }
    String getError() { return _error; }
    bool fileExists(String path) { return xcopySd().exists(path.c_str()); }
    bool deleteFile(String path) { return xcopySd().remove(path.c_str()); }
    bool makeDirectory(String path) { return xcopySd().mkdir(path.c_str()); }

    // This class does not own the card; it is a cursor over one directory of the
    // single shared SdFat. See XCopySdFat.h for why there is only one.
    SdFat &getSdFat() { return xcopySd(); }

    // TODO: These functions have a maxItems parameter as they is not currently enough memory
    //       change them to some sort of getNext() type arrangement
    GenericList<String> *getFiles(String directory, int maxItems);
    
  private:
    String _error;
    String _directory;
    SdFile _root;
    SdFile _file;
    XCopyFile _xfile;
};


#endif // XCOPYSDCARD_H