#include "XCopy.h"

void XCopy::navigateDown()
{
    if (_xcopyState == menus || _xcopyState == idle)
    {
        if (_menu.down())
        {
            _audio.playClick(false);
            _menu.drawMenu(_menu.getRoot());
        }
    }

    if (_xcopyState == directorySelection)
    {
        if (_directory.down())
        {
            _audio.playClick(false);
            _directory.drawDirectory();
        }
    }
}

void XCopy::navigateUp()
{
    if (_xcopyState == menus || _xcopyState == idle)
    {
        if (_menu.up())
        {
            _audio.playClick(false);
            _menu.drawMenu(_menu.getRoot());
        }
    }

    if (_xcopyState == directorySelection)
    {
        if (_directory.up())
        {
            _audio.playClick(false);
            _directory.drawDirectory();
        }
    }
}

void XCopy::navigateLeft()
{
    if (_xcopyState == menus || _xcopyState == idle)
    {
        if (_menu.back())
        {
            _audio.playBack(false);
            _xcopyState = menus;
        }

        return;
    }

    if (_xcopyState == copyADFToDisk)
    {
        _xcopyState = directorySelection;
        // _drawnOnce = false;
        _audio.playBack(false);
        _directory.drawDirectory(true);

        return;
    }

    if (_xcopyState == directorySelection)
    {
        String path = _directory.getCurrentPath();

        if (path != "/")
        {
            String oldPath = path;

            if (path.endsWith("/"))
                path = path.remove(path.length() - 1);
            path = path.substring(0, path.lastIndexOf("/") + 1);
            _directory.getDirectory(path, &_disk, ".adf");

            XCopyDirectoryEntry *item = _directory.getRoot();
            while (item != NULL)
            {
                if (item->path == oldPath)
                    break;
                item = item->next;
            }

            _directory.setCurrentItem(item);
            _directory.setIndex(_directory.getItemIndex(item));

            _audio.playBack(false);
            _directory.drawDirectory(true);

            return;
        }
        else
        {
            _directory.clear();
            _audio.playBack(false);
            _xcopyState = menus;

            return;
        }
    }

    if (_xcopyState != menus && _xcopyState != idle)
    {
        _audio.playBack(false);
        _xcopyState = menus;
    }
}

void XCopy::navigateRight()
{
    navigateSelect();
}

void XCopy::navigateSelect()
{
    if (_xcopyState == directorySelection)
    {
        XCopyDirectoryEntry *item = _directory.getCurrentItem();

        if (item == NULL)
            return;

        // avoid changing the name to a fixed lowercase/upprcase for comparison.
        String itemName = item->name;

        if (item->isDirectory() && item->source == sdCard)
        {
            _audio.playBack(false);
            _directory.getDirectory(item->path, &_disk, ".adf");
            _directory.drawDirectory(true);
        }
        else if (item->isDirectory() && item->source == flashMemory)
        {
            _audio.playBack(false);
            _directory.getDirectoryFlash(false, &_disk, ".adf");
            _directory.drawDirectory(true);
        }
        else if (itemName.toLowerCase().endsWith(".adf") && item->source == sdCard)
        {
            _xcopyState = copyADFToDisk;
            _audio.playSelect(false);
            _graphics.clearScreen();
            _config = new XCopyConfig();
            _disk.adfToDisk(item->path + item->name, _config->getVerify(), _config->getRetryCount(), _sdCard);
            delete _config;
        }
        else if (itemName.toLowerCase().endsWith(".adf") && item->source == flashMemory)
        {
            _xcopyState = copyADFToDisk;
            _audio.playBack(false);
            _graphics.clearScreen();
            _config = new XCopyConfig();
            _disk.adfToDisk(item->path + item->name, _config->getVerify(), _config->getRetryCount(), _flashMemory);
            delete _config;
        }

        return;
    }

    if (_xcopyState == menus || _xcopyState == idle)
    {
        XCopyMenuItem *item = _menu.getCurrentItem();

        if (item->firstChild != NULL)
        {
            _menu.setRoot(item->firstChild);
            _menu.setCurrentItem(item->firstChild);
            _audio.playBack(false);
            _xcopyState = menus;
            return;
        }

        if (item->command == debuggingTempFile)
        {
            setBusy(true);
            _xcopyState = debuggingTempFile;
            _audio.playSelect(false);
        }

        if (item->command == debuggingSDFLash)
        {
            setBusy(true);
            _xcopyState = debuggingSDFLash;
            _audio.playSelect(false);
        }

        if (item->command == debuggingEraseCopy)
        {
            setBusy(true);
            _xcopyState = debuggingEraseCopy;
            _audio.playSelect(false);
        }

        if (item->command == debuggingCompareFlashToSDCard)
        {
            setBusy(true);
            _xcopyState = debuggingCompareFlashToSDCard;
            _audio.playSelect(false);
        }

        if (item->command == debuggingFlashDetails)
        {
            setBusy(true);
            _xcopyState = debuggingFlashDetails;
            _audio.playSelect(false);
        }

        if (item->command == debuggingSerialPassThrough)
        {
            setBusy(true);
            _xcopyState = debuggingSerialPassThrough;
            _audio.playSelect(false); // filenames are always uppercase 8.3 format
            _graphics.clearScreen();
            _graphics.drawText(0, 0, ST7735_GREEN, "Serial Passtrhough Mode", true);
        }        

        if (item->command == showTime)
        {
            setBusy(true);
            _xcopyState = showTime;
            _audio.playSelect(false); // filenames are always uppercase 8.3 format
            _graphics.clearScreen();
        }

        if (item->command == about)
        {
            setBusy(true);
            _xcopyState = about;
            _drawnOnce = false;
            _audio.playSelect(false); // filenames are always uppercase 8.3 format
            _graphics.clearScreen();
        }

        if (item->command == copyDiskToADF)
        {
            setBusy(true);
            _xcopyState = copyDiskToADF;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == copyDiskToFlash)
        {
            setBusy(true);
            _xcopyState = copyDiskToFlash;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == copyFlashToDisk)
        {
            setBusy(true);
            _xcopyState = copyFlashToDisk;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == testDisk)
        {
            setBusy(true);
            _xcopyState = testDisk;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == fluxDisk)
        {
            setBusy(true);
            _xcopyState = fluxDisk;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == formatDisk)
        {
            setBusy(true);
            _xcopyState = formatDisk;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == copyADFToDisk)
        {
            setBusy(true);
            _xcopyState = directorySelection;
            _drawnOnce = false;
            _audio.playSelect(false);
            _directory.getDirectory("/", &_disk, ".adf");
        }

        if (item->command == copyDiskToDisk)
        {
            setBusy(true);
            _xcopyState = copyDiskToDisk;
            _drawnOnce = false;
            _audio.playSelect(false);
        }

        if (item->command == setVerify)
        {
            setBusy(true);
            _audio.playSelect(false);
            _config = new XCopyConfig();
            _config->setVerify(!_config->getVerify());
            verifyMenuItem->text = "Set Verify: " + (_config->getVerify() ? String("True") : String("False"));
            _config->writeConfig();
            delete _config;

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setRetry)
        {
            setBusy(true);
            _audio.playSelect(false);
            _config = new XCopyConfig();
            uint8_t count = _config->getRetryCount();
            count++;
            if (count > 5)
                count = 0;
            _config->setRetryCount(count);
            
            retryCountMenuItem->text = "Set Retry Count: " + String(_config->getRetryCount());
            _config->writeConfig();
            delete _config;

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setVolume)
        {
            setBusy(true);

            _config = new XCopyConfig();
            float volume = _config->getVolume();
            volume += 0.2f;
            if (volume > 1.2f)
                volume = 0.0f;
            _config->setVolume(volume);

            volumeMenuItem->text = "Set Volume: " + String(_config->getVolume());
            _config->writeConfig();
            delete _config;

            _audio.setGain(0, volume);
            _audio.playSelect(false);

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setSSID)
        {
            setBusy(true);
            _audio.playSelect(false);

            ssidMenuItem->text = "SSID: " + _config->getSSID();

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setPassword)
        {
            setBusy(true);
            _audio.playSelect(false);

            passwordMenuItem->text = "Password: " + _config->getPassword();

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }
    }
}