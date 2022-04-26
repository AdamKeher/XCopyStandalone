#ifndef XCOPYADFLIB_H
#define XCOPYADFLIB_H

#include <Arduino.h>
#include <Streaming.h>
#include "../adflib/adflib.h"
#include "../adflib/adf_nativ.h"

class XCopyADFLib
{
public:
  void begin(uint8_t sdCSPin);
  Device *mount(char *filename);
  Volume *openVolume(Device *device);
  String printDevice(Device *device);
  String printVolume(Volume *volume);
  String printDirectory(Volume *volume);

  void adfDump();
  void unmount();

  Device *getDevice() { return _dev; }
  Volume *getVolume() { return _vol; }
  String getFilename() { return _filename; }

private:
  String printEntry(struct Volume *vol, struct Entry *entry, char *path, bool sect, bool comment);

  uint8_t _sdCSPin;
  struct Device *_dev;
  struct Volume *_vol;
  String _filename;
};

#endif // XCOPYADFLIB_H