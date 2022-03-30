#include "XCopy.h"

XCopy::XCopy(TFT_ST7735 *tft)
{
    _tft = tft;
}

void XCopy::begin()
{
#ifdef XCOPY_DEBUG
    _ram.initialize();
#endif
    Serial.begin(115200);

    pinMode(PIN_SDCS, INPUT_PULLUP);
    pinMode(PIN_FLASHCS, INPUT_PULLUP);
    pinMode(PIN_CARDDETECT, INPUT_PULLUP);
    pinMode(PIN_BUSYPIN, OUTPUT);
    pinMode(PIN_ESPRESETPIN, OUTPUT);
    pinMode(PIN_ESPPROGPIN, OUTPUT);
    
    Serial << XCopyConsole::clearscreen() << XCopyConsole::home() << XCopyConsole::background_purple() << XCopyConsole::high_yellow();
    Serial << F("                                                                          \r\n");
    Serial << F(" X-Copy Standalone ") << XCOPYVERSION <<  F("                           (c)2022 Adam Keher \r\n");
    Serial << F("                                                                          \r\n");
    Serial << XCopyConsole::reset() << XCopyConsole::echo() << F("\r\n");

    // Init Serial Flash
    // -------------------------------------------------------------------------------------------
    Serial << F("Initialising SPI Flash RAM: ");
    if (SerialFlash.begin(PIN_FLASHCS))
        Serial << XCopyConsole::success("OK\r\n");
    else
        Serial << XCopyConsole::error("ERROR\r\n");

    // Init Config
    // -------------------------------------------------------------------------------------------
    Serial << F("Loading configuration: ");
    _config = new XCopyConfig(false);
    if (_config->readConfig())
        Serial << XCopyConsole::success("OK\r\n");
    else
        Serial << XCopyConsole::error("ERROR\r\n");
    
    // Init Audio
    // -------------------------------------------------------------------------------------------
    Serial << F("Initialising audio: ");
    _audio.begin(_config->getVolume());
    Serial << XCopyConsole::success("OK\r\n");

    // Init Time
    // -------------------------------------------------------------------------------------------
    Serial << F("Starting realtime clock: ");
    XCopyTime::syncTime();
    Serial << XCopyConsole::success("OK\r\n");

    // Init TFT
    // -------------------------------------------------------------------------------------------
    Serial << F("Initialising TFT: ");
    _tft->begin();
    _tft->setRotation(3);
    _tft->setCharSpacing(2);
    _graphics.begin(_tft);
    Serial << XCopyConsole::success("OK\r\n");

    // Intro
    // -------------------------------------------------------------------------------------------
    intro();

    // Init Disk Routines
    // -------------------------------------------------------------------------------------------
    Serial << F("Initialising drive: ");
    _disk.begin(&_graphics, &_audio, _esp, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
    Serial << XCopyConsole::success("OK\r\n");

    // Test Disk Orientation
    // -------------------------------------------------------------------------------------------
    Serial << F("Testing drive cable orientation: ");
    _graphics.drawText(0, 115, ST7735_WHITE, "       Test Floppy Cable", true);
    delay(300);
    XCopyFloppy* _floppy = new XCopyFloppy();
    if (_floppy->detectCableOrientation() == true) {
        Serial << XCopyConsole::success("OK\r\n");
    } else {
        Serial << XCopyConsole::error("ERROR\r\n");
        Serial << XCopyConsole::error(F("Floppy cable insererted incorrectly. Possibly upside down. Fix & reset.\r\n"));
        _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);
        _graphics.drawText(47, 75, ST7735_RED, "Floppy Cable", TRUE);
        _graphics.drawText(45, 85, ST7735_RED, "Upside Down!", TRUE);
        _graphics.drawText(44, 95, ST7735_RED, "Fix and Reset",TRUE);
        _audio.playChime(true);
        delay(5000);
    }
    delete _floppy;

    // Init ESP
    // -------------------------------------------------------------------------------------------
    Serial << F("Initialising ESP8266 WIFI (Serial") + String(Serial1) + F(" @ ") + String(ESPBaudRate) + F("): ") ;
    _graphics.drawText(0, 115, ST7735_WHITE, F("               Init WiFi"), true);
    _esp = new XCopyESP8266(ESPBaudRate, PIN_ESPRESETPIN, PIN_ESPPROGPIN);
    _esp->reset();
    _esp->setEcho(false);
    if (_esp->begin())
    {
        _esp->setCallBack(theCallbackFunction);
        _graphics.drawText(0, 115, ST7735_WHITE, F("       Connecting to WiFi"), true);

        Serial << XCopyConsole::success("OK\r\n");
        Serial << "Connecting to wireless network (" + _config->getSSID() + "): ";
        if (_esp->connect(_config->getSSID(), _config->getPassword(), 20000)) {
            Serial << XCopyConsole::success("OK\r\n");
            // update time from NTP server
            // -------------------------------------------------------------------------------------------
            Serial << F("Updating time from NTP server: ");
            delay(1000);
            refreshTimeNtp();
            Serial << XCopyConsole::success("OK\r\n");
        }
        else
            Serial << XCopyConsole::error("Failed.\r\n");
    }
    else
        Serial << XCopyConsole::error(F("ESP8266 WIFI Chip initialization failed. (Serial") + String(Serial1) + F(" @ ") + String(ESPBaudRate) + F(").\r\n"));

    // Init Command Line
    // -------------------------------------------------------------------------------------------
    _command = new XCopyCommandLine(XCOPYVERSION, _esp, _config);

    // Init Menu
    // -------------------------------------------------------------------------------------------
    _menu.begin(&_graphics);
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
    _menu.addChild("Test Drive", testDrive, parentItem);

    debugParentItem = _menu.addItem("Debugging", undefined);

    XCopyMenuItem *espParentItem = _menu.addChild("ESP", undefined, debugParentItem);
    _menu.addChild("ESP Passthrough Mode", debuggingSerialPassThrough, espParentItem);
    _menu.addChild("ESP Programming Mode", debuggingSerialPassThroughProg, espParentItem);
    _menu.addItem("", undefined);
    _menu.addChild("Reset ESP", resetESP, espParentItem);

    XCopyMenuItem *flashParentItem = _menu.addChild("Flash", undefined, debugParentItem);
    XCopyMenuItem *dangerousParentitem = _menu.addChild("Dangerous", undefined, flashParentItem);
    _menu.addChild("Erase Flash", debuggingEraseFlash, dangerousParentitem);
    _menu.addChild("Erase Flash and Copy SD", debuggingEraseCopy, dangerousParentitem);
    _menu.addChild("Erase Flash and Fault Find", debuggingFaultFind, dangerousParentitem);
    _menu.addChild("Flash Memory Details", debuggingFlashDetails, flashParentItem);
    _menu.addChild("Compare Flash to SD Card", debuggingCompareFlashToSDCard, flashParentItem);
    _menu.addChild("Test Temp File", debuggingTempFile, flashParentItem);
    _menu.addChild("Test Flash & SD Card", debuggingSDFLash, flashParentItem);
    
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);

    parentItem = _menu.addItem("Settings", undefined);
    volumeMenuItem = _menu.addChild("Set Volume: " + String(_config->getVolume()), setVolume, parentItem);

    XCopyMenuItem *timeParentItem = _menu.addChild("Time", undefined, parentItem);
    _menu.addChild("Set Time from NTP", showTime, timeParentItem);
    int timeZone = _config->getTimeZone();
    timeZoneMenuItem = _menu.addChild("Set Time Zone: " + String(timeZone >= 0 ? "+" : "") + String(timeZone), setTimeZone, timeParentItem);

    XCopyMenuItem *diskParentItem = _menu.addChild("Disk", undefined, parentItem);
    retryCountMenuItem = _menu.addChild("Set Retry Count: " + String(_config->getRetryCount()), setRetry, diskParentItem);
    verifyMenuItem = _menu.addChild("Set Verify: " + (_config->getVerify() ? String("True") : String("False")), setVerify, diskParentItem);
    diskDelayMenuItem = _menu.addChild("Set Disk Delay: " + String(_config->getDiskDelay()) + "ms", setDiskDelay, diskParentItem);


    XCopyMenuItem *networkParentItem = _menu.addChild("Network", undefined, parentItem);
    ssidMenuItem = _menu.addChild("SSID: " + _config->getSSID(), setSSID, networkParentItem);
    passwordMenuItem = _menu.addChild("Password: " + _config->getPassword(), setPassword, networkParentItem);

    _menu.addChild("", undefined, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("Reset / Reboot", resetDevice, parentItem);
    _menu.addChild("About XCopy", about, parentItem);

    // delete _config;

    // Init Directory
    // -------------------------------------------------------------------------------------------
    _directory.begin(&_graphics, &_disk, PIN_SDCS, PIN_FLASHCS);

    // Init Message
    // -------------------------------------------------------------------------------------------
    Serial << F("\r\nType 'help' for a list of commands.\r\n");
    _command->printPrompt();

    _menu.drawMenu(_menu.getRoot());
}

void XCopy::refreshTimeNtp() {
    XCopyTime::syncTime(false);
    time_t time = _esp->getTime();
    int timeZone = _config->getTimeZone();
    time = time + (timeZone * 60 * 60);
    XCopyTime::setTime(time);
    XCopyTime::syncTime(true);
}

void XCopy::setBusy(bool busy)
{
    digitalWrite(PIN_BUSYPIN, busy);
}

void XCopy::intro()
{
    _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);

    _graphics.drawText(50, 85, ST7735_GREEN, "iTeC/crAss");
    _graphics.drawText(50, 95, ST7735_WHITE, XCOPYVERSION);

    _audio.playChime(true);
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
    if (millis() - _lastRam > 5000)
    {
        ramReport();
        _lastRam = millis();
    }
#endif

    processState();
    _command->Update();
    _esp->Update();
}

void XCopy::cancelOperation()
{
    switch (_xcopyState)
    {
    case debuggingSerialPassThrough:
        _cancelOperation = true;
        break;
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

void XCopy::theCallbackFunction(const String command)
{    
    Serial << "DEBUG::ESPCALLBACK::(" << command << ")\r\n";
}