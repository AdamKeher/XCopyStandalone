#ifndef XCOPYDIRECTORY_H
#define XCOPYDIRECTORY_H

#include <Arduino.h>
#include <SerialFlash.h>
#include <Streaming.h>
#include "XCopyDisk.h"
#include "XCopyGraphics.h"
#include "XCopySDCard.h"
#include "XCopyPins.h"

#define ITEMSPERSCREEN 12

class XCopyDirectoryEntry {
public:
  XCopyDirectoryEntry();

  bool isDirectory() { return _isDirectory; }
  void setIsDirectory(bool value) { _isDirectory = value; }
  bool isIncorrectSize = false;
  String longName;
  ADFFileSource source;
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

  void begin(XCopyGraphics *graphics, XCopyDisk *disk);
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

  /*
     Row geometry, shared with drawItem() so a single row repaint lands exactly
     where drawDirectory() put it.
  */
  static const uint8_t ROW_TOP = 0;
  static const uint8_t ROW_HEIGHT = 10;
  static const uint8_t ROW_X = 5;

  /*
     The listing scrolls, so what is on screen is a window onto the list rather
     than the whole of it. The window is pinned to the bottom: the cursor sits on
     the last visible row until it reaches the end of the list.
  */
  uint16_t windowTop() const {
    return (_index + 1 >= ITEMSPERSCREEN) ? (uint16_t)(_index + 1 - ITEMSPERSCREEN) : 0;
  }

  //! The whole window. Always clears first - every caller wants a clean screen.
  void drawDirectory();
  /*
     One entry, drawn exactly as drawDirectory() draws it. Returns true if it
     painted a full screen cover image over the listing, which is the one thing a
     single row repaint cannot undo.
  */
  bool drawItem(XCopyDirectoryEntry *item, uint8_t row);

  /**
   * @brief Repaint only the two entries whose colour changed.
   *
   * Returns false when that is not enough and the caller has to draw the whole
   * window: the list scrolled under the cursor, or one of the two entries lives
   * in flash and may have painted a full screen thumbnail over the listing.
   *
   * @param previousTop windowTop() from before the cursor moved.
   */
  bool redrawSelection(XCopyDirectoryEntry *previous, uint16_t previousTop);

private:
  // fix: change name from root to head
  XCopyDirectoryEntry *_root;
  XCopyDirectoryEntry *_currentItem;
  uint16_t _index;
  String _currentPath;
  XCopyGraphics *_graphics;
  XCopyDisk *_disk;
};

#endif // XCOPYDIRECTORY_H