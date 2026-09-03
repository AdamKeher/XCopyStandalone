#include "XCopyMenu.h"

int XCopyMenuItem::getLevel()
{
    int level = 0;
    XCopyMenuItem *item = this->parent;
    while (item != NULL)
    {
        level++;
        item = item->parent;
    }
    return level;
}

XCopyMenu::XCopyMenu()
{
    // _items = 0;
}

void XCopyMenu::begin(XCopyGraphics *graphics)
{
    _graphics = graphics;
}

bool XCopyMenu::up()
{
    if (_currentItem->prev == NULL)
        return false;

    _currentItem = _currentItem->prev;

    if (_currentItem->text == "")
        up();

    return true;
}

bool XCopyMenu::down()
{
    if (_currentItem->next == NULL)
        return false;

    _currentItem = _currentItem->next;

    if (_currentItem->text == "")
        down();

    return true;
}

bool XCopyMenu::back()
{
    if (_currentItem->parent != NULL)
    {
        // setCurrentItem() moves _root to the parent's level with it.
        setCurrentItem(_currentItem->parent);
        return true;
    }

    return false;
}

XCopyMenuItem *XCopyMenu::addItem(String name, XCopyAction action, XCopyMenuItem *root)
{
    XCopyMenuItem *item = new XCopyMenuItem();

    if (root == NULL)
        root = getRoot();

    item->text = name;
    item->action = action;

    if (root != NULL)
    {
        XCopyMenuItem *last = getLast(root);
        item->prev = last;
        last->next = item;
    }

    if (_root == NULL)
    {
        _root = item;
        _currentItem = item;
    }

    return item;
}

XCopyMenuItem *XCopyMenu::addChild(String name, XCopyAction action, XCopyMenuItem *parent)
{
    XCopyMenuItem *item = new XCopyMenuItem();

    item->text = name;
    item->action = action;
    item->parent = parent;

    if (parent->firstChild != NULL)
    {
        XCopyMenuItem *last = getLast(parent->firstChild);
        item->prev = last;
        last->next = item;
    }
    else
        parent->firstChild = item;

    return item;
}

XCopyMenuItem *XCopyMenu::getFirst(XCopyMenuItem *item)
{
    if (item == NULL)
        return NULL;

    while (item->prev != NULL)
        item = item->prev;
    return item;
}

XCopyMenuItem *XCopyMenu::getLast(XCopyMenuItem *item)
{
    if (item == NULL)
        return NULL;

    while (item->next != NULL)
        item = item->next;
    return item;
}

/*
   One entry, at the row drawMenu() would have put it on.

   Deliberately identical to the body of drawMenu()'s loop rather than similar to
   it: the two have to lay down the same pixels or a repainted row would sit a
   character to the side of the one it replaced. Nothing is cleared first - the
   string is the same string, only its colour changes, so overprinting it lands on
   exactly the pixels it is replacing.
*/
void XCopyMenu::drawItem(XCopyMenuItem *item, uint8_t row)
{
    _graphics->setCursor(ROW_X, ROW_TOP + (row * ROW_HEIGHT));
    if (item->firstChild != NULL)
    {
        _graphics->drawText(ST7735_YELLOW, ">> ");
    }

    uint16_t color = isCurrentItem(item) ? ST7735_GREEN : ST7735_WHITE;
    _graphics->setTextWrap(false);
    _graphics->drawText(color, item->text);
}

void XCopyMenu::drawMenu(XCopyMenuItem *item)
{
    uint8_t count = 0;
    while (item != NULL)
    {
        drawItem(item, count);
        item = item->next;
        count++;
    }
}

int16_t XCopyMenu::rowOf(XCopyMenuItem *item)
{
    int16_t row = 0;
    for (XCopyMenuItem *walk = _root; walk != NULL; walk = walk->next, row++)
        if (walk == item)
            return row;

    return -1;
}

bool XCopyMenu::redrawSelection(XCopyMenuItem *previous)
{
    if (previous == NULL || _currentItem == NULL)
        return false;

    const int16_t previousRow = rowOf(previous);
    const int16_t currentRow = rowOf(_currentItem);
    if (previousRow < 0 || currentRow < 0)
        return false;

    // The outgoing entry first, so a level of one - where the two are the same
    // entry - still ends up drawn as the current one.
    if (previous != _currentItem)
        drawItem(previous, (uint8_t)previousRow);
    drawItem(_currentItem, (uint8_t)currentRow);

    return true;
}

void XCopyMenu::printItem(XCopyMenuItem *item)
{
    Serial << "{\r\n";
    Serial << "        Item: " << item->text << "\r\n";
    Serial << "      Action: " << (int)item->action << "\r\n";
    Serial << "       Level: " << item->getLevel() << "\r\n";
    Serial << "        Prev: " << (item->prev == NULL ? "NULL" : item->prev->text) << "\r\n";
    Serial << "        Next: " << (item->next == NULL ? "NULL" : item->next->text) << "\r\n";
    Serial << "      Parent: " << (item->parent == NULL ? "NULL" : item->parent->text) << "\r\n";
    Serial << "  FirstChild: " << (item->firstChild == NULL ? "NULL" : item->firstChild->text) << "\r\n";
    Serial << "}\r\n";
}

void XCopyMenu::printItems(XCopyMenuItem *item)
{
    while (item != NULL)
    {
        printItem(item);

        if (item->firstChild != NULL)
        {
            printItems(item->firstChild);
        }

        item = item->next;
    }
}

/*
   The one place _root moves with _currentItem.

   _root is the level being drawn and _currentItem is the highlighted entry in it, so
   the second has to be inside the first or drawMenu() paints a list that contains no
   current item and highlights nothing. Every navigation path used to maintain that by
   hand and setCurrentItem(XCopyAction) did not, which is how a web started action left
   the cursor down in a submenu while the screen showed the top level: audio on every
   keypress, no highlight anywhere, and select firing whatever the invisible cursor had
   landed on.
*/
void XCopyMenu::setCurrentItem(XCopyMenuItem *item) {
    if (item == NULL)
        return;

    _currentItem = item;
    _root = getFirst(item);
}

void XCopyMenu::setCurrentItem(XCopyAction action) {
    // From the top of the tree, not from _root. _root is only the level on screen, so
    // searching it finds nothing whenever the action lives in a different branch to
    // the one being displayed - a console or web command issued while the user sat in
    // some other submenu silently failed to move the cursor at all.
    XCopyMenuItem *item = findItem(action, topItem());
    if (item != nullptr) {
        setCurrentItem(item);
    }
}

/*
   First item of the top level, found by walking up. There is no preserved pointer to
   it: addItem() seeds _root with it, but _root is moved every time the user descends
   or backs out.
*/
XCopyMenuItem *XCopyMenu::topItem()
{
    XCopyMenuItem *item = (_currentItem != NULL) ? _currentItem : _root;

    if (item == NULL)
        return NULL;

    while (item->parent != NULL)
        item = item->parent;

    return getFirst(item);
}

XCopyMenuItem* XCopyMenu::findItem(XCopyAction action, XCopyMenuItem *item) {
    XCopyMenuItem* found = nullptr;

    while (item != NULL)
    {
        if (item->action == action) {
            found = item;
            break;
        }

        if (item->firstChild != NULL)
        {
            found = findItem(action, item->firstChild);
            if (found != nullptr) break;
        }

        item = item->next;
    }

    return found;
}