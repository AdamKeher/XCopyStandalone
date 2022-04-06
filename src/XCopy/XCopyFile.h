#ifndef XCOPYFILE_H
#define XCOPYFILE_H

#include "Arduino.h"

class XCopyFile
{
  public:
    XCopyFile() {}
 

    String filename = "";
    String date = "";
    String time = "";
    unsigned long size = 0;
    bool isADF = false;
    bool isDirectory = false;
};

#endif // XCOPYFILE
