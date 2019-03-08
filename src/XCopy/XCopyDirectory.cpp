#include "XCopyDirectory.h"

XCopyDirectory::XCopyDirectory()
{
}

void XCopyDirectory::begin(XCopyGraphics *graphics, XCopyDisk *disk, uint8_t sdCSPin, uint8_t flashCSPin)
{
    _graphics = graphics;
    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;

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
    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "\r\nError Accessing SPI Flash.\r\n";
        return;
    }

    if (root)
    {
        XCopyDirectoryEntry *defaultItems = new XCopyDirectoryEntry();
        defaultItems->setIsDirectory(true);
        defaultItems->source = flashMemory;
        defaultItems->name = "Built In ADF Files";
        defaultItems->longName = "Built In ADF Files";
        defaultItems->path = "/Built In ADF Files/";
        addRoot(defaultItems);
    }
    else
    {
        clear();
        _currentPath = "/Built In ADF Files/";
        SerialFlash.opendir();
        while (1)
        {
            char filename[64];
            uint32_t filesize;

            if (SerialFlash.readdir(filename, sizeof(filename), filesize))
            {
                if (String(filename).toUpperCase().endsWith(filter.toUpperCase()))
                {
                    XCopyDirectoryEntry *flashFile = new XCopyDirectoryEntry();
                    flashFile->setIsDirectory(false);
                    flashFile->name = filename;
                    strlwr(filename);
                    flashFile->longName = filename;
                    flashFile->size = filesize;
                    flashFile->source = flashMemory;
                    flashFile->volumeName = "Unknown";
                    flashFile->volumeName = disk->getADFVolumeName(flashFile->name, _flashMemory);
                    flashFile->path = "";
                    flashFile = addItem(flashFile);
                }
            }
            else
            {
                break; // no more files
            }
        }
    }
}

void XCopyDirectory::getDirectory(String path, XCopyDisk *disk, String filter, bool dirAtTop)
{
    clear();
    _currentPath = path;

    if (_disk->cardDetect())
        if (SD.begin(_sdCSPin))
        {
            char buffer[path.length()];
            memset(buffer, 0, sizeof(buffer));
            path.toCharArray(buffer, sizeof(buffer) + 1);

            SdFile root;
            root.open(buffer);

            while (true)
            {
                SdFile entry;
                if (!entry.openNext(&root, O_RDONLY))
                    break;

                char sfnBuffer[20];
                char lfnBuffer[255];
                entry.getSFN(sfnBuffer);
                entry.getName(lfnBuffer, 255);
                strupr(sfnBuffer);

                dir_t d;
                entry.dirEntry(&d);

                if (entry.isDir() && String(sfnBuffer) != "SYSTEM~1")
                {
                    XCopyDirectoryEntry *item = new XCopyDirectoryEntry();
                    item->name = sfnBuffer;
                    item->longName = lfnBuffer;
                    item->setIsDirectory(true);
                    item->path = path + sfnBuffer + '/';
                    item->source = sdCard;
                    addItem(item);
                }
                else if ((String(sfnBuffer).endsWith(filter.toUpperCase()) || filter == "") && String(sfnBuffer) != "SYSTEM~1")
                {
                    XCopyDirectoryEntry *item = new XCopyDirectoryEntry();
                    item->name = sfnBuffer;
                    item->longName = lfnBuffer;
                    item->date = String(FAT_YEAR(d.creationDate)) + "/" + String(FAT_MONTH(d.creationDate)) + '/' + String(FAT_DAY(d.creationDate));
                    item->size = entry.fileSize();
                    item->setIsDirectory(false);
                    item->path = path;
                    item->source = sdCard;
                    item->volumeName = disk->getADFVolumeName(item->path + item->name);

                    addItem(item);
                }
                entry.close();
            }

            // move directory entries to top in sorted order
            if (dirAtTop)
            {
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
        }

    if (path == "/")
    {
        getDirectoryFlash(true, NULL);
    }
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
            result = item->name < compare->name;
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
    Serial << "        Item: " << item->name << "\r\n";
    Serial << "        Path: " << item->path << "\r\n";
    Serial << " Volume Name: " << item->volumeName << "\r\n";
    Serial << "   Directory: " << (item->isDirectory() ? "TRUE" : "FALSE") << "\r\n";
    Serial << "        Size: " << item->size << "\r\n";
    Serial << "      Source: " << item->source << "\r\n";
    Serial << "        Prev: " << (item->prev == NULL ? "NULL" : item->prev->name) << "\r\n";
    Serial << "        Next: " << (item->next == NULL ? "NULL" : item->next->name) << "\r\n";
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

void XCopyDirectory::drawDirectory(bool clearScreen)
{
    if (clearScreen)
        _graphics->clearScreen();

    _graphics->setCharSpacing(1);
    _graphics->setTextScale(0);

    // seclect current item
    XCopyDirectoryEntry *item = getRoot();

    // skip ahead for scrolling
    if (getIndex() + 1 >= ITEMSPERSCREEN)
    {
        for (int i = 0; i < getIndex() + 1 - ITEMSPERSCREEN; i++)
        {
            item = item->next;
        }

        _graphics->clearScreen();
    }

    uint16_t count = 0;

    _graphics->clearScreen();

    while (item != NULL && count < ITEMSPERSCREEN)
    {
        _graphics->setCursor(5, 0 + (count * 10));
        if (item->isDirectory())
        {
            if (item->source == flashMemory)
                _graphics->drawText(ST7735_CYAN, ">> ");
            else
                _graphics->drawText(ST7735_YELLOW, ">> ");
        }

        uint16_t color = isCurrentItem(item) ? ST7735_GREEN : ST7735_WHITE;
        _graphics->setTextWrap(false);
        _graphics->drawText(color, item->longName);

        if (item->source == flashMemory && isCurrentItem(item))
        {
            String imageName = item->name.substring(0, item->name.lastIndexOf(".")) + ".TMB";
            if (SD.exists(imageName.c_str()))
            {
                _graphics->bmpDraw(imageName.c_str(), 0, 0);
                break;
            }
        } 
        else if (item->source == sdCard && isCurrentItem(item))
        {
            _graphics->getTFT()->fillRect(0, 119, _graphics->getTFT()->width(), 10, ST7735_BLUE);
            _graphics->drawText(5, 120, ST7735_YELLOW, item->isDirectory() ? "Directory" : "Vol: " + item->volumeName);
        }

        item = item->next;
        count++;
    }

    _graphics->setCharSpacing(2);
}

XCopyDirectoryEntry::XCopyDirectoryEntry()
{
    prev = NULL;
    next = NULL;
}