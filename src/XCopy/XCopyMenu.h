#ifndef XCOPYMENU_H
#define XCOPYMENU_H
#include <Arduino.h>
#include <Streaming.h>
#include "XCopyGraphics.h"

class XCopyMenuItem
{
public:
  String text;
  struct XCopyMenuItem *prev;
  struct XCopyMenuItem *next;
  struct XCopyMenuItem *parent;
  struct XCopyMenuItem *firstChild;
  int command;

  int getLevel();
};

class XCopyMenu
{
public:
  XCopyMenu();
  void begin(XCopyGraphics *graphics);

  bool down();
  bool up();
  bool back();

  XCopyMenuItem *addItem(String name, int command, XCopyMenuItem *root = NULL);
  XCopyMenuItem *addChild(String name, int command, XCopyMenuItem *parent);

  XCopyMenuItem *getFirst(XCopyMenuItem *item);
  XCopyMenuItem *getLast(XCopyMenuItem *item);
  XCopyMenuItem *getRoot() { return _root; }
  XCopyMenuItem *getCurrentItem() { return _currentItem; }

  void setRoot(XCopyMenuItem *item) { _root = item; }
  void setCurrentItem(XCopyMenuItem *item) { _currentItem = item; }

  bool isCurrentItem(XCopyMenuItem *item) { return item == _currentItem; }

  void printItem(XCopyMenuItem *item);
  void printItems(XCopyMenuItem *item);

  void drawMenu(XCopyMenuItem *item);

private:
  XCopyGraphics *_graphics;
  XCopyMenuItem *_root;
  XCopyMenuItem *_currentItem;
};

#endif // XCOPYMENU_H