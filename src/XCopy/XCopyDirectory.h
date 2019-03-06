#ifndef XCOPYDIRECTORY_H
#define XCOPYDIRECTORY_H

#include <Arduino.h>
#include <SerialFlash.h>
#include <SD.h>
#include <Streaming.h>
#include "XCopyDisk.h"
#include "XCopyGraphics.h"

#define ITEMSPERSCREEN 13

enum XCopyDirectoryEntrySource
{
  flashMemory = 0,
  sdCard = 1
};

class XCopyDirectoryEntry
{
public:
  XCopyDirectoryEntry();

  bool isDirectory() { return _isDirectory; }
  void setIsDirectory(bool value) { _isDirectory = value; }
  String name;
  unsigned long size;
  String path;
  String volumeName;
  XCopyDirectoryEntrySource source;

  struct XCopyDirectoryEntry *prev;
  struct XCopyDirectoryEntry *next;

private:
  bool _isDirectory;
};

class XCopyDirectory
{
public:
  XCopyDirectory();

  bool down();
  bool up();

  void begin(XCopyGraphics *graphics, XCopyDisk *disk, uint8_t sdCSPin, uint8_t flashPin);
  void clear();
  void getDirectoryFlash(bool root, XCopyDisk *disk, String filter = "");
  void getDirectory(String path, XCopyDisk *disk, String filter = "", bool dirAtTop = true);

  XCopyDirectoryEntry *addItem(XCopyDirectoryEntry *item, XCopyDirectoryEntry *root = NULL);
  XCopyDirectoryEntry *addRoot(XCopyDirectoryEntry *item);
  XCopyDirectoryEntry *moveToRoot(XCopyDirectoryEntry *item);
  XCopyDirectoryEntry *moveItemBefore(XCopyDirectoryEntry *item, XCopyDirectoryEntry *before);
  XCopyDirectoryEntry *removeItem(XCopyDirectoryEntry *item);
  XCopyDirectoryEntry *insertItemBefore(XCopyDirectoryEntry *item, XCopyDirectoryEntry *before);

  XCopyDirectoryEntry *getFirst(XCopyDirectoryEntry *item);
  XCopyDirectoryEntry *getLast(XCopyDirectoryEntry *item);
  XCopyDirectoryEntry *getRoot() { return _root; }
  XCopyDirectoryEntry *getCurrentItem() { return _currentItem; }
  uint16_t getItemIndex(XCopyDirectoryEntry *item);
  uint16_t getIndex() { return _index; }

  String getCurrentPath() { return _currentPath; }

  void setRoot(XCopyDirectoryEntry *item) { _root = item; }
  void setCurrentItem(XCopyDirectoryEntry *item) { _currentItem = item; }
  void setIndex(uint16_t value) { _index = value; }

  bool isCurrentItem(XCopyDirectoryEntry *item) { return item == _currentItem; }

  void printItem(XCopyDirectoryEntry *item);
  void printItems(XCopyDirectoryEntry *item);

  void drawDirectory(bool clearScreen = false);

private:
  // fix: change name from root to head
  XCopyDirectoryEntry *_root;
  XCopyDirectoryEntry *_currentItem;
  uint16_t _index;
  String _currentPath;

  uint8_t _sdCSPin;
  uint8_t _flashCSPin;

  XCopyGraphics *_graphics;
  XCopyDisk *_disk;
};

#endif // XCOPYDIRECTORY_H