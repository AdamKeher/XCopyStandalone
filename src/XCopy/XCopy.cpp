#include "XCopy.h"

XCopy::XCopy(TFT_ST7735 *tft)
{
    _tft = tft;
}

void XCopy::begin(int sdCSPin, int flashCSPin, int cardDetectPin, int busyPin)
{
#ifdef XCOPY_DEBUG
    _ram.initialize();
#endif
    Serial.begin(115200);

    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;
    _cardDetectPin = cardDetectPin;
    _busyPin = busyPin;

    pinMode(_sdCSPin, INPUT_PULLUP);
    pinMode(_flashCSPin, INPUT_PULLUP);
    pinMode(_cardDetectPin, INPUT_PULLUP);
    pinMode(_busyPin, OUTPUT);

    // ToDo: Reset ESP8266

    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";

    Serial << " X-Copy Standalone ";
    Serial << XCOPYVERSION;
    Serial << "                          (c)2018 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "\033[12h\r\n"; // terminal echo

    // Init ADF LIB
    // -------------------------------------------------------------------------------------------
    _adfLib = new XCopyADFLib();
    _adfLib->begin(_sdCSPin);

    // Init Serial Flash
    // -------------------------------------------------------------------------------------------
    if (!SerialFlash.begin(_flashCSPin))
        Serial << "\033[31mSPI Flash Chip initialization failed.\033[0m\r\n";
    else
        Serial << "\033[32mSPI Flash Chip initialized.\033[0m\r\n";

    // Init Config
    // -------------------------------------------------------------------------------------------
    _config = new XCopyConfig(false);
    if (_config->readConfig())
        Serial << "\033[32mConfig Loaded.\033[0m\r\n";
    else
        Serial << "\033[31mConfig Failed to Load.\033[0m\r\n";
    
    // Init Audio
    // -------------------------------------------------------------------------------------------
    _audio.begin(_config->getVolume());
    Serial << "\033[32mAudio initialized.\033[0m\r\n";

    // Init Time
    // -------------------------------------------------------------------------------------------
    XCopyTime::setTime();
    Serial << "\033[32mTime Set.\033[0m\r\n";

    // Init TFT
    // -------------------------------------------------------------------------------------------
    _tft->begin();
    _tft->setRotation(3);
    _tft->setCharSpacing(2);
    _graphics.begin(_tft);
    Serial << "\033[32mTFT initialized.\033[0m\r\n";

    // Intro
    // -------------------------------------------------------------------------------------------
    intro();

    // Init ESP
    // -------------------------------------------------------------------------------------------
    _graphics.drawText(0, 115, ST7735_WHITE, "               Init WiFi", true);
    _esp = new XCopyESP8266(ESPSerial, ESPBaudRate);
    _esp->setEcho(false);
    if (!_esp->begin())
        Serial << "\033[31mESP8266 WIFI Chip initialization failed. (Serial" << ESPSerial << " @ " << ESPBaudRate << ")\033[0m\r\n";
    else
    {
        _graphics.drawText(0, 115, ST7735_WHITE, "       Connecting to WiFi", true);
        Serial << "\033[32mESP8266 WIFI Chip initialized. (Serial" << ESPSerial << " @ " << ESPBaudRate << ")\033[0m\r\n";
    }
    _esp->setCallBack(theCallbackFunction);

    // Init Command Line
    // -------------------------------------------------------------------------------------------
    _command = new XCopyCommandLine(XCOPYVERSION, _adfLib, _esp, _config);

    // Init Disk Routines
    // -------------------------------------------------------------------------------------------
    _disk.begin(&_graphics, &_audio, _esp, _sdCSPin, _flashCSPin, _cardDetectPin);

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

    debugParentItem = _menu.addItem("Debugging", undefined);
    _menu.addChild("Test Temp File", debuggingTempFile, debugParentItem);
    _menu.addChild("Flash Memory Details", debuggingFlashDetails, debugParentItem);
    _menu.addChild("Compare Flash to SD Card", debuggingCompareFlashToSDCard, debugParentItem);
    _menu.addChild("Test Flash & SD Card", debuggingSDFLash, debugParentItem);
    _menu.addChild("Erase Flash and Copy SD", debuggingEraseCopy, debugParentItem);
    _menu.addChild("Serial Passthrough", debuggingSerialPassThrough, debugParentItem);

    _menu.addItem("", undefined);
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);
    _menu.addItem("", undefined);

    parentItem = _menu.addItem("Settings", undefined);
    _menu.addChild("Set Time", showTime, parentItem);
    retryCountMenuItem = _menu.addChild("Set Retry Count: " + String(_config->getRetryCount()), setRetry, parentItem);
    verifyMenuItem = _menu.addChild("Set Verify: " + (_config->getVerify() ? String("True") : String("False")), setVerify, parentItem);
    volumeMenuItem = _menu.addChild("Set Volume: " + String(_config->getVolume()), setVolume, parentItem);
    ssidMenuItem = _menu.addChild("SSID: " + _config->getSSID(), setSSID, parentItem);
    passwordMenuItem = _menu.addChild("Password: " + _config->getPassword(), setPassword, parentItem);
    _menu.addChild("", undefined, parentItem);
    _menu.addChild("About XCopy", about, parentItem);

    // delete _config;

    // Init Directory
    // -------------------------------------------------------------------------------------------
    _directory.begin(&_graphics, &_disk, _sdCSPin, _flashCSPin);

    // Init Message
    // -------------------------------------------------------------------------------------------
    Serial << "\r\nType 'help' for a list of commands.\r\n";
    _command->printPrompt();

    _menu.drawMenu(_menu.getRoot());
}

void XCopy::setBusy(bool busy)
{
    digitalWrite(_busyPin, busy);
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