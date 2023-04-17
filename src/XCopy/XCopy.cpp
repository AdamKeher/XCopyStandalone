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

    Log.setESP(_esp);

    Log << XCopyConsole::clearscreen() << XCopyConsole::home() << XCopyConsole::background_purple() << XCopyConsole::high_yellow();
    Log << F("                                                                          \r\n");
    Log << F(" X-Copy Standalone ") << XCOPYVERSION <<  F("                           (c)2022 Adam Keher \r\n");
    Log << F("                                                                          \r\n");
    Log << XCopyConsole::reset() << XCopyConsole::echo() << F("\r\n");

    // Init Serial Flash
    // -------------------------------------------------------------------------------------------
    Log << F("Initialising SPI Flash RAM: ");
    if (SerialFlash.begin(PIN_FLASHCS))
        Log << XCopyConsole::success("OK\r\n");
    else
        Log << XCopyConsole::error("ERROR\r\n");

    // Init Config
    // -------------------------------------------------------------------------------------------
    Log << F("Loading configuration: ");
    _config = new XCopyConfig(false);
    if (_config->readConfig())
        Log << XCopyConsole::success("OK\r\n");
    else
        Log << XCopyConsole::error("ERROR\r\n");
    
    // Init Audio
    // -------------------------------------------------------------------------------------------
    Log << F("Initialising audio: ");
    _audio.begin(_config->getVolume());
    Log << XCopyConsole::success("OK\r\n");

    // Init Time
    // -------------------------------------------------------------------------------------------
    Log << F("Starting realtime clock: ");
    XCopyTime::syncTime();
    Log << XCopyConsole::success("OK\r\n");

    // Init TFT
    // -------------------------------------------------------------------------------------------
    Log << F("Initialising TFT: ");
    _tft->begin();
    _tft->setRotation(TFT_ROTATION);
    _tft->setCharSpacing(2);
    _graphics.begin(_tft);
    Log << XCopyConsole::success("OK\r\n");

    // Intro
    // -------------------------------------------------------------------------------------------
    intro();

    // Init Disk Routines
    // -------------------------------------------------------------------------------------------
    Log << F("Initialising drive: ");
    _disk.begin(&_graphics, &_audio, _esp);
    Log << XCopyConsole::success("OK\r\n");

    // Test Disk Orientation
    // -------------------------------------------------------------------------------------------
    Log << F("Testing drive cable orientation: ");
    _graphics.drawText(0, 115, ST7735_WHITE, "       Test Floppy Cable", true);
    delay(300);
    XCopyFloppy* _floppy = new XCopyFloppy();
    if (_floppy->detectCableOrientation() == true) {
        Log << XCopyConsole::success("OK\r\n");
    } else {
        Log << XCopyConsole::error("ERROR\r\n");
        Log << XCopyConsole::error(F("Floppy cable insererted incorrectly. Possibly upside down. Fix & reset.\r\n"));
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

    Log << F("Initialising ESP8266 WIFI (Serial") + String(Serial1) + F(" @ ") + String(ESPBaudRate) + F("): ") ;
    _graphics.drawText(0, 115, ST7735_WHITE, F("               Init WiFi"), true);
    _esp = new XCopyESP8266(ESPBaudRate, PIN_ESPRESETPIN, PIN_ESPPROGPIN);
    _esp->reset();
    _esp->setEcho(false);
    if (_esp->begin())
    {
        Log << XCopyConsole::success("OK\r\n");

        // detect cancelPin
        _graphics.drawText(0, 115, ST7735_WHITE, F("     Detecting Cancel Pin"), true);
        Log << F("Detecting cancel pin: ");
        if (detectCancelPin()) {
            Log << XCopyConsole::success("OK\r\n");
        }
        else {
            Log << XCopyConsole::error("Error\r\n");
        }

        // connect to wifi
        _graphics.drawText(0, 115, ST7735_WHITE, F("       Connecting to WiFi"), true);

        _esp->setCallBack(this, onWebCommand);

        Log << F("Connecting to wireless network (") + _config->getSSID() + "): ";

        if (_config->getSSID() == "" || _config->getPassword() == "") {
            Log << XCopyConsole::error(F(" Failed. No SSID or password set\r\n"));
        } 
        else {
            if (_esp->connect(_config->getSSID(), _config->getPassword(), 20000)) {
                Log << XCopyConsole::success(F("OK\r\n"));
                // update time from NTP server
                // -------------------------------------------------------------------------------------------
                Log << F("Updating time from NTP server: ");
                delay(1000);
                refreshTimeNtp();
                Log << XCopyConsole::success(F("OK\r\n"));
            }
            else
                Log << XCopyConsole::error(F("Failed\r\n"));
        }
    }
    else
        Log << XCopyConsole::error(F("Failed\r\n"));

    // Init Command Line
    // -------------------------------------------------------------------------------------------
    _command = new XCopyCommandLine(XCOPYVERSION, _esp, _config, &_disk);
    _command->setCallBack(this, onWebCommand);

    // Test Brainfile
    // -------------------------------------------------------------------------------------------
    Log << F("Locating BootBlock Library (brainfile.json): ");    
    Log << (XCopyBrainFile::exists() ? XCopyConsole::success("OK\r\n") : XCopyConsole::error("ERROR\r\n"));

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
    _menu.addChild("Scan Free Blocks", scanBlocks, parentItem);
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
    _directory.begin(&_graphics, &_disk);

    // Init Message
    // -------------------------------------------------------------------------------------------
    Log << F("\r\nType 'help' for a list of commands.\r\n");
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
    _graphics.clearScreen();
    _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);
    _graphics.drawText(50, 85, ST7735_GREEN, "iTeC/crAss");
    _graphics.drawText(50, 95, ST7735_WHITE, XCOPYVERSION);
    _audio.playChime(true);
}

#ifdef XCOPY_DEBUG
void XCopy::ramReport()
{
    _ram.run();
    Log << F("\r\n=[memory report]============\r\n");
    Log << F("total: ") << _ram.total() / 1024 << F("kb\r\n");
    uint32_t avalue = _ram.adj_free();
    Log << F("free: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    avalue = _ram.stack_total();
    Log << F("stack: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    avalue = _ram.heap_total();
    Log << F(" heap: ") << (avalue + 512) << F(" b (") << (((float)avalue) / _ram.total()) * 10 << F("%%)\r\n");
    if (_ram.warning_crash())
        Log << F("**Warning: stack and heap crash possible\r\n");
    if (_ram.warning_lowmem())
        Log << F("**Warning: unallocated memory running low\r\n");
    Log << F("=[memory report]============\r\n");
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

    if (_playCardSound == true) {
        _playCardSound = false;
        if (digitalRead(PIN_CARDDETECT) == 1)
            _audio.playSelect(false);
        else
            _audio.playBack(false);
    }
}

void XCopy::cancelOperation()
{
    switch (_xcopyState)
    {
    case debuggingSerialPassThrough:
        _cancelOperation = true;
        break;
    case testDisk:
    case copyDiskToADF:
    case copyADFToDisk:
    case copyDiskToDisk:
    case copyDiskToFlash:
    case copyFlashToDisk:
    case fluxDisk:
    case formatDisk:
    case scanBlocks:
    case diskSearch:
    case modSearch:
        _disk.cancelOperation();
        break;
    default:
        break;
    }
}

bool XCopy::detectCancelPin() {
    // wait for nav_left to toggle
    bool detected = false;
    uint32_t time = millis();
    while (!detected && millis() - time < 1000) { 
        _esp->print("detectpin\r\n");
#if PCBVERSION == 1
        detected = !digitalRead(PIN_NAVIGATION_LEFT_PIN);
#else
        detected = !digitalRead(PIN_NAVIGATION_UP_PIN);
#endif        
    }
    return detected;
}

void XCopy::processKeys(String keys) {
    _command->processKeys(keys);
}

void XCopy::onWebCommand(void* obj, const String command)
{
    // Log << "DEBUG::ESPCALLBACK::(" << command << ")\r\n";
    XCopy* xcopy = (XCopy*)obj;
    
    if (command == "copyDiskToADF") {
        xcopy->startFunction(copyDiskToADF);
    }
    else if (command == "copyDiskToDisk") {
        xcopy->startFunction(copyDiskToDisk);
    }
    else if (command == "copyDiskToFlash") {
        xcopy->startFunction(copyDiskToFlash);
    }
    else if (command == "copyFlashToDisk") {
        xcopy->startFunction(copyFlashToDisk);
    }
    else if (command == "testDisk") {
        xcopy->startFunction(testDisk);
    }
    else if (command == "scanBlocks") {
        xcopy->startFunction(scanBlocks);
    }
    else if (command == "formatDisk") {
        xcopy->startFunction(formatDisk);
    }
    else if (command == "diskFlux") {
        xcopy->startFunction(fluxDisk);
    }
    else if (command.startsWith("getSdFiles")) {
        String _param = "/";
        if (command.indexOf(",") > 0) {
            _param = command.substring(command.indexOf(",") + 1);
        }        
        xcopy->startFunction(getSdFiles, _param);
    }
    else if (command.startsWith("sendFile")) {
        String path = command.substring(command.indexOf(",")+1);
        xcopy->sendFile(path);
    }
    else if (command.startsWith("getFile")) {
        size_t filesize = 0;
        String path = command.substring(command.indexOf(",")+1);        
        String ssize = path.substring(path.indexOf(",")+1);
        path = path.substring(1, path.indexOf(","));
        sscanf(ssize.c_str(), "%zu", &filesize);

        xcopy->getFile(path, filesize);
    }
    // (command == "copyADFtoDisk") {
    //     xcopy->startCopyADFtoDisk();
    else if (command.startsWith("writeADFFile")) {
        String path = command.substring(command.indexOf(",")+1);
        xcopy->startCopyADFtoDisk(path);
    }
    else if (command.startsWith("k,")) {
        xcopy->processKeys(command.substring(2));
    }
    else if (command == "setBusy,true") { xcopy->setBusy(true); }
    else if (command == "setBusy,false") { xcopy->setBusy(false); }
    else if (command.startsWith("getBlock")) {
        int _sector = 0;
        if (command.indexOf(",") > 0) {
            String _param = command.substring(command.indexOf(",") + 1);
            _sector = _param.toInt();
        }        
        xcopy->sendBlock(_sector);
    }
    else if (command.startsWith("copyEmptyBlocks")) {
        String _params = command.substring(command.indexOf(",") + 1);
        int count = 0;
        byte blocks[220];
        while (_params.length() > 0) {            
            int index = _params.indexOf(",");
            blocks[count++] = _params.substring(0, index).toInt();
            _params = _params.substring(index + 1);
            if (index == -1) _params = "";
        }

        int filesize = 0;
        for (size_t index = 0; index < 220; index++) {
            for (size_t bit = 0; bit < 8; bit++) {
                if ((blocks[index] & (1 << bit)) > 0) {
                    filesize += 512;
                }
            }
        }

        xcopy->getDisk()->writeBlocksToFile(blocks, 0, filesize, ".bin", xcopy->getConfig()->getRetryCount());
    }
    else if (command.startsWith("asciiSearch")) {
        xcopy->_searchText = command.substring(command.indexOf(",") + 1);
        xcopy->startFunction(diskSearch);
    }
    else if (command.startsWith("modSearch")) {
        xcopy->_searchText = command.substring(command.indexOf(",") + 1);
        xcopy->startFunction(modSearch);
    }
    else if (command == "debuggingSerialPassThrough") {
            xcopy->startFunction(debuggingSerialPassThrough);
    }
}

void XCopy::sendFile(String path) {
   setBusy(true);

   Serial << "Sending file: '" << path << "'\r\n";
    
    XCopySDCard *_sdCard = new XCopySDCard();
    _sdCard->begin();
    
    if (!_sdCard->cardDetect()) {
        Serial1.print("error");
        Serial << _sdCard->getError() + "\r\n";
        delete _sdCard;
        setBusy(false);
        return;
    }

    if (!_sdCard->begin()) {
        Serial1.print("error");
        Serial << _sdCard->getError() + "\r\n";
        delete _sdCard;
        setBusy(false);
        return;
    }

    FatFile file;
    bool fresult = file.open(path.c_str());

    if (!fresult) {
        Serial1.print("error");
        Serial << "SD file open failed";
        delete _sdCard;
        setBusy(false);
        return;
    }

    _graphics.clearScreen();
    _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);
    _graphics.drawText(44, 85, ST7735_GREEN, "Sending File", TRUE);

    // send file size
    size_t size = file.fileSize();
    Serial1.print(size);
    Serial1.print("\n");

    // copy data from sd file to flash file
    size_t bufferSize = 2048;
    char buffer[bufferSize];
    int readsize = 0;

    unsigned long time = millis();

    do {
        readsize = file.read(buffer, bufferSize);
        Serial1.write(buffer, readsize);
        Serial.print(".");
        delay(75);
    } while (readsize > 0);

    Serial << "\r\nSent file '";
    file.printName();
    Serial << "': " << file.fileSize() << " in " << (millis() - time) / 1000.0f << "s\r\n";

    file.close();
    delete _sdCard;

    Serial.println("Done");

    _menu.redraw();

    setBusy(false);
}

void XCopy::getFile(String path, size_t filesize) {
   setBusy(true);

   Serial << "Getting file: '" << path << "' (" << filesize << ")\r\n";
    
    XCopySDCard *_sdCard = new XCopySDCard();
    _sdCard->begin();
    bool error = false;

    if (!_sdCard->cardDetect()) {
        Serial1.print("error,detect\n");
        Serial << "Error: Card detect error\r\n";
        error = true;
    }

    if (!_sdCard->begin()) {
        Serial1.print("error,init\n");
        Serial << "Error: Initialisation error\r\n";
        error = true;
    }    

    if (_sdCard->fileExists(path)) {
        Serial1.print("error,exists\n");
        Serial << "Error: File exists\r\n";
        error = true;
    }

    FatFile file;
    bool fresult = file.open(path.c_str(), O_RDWR | O_CREAT);
    if (!fresult) {
        Serial1.print("error,open\n");
        Serial << "Error: SD file open failed\r\n";
        error = true;
    }

    if (error) {
        delete _sdCard;
        setBusy(false);
        return;
    }

    _graphics.clearScreen();
    _graphics.bmpDraw("XCPYLOGO.BMP", 0, 30);
    _graphics.drawText(42, 85, ST7735_GREEN, "Receiving File", TRUE);

    // send file size
    Serial1.print("OK\n");

    // copy data from sd file to flash file
    size_t bufferSize = 2048;
    char buffer[bufferSize];
    int readsize = 0;

    unsigned long time = millis();
    unsigned long lasttime = millis();
    size_t totalsize = 0;

    do {
        while (Serial1.available()) {
            lasttime = millis();
            readsize = Serial1.readBytes(buffer, bufferSize);
            if (readsize > 0) {
                totalsize += readsize;
                file.write(buffer, readsize);
                Serial.print(".");            
            }
            if (totalsize >= filesize) {
                Serial << "Exact File Size!\r\n";
                break;
            }
        }

        if (millis() - lasttime > 1000) { break; }
    } while (true);

    Serial << "\r\nReceived file '";
    file.printName();
    Serial << "': " << file.fileSize() << " in " << (millis() - time) / 1000.0f << "s\r\n";

    file.close();
    delete _sdCard;

    Serial.println("Done");

    _menu.redraw();

    setBusy(false);
}

void XCopy::sendBlock(int block) {
    setBusy(true);

    int track = floor(block / 11.0f);
    int sector = block % 11;

    gotoLogicTrack(track);
    uint8_t errors = readTrack(true);
    if (errors != -1) {
        Track *track = getTrack();
        struct Sector *aSec = (Sector *)&track[sector].sector;

        String webLine = "broadcast sendBlockDetails," + String(block) + "," + String(getTrackInfo()) + "," + String(errors) 
            + "," + String(getSectorCnt()) + "," + String(getBitCount()) +  "," + String(aSec->format_type) + "," 
            + String(aSec->toGap) + "," + String(aSec->data_chksum) + "," + String(aSec->header_chksum) + "\r\n";
        _esp->print(webLine);

        for (int i = 0; i < 16; i++) {
            webLine = "";
            for (int j = 0; j < 32; j++) {
                if (aSec->data[(i * 32) + j] < 16) {
                    webLine.append("0");
                }
                webLine.append(String(aSec->data[(i * 32) + j], HEX) + "|");
            }
            _esp->print("broadcast sendBlock," + String(block) + "," + String(i) +"," + webLine + "\r\n");
            delay(20);
        }

        analyseHist(true);
        float time = .0f;
        String line = "broadcast sendBlockHist,";
        int *hist = getHist();
        for (int i = 0; i < 256; i++) {
            if (hist[i] > 0) {
                time = (float(i) * 0.04166667) + 0.25;
                line += String(time) + "|" + String(hist[i]) + "&";
            }
        }
        line += "\r\n";
        _esp->print(line);
    }
    else {
        Log << F("bitCount: ") + String(getBitCount()) + F(" (Read failed!)\r\n");
    }
    setBusy(false);
}

void XCopy::cardChange()
{
    // mark the audio sample to be played, do not play inside the interrupt
    _playCardSound = true;
}

// void XCopy::copyBlock(byte blocks[]) {
//     _disk.writeBlocksToFile(blocks, _config->getRetryCount());
// }

void XCopy::startFunction(XCopyState state, String param) {
    if (state == getSdFiles) {
        setBusy(true);
        _esp->updateWebSdCardFiles(param);
        setBusy(false);
        return;
    }

    if (state == debuggingSerialPassThrough) {
        _menu.setCurrentItem(state);
        navigateSelect();
        return;
    }
    setBusy(true);
    _esp->setState(state);
    _menu.setCurrentItem(state);
    _xcopyState = state;
    _drawnOnce = false;
    _audio.playSelect(false);
    _graphics.clearScreen();
}

void XCopy::startCopyADFtoDisk(String path) {
    if (path == "") {
        startFunction(directorySelection);
        _directory.getDirectory("/", &_disk, ".adf");
    } else {
        // startFunction(directorySelection);
        setBusy(true);
        _directory.getDirectory("/", &_disk, ".adf");
        _xcopyState = copyADFToDisk;
        _audio.playSelect(false);
        // _config = new XCopyConfig();
        _disk.adfToDisk(path, _config->getVerify(), _config->getRetryCount(), _sdCard);
        // delete _config;
        setBusy(false);
    }
}

// NAVIGATION

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

        if (item->isDirectory() && item->source == _sdCard)
        {
            String directory = _directory.getCurrentPath() + item->longName + "/";
            _audio.playBack(false);
            _directory.getDirectory(directory, &_disk, ".adf");
            _directory.drawDirectory(true);
        }
        else if (item->isDirectory() && item->source == _flashMemory)
        {
            _audio.playBack(false);
            _directory.getDirectoryFlash(false, &_disk, ".adf");
            _directory.drawDirectory(true);
        }
        else if (item->longName.toLowerCase().endsWith(".adf"))
        {
            _xcopyState = copyADFToDisk;
            _audio.playSelect(false);
            String itemname = (item->source == _sdCard ? _directory.getCurrentPath() + item->longName : item->longName.toUpperCase());
            _disk.adfToDisk(itemname, _config->getVerify(), _config->getRetryCount(), item->source);
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

        if (item->command == debuggingFaultFind)
        {
            setBusy(true);
            _xcopyState = debuggingFaultFind;
            _audio.playSelect(false);
        }

        if (item->command == debuggingEraseFlash)
        {
            setBusy(true);
            _xcopyState = debuggingEraseFlash;
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
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 0, ST7735_GREEN, "ESP Passthrough Mode", true);
        }

        if (item->command == debuggingSerialPassThroughProg)
        {
            setBusy(true);

            _xcopyState = debuggingSerialPassThrough; // set as passthrough now ESP is in programming mode
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 0, ST7735_GREEN, "ESP Programming Mode", true);

            _esp->progMode();
        }

        if (item->command == resetESP)
        {
            setBusy(true);
            _xcopyState = resetESP;
            _audio.playSelect(false);

            _esp->reset();

            setBusy(false);

            _xcopyState = menus;
        }

        if (item->command == showTime)
        {
            setBusy(true);
            _xcopyState = showTime;
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 35, ST7735_YELLOW, "    Updating Time via NTP", true);
            _graphics.drawText(0, 35, ST7735_YELLOW, "          Updated Time", true);
            refreshTimeNtp();
        }

        if (item->command == about)
        {
            setBusy(true);
            _xcopyState = about;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == copyADFToDisk)
        {
            startCopyADFtoDisk();
        }

        if (item->command == copyDiskToADF)
        {
            startFunction(copyDiskToADF);
        }

        if (item->command == copyDiskToDisk)
        {
            startFunction(copyDiskToDisk);
        }

        if (item->command == copyDiskToFlash)
        {
            startFunction(copyDiskToFlash);
        }

        if (item->command == copyFlashToDisk)
        {
            startFunction(copyFlashToDisk);
        }

        if (item->command == testDisk)
        {
            startFunction(testDisk);
        }

        if (item->command == scanBlocks) {
            startFunction(scanBlocks);
        }

        if (item->command == formatDisk)
        {
            startFunction(formatDisk);
        }

        if (item->command == fluxDisk)
        {
            startFunction(fluxDisk);
        }

        if (item->command == setVerify)
        {
            setBusy(true);
            _audio.playSelect(false);
            // _config = new XCopyConfig();
            _config->setVerify(!_config->getVerify());
            verifyMenuItem->text = "Set Verify: " + (_config->getVerify() ? String("True") : String("False"));
            _config->writeConfig();
            // delete _config;

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setRetry)
        {
            setBusy(true);
            _audio.playSelect(false);
            // _config = new XCopyConfig();
            uint8_t count = _config->getRetryCount();
            count++;
            if (count > 5)
                count = 0;
            _config->setRetryCount(count);

            retryCountMenuItem->text = "Set Retry Count: " + String(_config->getRetryCount());
            _config->writeConfig();
            // delete _config;

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }

        if (item->command == setVolume)
        {
            setBusy(true);

            // _config = new XCopyConfig();
            float volume = _config->getVolume();
            volume += 0.2f;
            if (volume > 1.2f)
                volume = 0.0f;
            _config->setVolume(volume);

            volumeMenuItem->text = "Set Volume: " + String(_config->getVolume());
            _config->writeConfig();
            // delete _config;

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

        if (item->command == testDrive) {
            setBusy(true);
            _xcopyState = testDrive;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
        }

        if (item->command == setDiskDelay) {
            setBusy(true);

            // _config = new XCopyConfig();
            uint16_t delay2 = _config->getDiskDelay();
            delay2 += 100;
            if (delay2 > 500 )
                delay2 = 200;
            _config->setDiskDelay(delay2);

            diskDelayMenuItem->text = "Set Disk Delay: " + String(_config->getDiskDelay()) + "ms";
            _config->writeConfig();
            // delete _config;

            _audio.playSelect(false);

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }        

        if (item->command == setTimeZone) {
            setBusy(true);

            // _config = new XCopyConfig();
            int timeZone = _config->getTimeZone();
            timeZone++;
            if (timeZone > 12 )
                timeZone = -12;
            _config->setTimeZone(timeZone);

            timeZoneMenuItem->text = "Set Time Zone: " + String(timeZone >= 0 ? "+" : "") + String(timeZone);
            _config->writeConfig();

            // delete _config;
            _audio.playSelect(false);

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
        }        

        if (item->command == resetDevice) {
            Serial << "Resetting ...";
            pinMode(28, OUTPUT);
            pinMode(28, OUTPUT_OPENDRAIN);
            Serial << " Looks like pin 28 has not been jumpered to the RST pad on your Teensy 3.2\r\n";
        }   
    }
}

void XCopy::processState()
{
    if (_xcopyState == debuggingTempFile)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugCompareTempFile();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingSDFLash)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugTestFlashSD();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingEraseCopy)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugEraseCopyCompare();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingFaultFind)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugFaultFind();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingEraseFlash)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugEraseFlash();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingCompareFlashToSDCard)
    {
        _graphics.clearScreen();
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugCompare();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingFlashDetails)
    {
        XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_SDCS, PIN_FLASHCS, PIN_CARDDETECT);
        _debug->debugFlashDetails();
        delete _debug;

        setBusy(false);
        _xcopyState = menus;
    }

    if (_xcopyState == debuggingSerialPassThrough)
    {
        _esp->setEcho(true);

        while (!_cancelOperation)
        {
            if (Serial.available())
            {
                ESPSerial.write(Serial.read());
            }

            if (ESPSerial.available())
            {
                Serial.write(ESPSerial.read());
            }
        }

        _esp->setEcho(false);
        _cancelOperation = false;
        setBusy(false);
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
            // _config = new XCopyConfig();
            _disk.diskToADF("", _config->getVerify(), _config->getRetryCount(), _sdCard);
            // delete _config;

            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyDiskToFlash)
    {
        if (_drawnOnce == false)
        {
            // _config = new XCopyConfig();
            _disk.diskToADF("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            // delete _config;

            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyDiskToDisk)
    {
        if (_drawnOnce == false)
        {
            // _config = new XCopyConfig();
            _disk.diskToDisk(_config->getVerify(), _config->getRetryCount());
            // delete _config;

            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == copyFlashToDisk)
    {
        if (_drawnOnce == false)
        {
            // _config = new XCopyConfig();
            _disk.adfToDisk("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            // delete _config;

            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == testDisk)
    {
        if (_drawnOnce == false)
        {
            _disk.testDiskette(_config->getRetryCount());
            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == scanBlocks)
    {
        if (_drawnOnce == false)
        {
            _disk.scanEmptyBlocks(_config->getRetryCount());
            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == fluxDisk)
    {
        if (_drawnOnce == false)
        {
            _disk.diskFlux();

            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == formatDisk)
    {
        if (_drawnOnce == false)
        {
            // _config = new XCopyConfig();
            _disk.adfToDisk("BLANK.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
            // delete _config;

            setBusy(false);
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

    if (_xcopyState == testDrive) {
        if (_drawnOnce == false)
        {
            XCopyDriveTest *driveTest = new XCopyDriveTest();
            driveTest->begin(&_graphics, &_audio, _esp);
            driveTest->draw();
            while (1==1) {
                driveTest->update();
            }
            delete driveTest;
            setBusy(false);
            _drawnOnce = true;
        }        
    }

    if (_xcopyState == diskSearch) {
        if (_drawnOnce == false) {
            _disk.asciiSearch(_searchText, _config->getRetryCount());
            _searchText = "";
            setBusy(false);
            _drawnOnce = true;
        }
    }

    if (_xcopyState == modSearch) {
        if (_drawnOnce == false) {
            _disk.modSearch(_config->getRetryCount());
            _searchText = "";
            setBusy(false);
            _drawnOnce = true;
        }
    }    

    if (_xcopyState == about)
    {
        if (_drawnOnce == false)
        {
            _graphics.clearScreen();
            _graphics.drawText(0, 55, ST7735_WHITE, "     (c)2019 iTeC/crAss");
            _graphics.drawText(0, 65, ST7735_GREEN, "           " + String(XCOPYVERSION));
            _graphics.drawText(0, 75, ST7735_YELLOW, "  Insert Demo Effect Here");

            _drawnOnce = true;
        }
    }

    if (_xcopyState == idle)
    {
        setBusy(false);
    }
}