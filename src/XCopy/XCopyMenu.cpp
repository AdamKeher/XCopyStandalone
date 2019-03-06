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
        setRoot(getFirst(_currentItem->parent));
        setCurrentItem(_currentItem->parent);
        return true;
    }

    return false;
}

XCopyMenuItem *XCopyMenu::addItem(String name, int command, XCopyMenuItem *root)
{
    XCopyMenuItem *item = new XCopyMenuItem();

    if (root == NULL)
        root = getRoot();

    item->text = name;
    item->command = command;

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

XCopyMenuItem *XCopyMenu::addChild(String name, int command, XCopyMenuItem *parent)
{
    XCopyMenuItem *item = new XCopyMenuItem();

    item->text = name;
    item->command = command;
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

void XCopyMenu::drawMenu(XCopyMenuItem *item)
{
    uint8_t count = 0;
    while (item != NULL)
    {
        _graphics->setCursor(5, 45 + (count * 10));
        if (item->firstChild != NULL)
        {
            _graphics->drawText(ST7735_YELLOW, ">> ");
        }

        uint16_t color = isCurrentItem(item) ? ST7735_GREEN : ST7735_WHITE;
        _graphics->setTextWrap(false);
        _graphics->drawText(color, item->text);

        item = item->next;
        count++;
    }
}

void XCopyMenu::printItem(XCopyMenuItem *item)
{
    Serial << "{\n";
    Serial << "        Item: " << item->text << "\n";
    Serial << "       Level: " << item->getLevel() << "\n";
    Serial << "        Prev: " << (item->prev == NULL ? "NULL" : item->prev->text) << "\n";
    Serial << "        Next: " << (item->next == NULL ? "NULL" : item->next->text) << "\n";
    Serial << "      Parent: " << (item->parent == NULL ? "NULL" : item->parent->text) << "\n";
    Serial << "  FirstChild: " << (item->firstChild == NULL ? "NULL" : item->firstChild->text) << "\n";
    Serial << "}\n";
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
