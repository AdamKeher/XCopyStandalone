#ifndef XCOPYADFSDDRIVER_H
#define XCOPYADFSDDRIVER_H

extern "C"
{
#include "../adflib/adf_dev_driver.h"
}

/*
   An ADF image on the SD card, as an ADFlib device.

   This is upstream's "dump" driver with SdFat where stdio was. Upstream's version
   is vendored but not compiled here: it is built on FILE, which newlib will happily
   give us and which has no idea the card exists.

   Registered by XCopyAdf::begin() under the name "sd", so a caller can insist on it
   with adfDevOpenWithDriver("sd", path, mode). isDevice() also claims every name
   that is not obviously something else, so plain adfDevOpen() finds it too.

   What it replaces is worth writing down, because the shape of the old one is why
   the ADF support was unreliable. adf_nativ.cpp kept the open file's name in a
   single file scope char* - so mounting a second image silently retargeted the
   first - hardcoded 80/2/11 DD geometry whatever the file was, and had a
   myWriteSector() that printed its own name and returned RC_OK without writing
   anything. Here the file handle is per device in drvData, the geometry is read
   from the file's length, and writeSectors() writes.
*/
extern "C" const struct AdfDeviceDriver xcopyAdfSdDriver;

#endif // XCOPYADFSDDRIVER_H
