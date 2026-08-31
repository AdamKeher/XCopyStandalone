#ifndef XCOPYMENU_H
#define XCOPYMENU_H
#include <Arduino.h>
#include <Streaming.h>
#include "XCopyAction.h"
#include "XCopyGraphics.h"

class XCopyMenuItem
{
public:
  String text;
  struct XCopyMenuItem *prev;
  struct XCopyMenuItem *next;
  struct XCopyMenuItem *parent;
  struct XCopyMenuItem *firstChild;
  // What selecting this item asks for. Headings and spacers carry
  // XCopyAction::none.
  XCopyAction action;

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

  XCopyMenuItem *addItem(String name, XCopyAction action, XCopyMenuItem *root = NULL);
  XCopyMenuItem *addChild(String name, XCopyAction action, XCopyMenuItem *parent);

  XCopyMenuItem *getFirst(XCopyMenuItem *item);
  XCopyMenuItem *getLast(XCopyMenuItem *item);
  XCopyMenuItem *getRoot() { return _root; }
  XCopyMenuItem *getCurrentItem() { return _currentItem; }

  void setRoot(XCopyMenuItem *item) { _root = item; }
  void setCurrentItem(XCopyMenuItem *item);
  void setCurrentItem(XCopyAction action);
  XCopyMenuItem *findItem(XCopyAction action) { return findItem(action, _root); };
  XCopyMenuItem *findItem(XCopyAction action, XCopyMenuItem *item);

  bool isCurrentItem(XCopyMenuItem *item) { return item == _currentItem; }

  void printItem(XCopyMenuItem *item);
  void printItems(XCopyMenuItem *item);
  void printCurrentItem() { printItem(_currentItem); }
  void printAll() { printItems(_root); }

  void drawMenu(XCopyMenuItem *item);
  void redraw() {
    _graphics->clearScreen();
    _graphics->drawHeader();
    XCopyMenuItem *item = _currentItem;
    if (item->parent != nullptr) {
      item = item->parent->firstChild;
    }
    drawMenu(item);
  };

private:
  XCopyGraphics *_graphics;
  XCopyMenuItem *_root;
  XCopyMenuItem *_currentItem;
};

#endif // XCOPYMENU_H