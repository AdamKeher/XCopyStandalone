#ifndef XCOPYFILE_H
#define XCOPYFILE_H

#include "Arduino.h"

class XCopyFile
{
  public:
    XCopyFile() {}
    XCopyFile(XCopyFile *item) { 
      filename = item->filename;
      date = item->date;
      time = item->time;
      size = item->size;
      isADF = item->isADF;
      isDirectory = item->isDirectory;
    }

    String filename = "";
    String date = "";
    String time = "";
    unsigned long size = 0;
    bool isADF = false;
    bool isDirectory = false;
};

#endif // XCOPYFILE
