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

  /*
     Sets the highlighted item AND the level drawn around it. _root must always be the
     head of the level _currentItem sits in, or drawMenu() paints a list with nothing
     highlighted in it.

     There is deliberately no setRoot(). Moving the two independently is what let the
     level on screen and the cursor in it drift apart; keeping one entry point makes
     that impossible rather than merely discouraged.
  */
  void setCurrentItem(XCopyMenuItem *item);
  void setCurrentItem(XCopyAction action);
  XCopyMenuItem *findItem(XCopyAction action) { return findItem(action, topItem()); };
  XCopyMenuItem *findItem(XCopyAction action, XCopyMenuItem *item);

  //! First item of the top level. Not _root, which is only the level on screen.
  XCopyMenuItem *topItem();

  bool isCurrentItem(XCopyMenuItem *item) { return item == _currentItem; }

  void printItem(XCopyMenuItem *item);
  void printItems(XCopyMenuItem *item);
  void printCurrentItem() { printItem(_currentItem); }
  void printAll() { printItems(_root); }

  /*
     Row geometry. The list starts below the header logo and every entry is one
     text line tall. Shared with drawItem() so a single row repaint lands exactly
     where drawMenu() put it - a targeted repaint that is one pixel out is worse
     than no targeted repaint at all.
  */
  static const uint8_t ROW_TOP = 45;
  static const uint8_t ROW_HEIGHT = 10;
  static const uint8_t ROW_X = 5;

  void drawMenu(XCopyMenuItem *item);
  //! One entry, drawn exactly as drawMenu() draws it.
  void drawItem(XCopyMenuItem *item, uint8_t row);
  //! Which row of the level on screen an item sits on, or -1 if it is not in it.
  int16_t rowOf(XCopyMenuItem *item);

  /**
   * @brief Repaint only the two entries whose colour changed.
   *
   * Moving the cursor changes two pixels' worth of meaning and nothing else, but
   * the whole level was being reprinted for it - every string blitted over an
   * identical copy of itself, down a 15MHz SPI bus, on every click of the stick.
   *
   * Returns false if either entry is not in the level being drawn, which is the
   * caller's cue to draw the whole thing: descending, backing out and any jump
   * from the console or the browser all change the list itself, not the cursor
   * in it.
   */
  bool redrawSelection(XCopyMenuItem *previous);
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