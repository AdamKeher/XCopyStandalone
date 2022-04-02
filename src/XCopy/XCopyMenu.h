#ifndef XCOPYMENU_H
#define XCOPYMENU_H
#include <Arduino.h>
#include <Streaming.h>
#include "XCopyState.h"
#include "XCopyGraphics.h"

class XCopyMenuItem
{
public:
  String text;
  struct XCopyMenuItem *prev;
  struct XCopyMenuItem *next;
  struct XCopyMenuItem *parent;
  struct XCopyMenuItem *firstChild;
  XCopyState command;

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

  XCopyMenuItem *addItem(String name, XCopyState command, XCopyMenuItem *root = NULL);
  XCopyMenuItem *addChild(String name, XCopyState command, XCopyMenuItem *parent);

  XCopyMenuItem *getFirst(XCopyMenuItem *item);
  XCopyMenuItem *getLast(XCopyMenuItem *item);
  XCopyMenuItem *getRoot() { return _root; }
  XCopyMenuItem *getCurrentItem() { return _currentItem; }

  void setRoot(XCopyMenuItem *item) { _root = item; }
  void setCurrentItem(XCopyMenuItem *item);
  void setCurrentItem(XCopyState command);
  XCopyMenuItem *findItem(XCopyState command) { return findItem(command, _root); };
  XCopyMenuItem *findItem(XCopyState command, XCopyMenuItem *item);

  bool isCurrentItem(XCopyMenuItem *item) { return item == _currentItem; }

  void printItem(XCopyMenuItem *item);
  void printItems(XCopyMenuItem *item);
  void printCurrentItem() { printItem(_currentItem); }
  void printAll() { printItems(_root); }

  void drawMenu(XCopyMenuItem *item);
  void redraw() {
    _graphics->clearScreen();
    _graphics->drawHeader();
    drawMenu(_currentItem);
  };

private:
  XCopyGraphics *_graphics;
  XCopyMenuItem *_root;
  XCopyMenuItem *_currentItem;
};

#endif // XCOPYMENU_H