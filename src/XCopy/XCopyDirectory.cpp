#include "XCopyDirectory.h"

XCopyDirectory::XCopyDirectory()
{
}

void XCopyDirectory::begin(XCopyGraphics *graphics, XCopyDisk *disk)
{
    _graphics = graphics;
    _disk = disk;
}

void XCopyDirectory::clear()
{
    XCopyDirectoryEntry *item = _root;

    _root = NULL;
    _currentItem = NULL;
    _index = 0;

    while (item != NULL)
    {
        XCopyDirectoryEntry *itemToDelete = item;
        item = item->next;

        delete itemToDelete;
    }
}

bool XCopyDirectory::up()
{
    if (_currentItem == NULL || _currentItem->prev == NULL)
        return false;

    _currentItem = _currentItem->prev;
    _index--;

    return true;
}

bool XCopyDirectory::down()
{
    if (_currentItem == NULL || _currentItem->next == NULL)
        return false;

    _currentItem = _currentItem->next;
    _index++;

    return true;
}

void XCopyDirectory::getDirectoryFlash(bool root, XCopyDisk *disk, String filter)
{
    if (!SerialFlash.begin(PIN_FLASHCS)) {
        Serial << "\r\nError Accessing SPI Flash.\r\n";
        return;
    }

    if (root) {
        XCopyDirectoryEntry *defaultItems = new XCopyDirectoryEntry();
        defaultItems->setIsDirectory(true);
        defaultItems->source = _flashMemory;
        defaultItems->longName = "Built In ADF Files";
        addRoot(defaultItems);
    }
    else {
        clear();
        _currentPath = "/Built In ADF Files/";
        SerialFlash.opendir();
        while (1) {
            char filename[64];
            uint32_t filesize;

            if (SerialFlash.readdir(filename, sizeof(filename), filesize)) {
                if (String(filename).toUpperCase().endsWith(filter.toUpperCase())) {
                    XCopyDirectoryEntry *flashFile = new XCopyDirectoryEntry();
                    flashFile->setIsDirectory(false);
                    flashFile->longName = filename;
                    flashFile->source = _flashMemory;
                    flashFile = addItem(flashFile);
                }
            }
            // no more files
            else { break; }
        }
    }
}

void XCopyDirectory::getDirectory(String path, XCopyDisk *disk, String filter, bool dirAtTop)
{
    clear();
    _currentPath = path;

    XCopySDCard *_sdcard = new XCopySDCard();

    if (!_sdcard->cardDetect()) {
        Serial << "SD card missing\r\n";
        delete _sdcard;
        return;
    }

    if (!_sdcard->begin()) {
        Serial << "SD card initialisation failed\r\n";
        delete _sdcard;
        return;
    }


    SdFile root;
    bool result = root.open(path.c_str());
    if (!result) { 
        Serial << "Failed to open path: '" + path + "'\r\n";
        delete _sdcard;
        return;
    }

    while (true)
    {
        SdFile entry;
        if (!entry.openNext(&root, O_RDONLY)) break;

        char lfnBuffer[255];
        entry.getName(lfnBuffer, 255);

        if (entry.isDir() && String(lfnBuffer) != "System Volume Information") {
            XCopyDirectoryEntry *item = new XCopyDirectoryEntry();
            item->longName = lfnBuffer;
            item->setIsDirectory(true);
            item->source = _sdCard;
            addItem(item);
        } 
        else if ((String(lfnBuffer).toUpperCase().endsWith(filter.toUpperCase()) || filter == "")) {
            XCopyDirectoryEntry *item = new XCopyDirectoryEntry();
            item->longName = lfnBuffer;
            item->setIsDirectory(false);
            item->source = _sdCard;
            if (entry.fileSize() != 901120) item->isIncorrectSize = true;

            addItem(item);
        }
        entry.close();
    }

    // move directory entries to top in sorted order
    if (dirAtTop) {
        XCopyDirectoryEntry *item = getRoot();
        XCopyDirectoryEntry *before = NULL;
        while (item != NULL)
        {
            if (item->isDirectory())
            {
                XCopyDirectoryEntry *tempNext;
                tempNext = item->next;
                moveItemBefore(item, before == NULL ? getRoot() : before);
                before = item->next;
                item = tempNext;
                continue;
            }
            item = item->next;
        }
    }

    if (path == "/") {
        getDirectoryFlash(true, NULL);
    }

    delete _sdcard;
}

XCopyDirectoryEntry *XCopyDirectory::addRoot(XCopyDirectoryEntry *item)
{
    return insertItemBefore(item, getRoot());
}

XCopyDirectoryEntry *XCopyDirectory::addItem(XCopyDirectoryEntry *item, XCopyDirectoryEntry *root)
{
    if (root == NULL)
    {
        root = getRoot();
    }

    if (getRoot() == NULL)
    {
        // add as root if no items found
        _root = item;
        _currentItem = item;

        return item;
    }
    else
    {
        // compare items
        XCopyDirectoryEntry *compare = getFirst(root);
        bool result = false;
        while (compare != NULL)
        {
            result = item->longName < compare->longName;
            if (result)
                break;
            compare = compare->next;
        }

        if (result)
        {
            // insert item before last compared item
            item->prev = compare->prev;
            item->next = compare;
            if (compare->prev != NULL)
                compare->prev->next = item;
            compare->prev = item;

            if (item->prev == NULL)
            {
                _root = item;
                _currentItem = item;
            }

            return item;
        }

        // add item to end
        XCopyDirectoryEntry *last = getLast(root);
        item->prev = last;
        last->next = item;
    }

    return item;
}

XCopyDirectoryEntry *XCopyDirectory::moveToRoot(XCopyDirectoryEntry *item)
{
    if (item == getRoot())
        return item;

    if (item->prev != NULL || item->next != NULL)
        removeItem(item);

    return addRoot(item);
}

XCopyDirectoryEntry *XCopyDirectory::removeItem(XCopyDirectoryEntry *item)
{
    if (getRoot() == item) // item is root
    {
        item->next->prev = NULL;
        _root = item->next;
    }
    else if (getLast(item) == item) // item is last
    {
        item->prev->next = NULL;
    }
    else // item is in middle of list
    {
        item->prev->next = item->next;
        item->next->prev = item->prev;
    }

    item->next = NULL;
    item->prev = NULL;

    return item;
}

XCopyDirectoryEntry *XCopyDirectory::insertItemBefore(XCopyDirectoryEntry *item, XCopyDirectoryEntry *before)
{
    if (item == before)
        return item;

    item->prev = before == NULL ? NULL : before->prev;
    if (before != NULL)
        before->prev = item;
    item->next = before;
    if (item->prev != NULL)
    {
        item->prev->next = item;
    }
    else
    {
        _root = item;
        _currentItem = item;
    }

    return item;
}

XCopyDirectoryEntry *XCopyDirectory::moveItemBefore(XCopyDirectoryEntry *item, XCopyDirectoryEntry *before)
{
    if (item == before)
        return item;

    removeItem(item);
    return insertItemBefore(item, before);
}

// FIX doesnt need a parameter
XCopyDirectoryEntry *XCopyDirectory::getFirst(XCopyDirectoryEntry *item)
{
    if (item == NULL)
        return NULL;

    while (item->prev != NULL)
        item = item->prev;
    return item;
}

// FIX doesnt need a parameter
XCopyDirectoryEntry *XCopyDirectory::getLast(XCopyDirectoryEntry *item)
{
    if (item == NULL)
        return NULL;

    while (item->next != NULL)
        item = item->next;
    return item;
}

uint16_t XCopyDirectory::getItemIndex(XCopyDirectoryEntry *item)
{
    XCopyDirectoryEntry *temp = getFirst(item);

    uint16_t index = 0;

    while (temp->next != NULL)
    {
        if (temp == item)
            break;
        index++;
        temp = temp->next;
    }

    return index;
}

void XCopyDirectory::printItem(XCopyDirectoryEntry *item)
{
    Serial << "{\r\n";
    Serial << "        Item: " << item->longName << "\r\n";
    Serial << "   Directory: " << (item->isDirectory() ? "TRUE" : "FALSE") << "\r\n";
    Serial << "      Source: " << item->source << "\r\n";
    Serial << "        Prev: " << (item->prev == NULL ? "NULL" : item->prev->longName) << "\r\n";
    Serial << "        Next: " << (item->next == NULL ? "NULL" : item->next->longName) << "\r\n";
    Serial << "}\r\n";
}

void XCopyDirectory::printItems(XCopyDirectoryEntry *item)
{
    while (item != NULL)
    {
        printItem(item);
        item = item->next;
    }
}

/*
   One entry, at the row drawDirectory() would have put it on.

   Deliberately identical to the body of drawDirectory()'s loop rather than similar
   to it: the two have to lay down the same pixels, or a repainted row would sit a
   character to the side of the one it replaced. Nothing is cleared first - the
   string is the same string and only its colour changes, so overprinting it lands
   on exactly the pixels it is replacing.

   Returns whether the entry painted a full screen thumbnail over the listing, which
   is the one thing a caller cannot repair by repainting a single row.
*/
bool XCopyDirectory::drawItem(XCopyDirectoryEntry *item, uint8_t row)
{
    _graphics->setCursor(ROW_X, ROW_TOP + (row * ROW_HEIGHT));
    if (item->isDirectory())
    {
        if (item->source == _flashMemory)
            _graphics->drawText(ST7735_CYAN, ">> ");
        else
            _graphics->drawText(ST7735_YELLOW, ">> ");
    }

    uint16_t color = isCurrentItem(item) ? ST7735_GREEN : ST7735_WHITE;
    if (item->isIncorrectSize)
        color = ST7735_RED;

    _graphics->setTextWrap(false);
    _graphics->drawText(color, item->longName);

    // Flash entries carry a cover image beside the ADF. It is drawn over the whole
    // panel, so it is only ever drawn for the entry the cursor is on.
    if (item->source == _flashMemory && isCurrentItem(item))
    {
        String imageName = item->longName.substring(0, item->longName.lastIndexOf(".")) + ".565";
        if (SerialFlash.exists(imageName.c_str()))
        {
            _graphics->rawDraw(imageName.c_str(), 0, 0);
            return true;
        }
    }

    return false;
}

void XCopyDirectory::drawDirectory()
{
    _graphics->clearScreen();
    _graphics->setCharSpacing(1);
    _graphics->setTextScale(0);

    // Walk to the top of the window. The list is longer than the screen, so this is
    // the first entry that is actually visible rather than the head of the list.
    XCopyDirectoryEntry *item = getRoot();
    for (uint16_t skip = windowTop(); skip > 0 && item != NULL; skip--)
        item = item->next;

    for (uint16_t count = 0; item != NULL && count < ITEMSPERSCREEN; count++)
    {
        // A thumbnail covers the rows below it, so there is nothing left to draw.
        if (drawItem(item, (uint8_t)count))
            break;
        item = item->next;
    }

    _graphics->setCharSpacing(2);
}

/*
   Moving the cursor is two entries changing colour, not a new screen.

   The listing was cleared and reprinted for every click of the stick - up to twelve
   filenames blitted over identical copies of themselves, down the same SPI bus the
   flash sits on. Two rows change; two rows are drawn.

   Anything that changes more than the two rows hands back false and takes the full
   redraw: the window scrolling, and either entry being a flash one, since those
   paint a cover image over the whole listing and only a full redraw puts it back.
*/
bool XCopyDirectory::redrawSelection(XCopyDirectoryEntry *previous, uint16_t previousTop)
{
    if (previous == NULL || _currentItem == NULL)
        return false;

    if (previousTop != windowTop())
        return false;

    if (previous->source == _flashMemory || _currentItem->source == _flashMemory)
        return false;

    /*
       The window is placed from _index, so the rows are read off _index too. The
       outgoing entry has to be exactly one step from it - up() and down() move by
       one and skip nothing - and if it is not, then _index and the list have come
       apart and the only safe thing to draw is all of it.
    */
    const uint16_t top = windowTop();
    const uint16_t previousIndex = getItemIndex(previous);
    if (previousIndex + 1 != _index && _index + 1 != previousIndex)
        return false;
    if (previousIndex < top || previousIndex >= top + ITEMSPERSCREEN)
        return false;
    if (_index < top || _index >= top + ITEMSPERSCREEN)
        return false;

    _graphics->setCharSpacing(1);
    _graphics->setTextScale(0);

    if (previous != _currentItem)
        drawItem(previous, (uint8_t)(previousIndex - top));
    drawItem(_currentItem, (uint8_t)(_index - top));

    _graphics->setCharSpacing(2);

    return true;
}

XCopyDirectoryEntry::XCopyDirectoryEntry()
{
    prev = NULL;
    next = NULL;
}