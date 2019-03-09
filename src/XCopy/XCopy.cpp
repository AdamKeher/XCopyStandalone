#include "XCopy.h"

XCopy::XCopy(TFT_ST7735 *tft)
{
    _tft = tft;
}

void XCopy::begin(int sdCSPin, int flashCSPin, int cardDetectPin)
{
#ifdef XCOPY_DEBUG
    _ram.initialize();
#endif

    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;
    _cardDetectPin = cardDetectPin;

    pinMode(_sdCSPin, INPUT_PULLUP);
    pinMode(_flashCSPin, INPUT_PULLUP);
    pinMode(_cardDetectPin, INPUT_PULLUP);

    _command = new XCopyCommandLine(XCOPYVERSION);

    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";

    Serial << " X-Copy Standalone ";
    Serial << XCOPYVERSION;
    Serial << "                          (c)2018 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "\033[12h\r\n"; // terminal echo

    _audio.begin();
    Serial << "\033[32mAudio initialized.\033[0m\r\n";

    XCopyTime::setTime();
    Serial << "\033[32mTime Set.\033[0m\r\n";

    if (!SerialFlash.begin(_flashCSPin))
        Serial << "\033[31mSPI Flash Chip initialization failed.\033[0m\r\n";
    else
        Serial << "\033[32mSPI Flash Chip initialized.\033[0m\r\n";

    _tft->begin();
    _tft->setRotation(3);
    _tft->setCharSpacing(2);
    _graphics.begin(_tft);
    Serial << "\033[32mTFT initialized.\033[0m\r\n";

    _disk.begin(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
    _menu.begin(&_graphics);

    _config = new XCopyConfig(false);

    if (_config->readConfig())
        Serial << "\033[32mConfig Loaded.\033[0m\r\n";
    else
        Serial << "\033[31mConfig Failed to Load.\033[0m\r\n";

    XCopyMenuItem *parentItem;
    XCopyMenuItem *debugParentItem;

    parentItem = _menu.addItem("Disk Copy", undefined);
    _menu.addChild("Copy ADF   to Disk", copyADFToDisk, parentItem);
    _menu.addChild("Copy Disk  to ADF", copyDiskToADF, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("Copy Disk  to Disk", copyDiskToDisk, parentItem);
    _menu.addChild("Copy Disk  to Flash", copyDiskToFlash, parentItem);
    _menu.addChild("Copy Flash to Disk", copyFlashToDisk, parentItem);

    parentItem = _menu.addItem("Utils", undefined);
    _menu.addChild("Test Disk", testDisk, parentItem);
    _menu.addChild("Format Disk", formatDisk, parentItem);
    _menu.addChild("Disk Flux", fluxDisk, parentItem);
    _menu.addChild("Compare Disk to ADF", undefined, parentItem);

    debugParentItem = _menu.addItem("Debugging", undefined);
    _menu.addChild("Test Temp File", debuggingTempFile, debugParentItem);
    _menu.addChild("Flash Memory Details", debuggingFlashDetails, debugParentItem);
    _menu.addChild("Compare Flash to SD Card", debuggingCompareFlashToSDCard, debugParentItem);
    _menu.addChild("Test Flash & SD Card", debuggingSDFLash, debugParentItem);
    _menu.addChild("Erase Flash and Copy SD", debuggingEraseCopy, debugParentItem);

    _menu.addItem("", undefined);
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);

    parentItem = _menu.addItem("Settings", undefined);
    _menu.addChild("Set Time", showTime, parentItem);
    retryCountMenuItem = _menu.addChild("Set Retry Count: " + String(_config->getRetryCount()), setRetry, parentItem);
    verifyMenuItem = _menu.addChild("Set Verify: " + (_config->getVerify() ? String("True") : String("False")), setVerify, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("About XCopy", about, parentItem);

    delete _config;

    _directory.begin(&_graphics, &_disk, _sdCSPin, _flashCSPin);


    Serial << "\r\nType 'help' for a list of commands.\r\n";
    _command->printPrompt();

    intro();
    _menu.drawMenu(_menu.getRoot());
}

void XCopy::intro()
{
    _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);

    _graphics.drawText(50, 85, ST7735_GREEN, "iTeC/crAss");
    _graphics.drawText(50, 95, ST7735_WHITE, XCOPYVERSION);

    _audio.playChime(true);
    delay(500);

    _graphics.clearScreen();
}

#ifdef XCOPY_DEBUG
void XCopy::ramReport()
{
    _ram.run();
    Serial << F("\r\n=[memory report]============\r\n");
    Serial << F("total: ") << _ram.total() / 1024 << F("kb\r\n");
    uint32_t avalue = _ram.adj_free();
    Serial << F("free: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    avalue = _ram.stack_total();
    Serial << F("stack: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    avalue = _ram.heap_total();
    Serial << F(" heap: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    if (_ram.warning_crash())
        Serial << F("**Warning: stack and heap crash possible\r\n");
    if (_ram.warning_lowmem())
        Serial << F("**Warning: unallocated memory running low\r\n");
    Serial << F("=[memory report]============\r\n");
}
#endif

void XCopy::update()
{
#ifdef XCOPY_DEBUG
    if (millis() - _lastRam > 2000)
    {
        ramReport();
        _lastRam = millis();
    }
#endif

    if (_xcopyState == debuggingTempFile)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
        _debug->debugCompareTempFile();
        delete _debug;

        _xcopyState = menus;
    }

    if (_xcopyState == debuggingSDFLash)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
        _debug->debug();
        delete _debug;

        _xcopyState = menus;
    }

    if (_xcopyState == debuggingEraseCopy)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
        _debug->debugEraseCopyCompare(true);
        delete _debug;

        _xcopyState = menus;
    }

    if (_xcopyState == debuggingCompareFlashToSDCard)
    {
        _graphics.clearScreen();
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
        _debug->debugEraseCopyCompare(false);
        delete _debug;

        _xcopyState = menus;
    }

    if (_xcopyState == debuggingFlashDetails)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, _sdCSPin, _flashCSPin, _cardDetectPin);
        _debug->flashDetails();
        delete _debug;
        _xcopyState = menus;
    }

    if (_xcopyState == menus)
    {
        _graphics.clearScreen();
        _graphics.drawHeader();
        _menu.drawMenu(_menu.getRoot());
        _xcopyState = idle;
    }

    if (_xcopyState == showTime)
    {
        if (_prevSeconds != second())
        {
            char buffer[32];
            sprintf(buffer, "    %02d:%02d:%02d %02d/%02d/%04d", hour(), minute(), second(), day(), month(), year());
            _graphics.drawText(0, 55, ST7735_YELLOW, buffer, true);

            _prevSeconds = second();
        }
    }

    if (_xcopyState == copyDiskToADF)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.diskToADF("DISK0001.ADF", _config->getVerify(), _config->getRetryCount(), _sdCard);
            delete _config;
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyDiskToFlash)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.diskToADF("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            delete _config;
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyDiskToDisk)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.diskToDisk(_config->getVerify(), _config->getRetryCount());
            delete _config;
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyFlashToDisk)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.adfToDisk("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            delete _config;
            _drawnOnce = true;
        }
    }

    if (_xcopyState == testDisk)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.testDisk(_config->getRetryCount());
            delete _config;
            _drawnOnce = true;
        }
    }

    if (_xcopyState == fluxDisk)
    {
        if (_drawnOnce == false)
        {
            _disk.diskFlux();
            _drawnOnce = true;
        }
    }

    if (_xcopyState == formatDisk)
    {
        if (_drawnOnce == false)
        {
            _graphics.bmpDraw("XCPYLOGO.BMP", 0, 87);
            _config = new XCopyConfig();
            _disk.adfToDisk("BLANK.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            delete _config;
            _drawnOnce = true;

        }
    }

    if (_xcopyState == directorySelection)
    {
        if (_drawnOnce == false)
        {
            _graphics.clearScreen();
            _directory.drawDirectory();
            _drawnOnce = true;
        }
    }

    if (_xcopyState == about)
    {
        if (_drawnOnce == false)
        {
            _graphics.clearScreen();
            _graphics.drawText(0, 55, ST7735_WHITE, "     (c)2019 iTeC/crAss");
            _graphics.drawText(0, 65, ST7735_GREEN, "         " + String(XCOPYVERSION));
            _graphics.drawText(0, 75, ST7735_YELLOW, "  Insert Demo Effect Here");

            _drawnOnce = true;
        }
    }

    if (_xcopyState == idle)
    {
    }

    _command->Update();
}

void XCopy::cancelOperation()
{
    switch(_xcopyState)
    {
        case testDisk:
            _disk.cancelOperation();
            break;
        case copyDiskToADF:
            _disk.cancelOperation();
            break;
        case copyADFToDisk:
            _disk.cancelOperation();
            break;
        case copyDiskToDisk:
            _disk.cancelOperation();
            break;
        case copyDiskToFlash:
            _disk.cancelOperation();
            break;
        case copyFlashToDisk:
            _disk.cancelOperation();
            break;
        case fluxDisk:
            _disk.cancelOperation();
            break;
        case formatDisk:
            _disk.cancelOperation();
            break;
        default:
            break;
    }   
}