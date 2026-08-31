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

    // Must precede Log.setESP() and _disk.begin(): both store this pointer for the
    // life of the program and were previously handed a null one. The constructor
    // only opens Serial1 and drives the two pins configured above, so it is safe
    // this early; the reset and handshake stay in "Init ESP" below.
    _esp = new XCopyESP8266(ESPBaudRate, PIN_ESPRESETPIN, PIN_ESPPROGPIN);

    Log.setESP(_esp);

    // Teensy end of the ESP file transfer protocol; the wire format both ends
    // implement is in shared/XCopyProtocol.h.
    _transfer.begin(&ESPSerial, &_graphics);

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
    _disk.begin(&_graphics, &_audio, _esp, &_floppy);
    Log << XCopyConsole::success("OK\r\n");

    // Test Disk Orientation
    // -------------------------------------------------------------------------------------------
    Log << F("Testing drive cable orientation: ");
    _graphics.drawText(0, 115, ST7735_WHITE, "       Test Floppy Cable", true);
    delay(300);
    if (_floppy.detectCableOrientation() == true) {
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

    // Init ESP
    // -------------------------------------------------------------------------------------------

    Log << F("Initialising ESP8266 WIFI (Serial") + String(Serial1) + F(" @ ") + String(ESPBaudRate) + F("): ") ;
    _graphics.drawText(0, 115, ST7735_WHITE, F("               Init WiFi"), true);
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
    _command = new XCopyCommandLine(XCOPYVERSION, _esp, _config, &_disk, &_floppy);
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

    parentItem = _menu.addItem("Disk Copy", XCopyAction::none);
    _menu.addChild("Copy ADF   to Disk", XCopyAction::copyADFToDisk, parentItem);
    _menu.addChild("Copy Disk  to ADF", XCopyAction::copyDiskToADF, parentItem);
    _menu.addChild("", XCopyAction::none, parentItem);
    _menu.addChild("Copy Disk  to Disk", XCopyAction::copyDiskToDisk, parentItem);
    _menu.addChild("Copy Disk  to Flash", XCopyAction::copyDiskToFlash, parentItem);
    _menu.addChild("Copy Flash to Disk", XCopyAction::copyFlashToDisk, parentItem);

    parentItem = _menu.addItem("Utils", XCopyAction::none);
    _menu.addChild("Test Disk", XCopyAction::testDisk, parentItem);
    _menu.addChild("Format Disk", XCopyAction::formatDisk, parentItem);
    _menu.addChild("Disk Flux", XCopyAction::fluxDisk, parentItem);
    _menu.addChild("Scan Free Blocks", XCopyAction::scanBlocks, parentItem);
    _menu.addChild("Compare Disk to ADF", XCopyAction::none, parentItem);
    _menu.addChild("Test Drive", XCopyAction::testDrive, parentItem);

    debugParentItem = _menu.addItem("Debugging", XCopyAction::none);

    XCopyMenuItem *espParentItem = _menu.addChild("ESP", XCopyAction::none, debugParentItem);
    _menu.addChild("ESP Passthrough Mode", XCopyAction::debuggingSerialPassThrough, espParentItem);
    _menu.addChild("ESP Programming Mode", XCopyAction::debuggingSerialPassThroughProg, espParentItem);
    _menu.addItem("", XCopyAction::none);
    _menu.addChild("Reset ESP", XCopyAction::resetESP, espParentItem);

    XCopyMenuItem *flashParentItem = _menu.addChild("Flash", XCopyAction::none, debugParentItem);
    XCopyMenuItem *dangerousParentitem = _menu.addChild("Dangerous", XCopyAction::none, flashParentItem);
    _menu.addChild("Erase Flash", XCopyAction::debuggingEraseFlash, dangerousParentitem);
    _menu.addChild("Erase Flash and Copy SD", XCopyAction::debuggingEraseCopy, dangerousParentitem);
    _menu.addChild("Erase Flash and Fault Find", XCopyAction::debuggingFaultFind, dangerousParentitem);
    _menu.addChild("Flash Memory Details", XCopyAction::debuggingFlashDetails, flashParentItem);
    _menu.addChild("Compare Flash to SD Card", XCopyAction::debuggingCompareFlashToSDCard, flashParentItem);
    _menu.addChild("Test Temp File", XCopyAction::debuggingTempFile, flashParentItem);
    _menu.addChild("Test Flash & SD Card", XCopyAction::debuggingSDFLash, flashParentItem);
    
    _menu.addItem("", XCopyAction::none);
    _menu.addItem("", XCopyAction::none);
    _menu.addItem("", XCopyAction::none);

    parentItem = _menu.addItem("Settings", XCopyAction::none);
    volumeMenuItem = _menu.addChild("Set Volume: " + String(_config->getVolume()), XCopyAction::setVolume, parentItem);

    XCopyMenuItem *timeParentItem = _menu.addChild("Time", XCopyAction::none, parentItem);
    _menu.addChild("Set Time from NTP", XCopyAction::showTime, timeParentItem);
    int timeZone = _config->getTimeZone();
    timeZoneMenuItem = _menu.addChild("Set Time Zone: " + String(timeZone >= 0 ? "+" : "") + String(timeZone), XCopyAction::setTimeZone, timeParentItem);

    XCopyMenuItem *diskParentItem = _menu.addChild("Disk", XCopyAction::none, parentItem);
    retryCountMenuItem = _menu.addChild("Set Retry Count: " + String(_config->getRetryCount()), XCopyAction::setRetry, diskParentItem);
    verifyMenuItem = _menu.addChild("Set Verify: " + (_config->getVerify() ? String("True") : String("False")), XCopyAction::setVerify, diskParentItem);
    diskDelayMenuItem = _menu.addChild("Set Disk Delay: " + String(_config->getDiskDelay()) + "ms", XCopyAction::setDiskDelay, diskParentItem);


    XCopyMenuItem *networkParentItem = _menu.addChild("Network", XCopyAction::none, parentItem);
    ssidMenuItem = _menu.addChild("SSID: " + _config->getSSID(), XCopyAction::setSSID, networkParentItem);
    passwordMenuItem = _menu.addChild("Password: " + _config->getPassword(), XCopyAction::setPassword, networkParentItem);

    _menu.addChild("", XCopyAction::none, parentItem);
    _menu.addChild("", XCopyAction::none, parentItem);
    _menu.addChild("Reset / Reboot", XCopyAction::resetDevice, parentItem);
    _menu.addChild("About XCopy", XCopyAction::about, parentItem);

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

    // The card-detect switch is still bouncing when the interrupt fires, so the
    // settle happens here rather than as a delay() inside the ISR.
    if (_playCardSound == true && millis() - _cardChangeMs >= 100) {
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
    case testDrive:
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
        xcopy->startFunction(XCopyAction::copyDiskToADF);
    }
    else if (command == "copyDiskToDisk") {
        xcopy->startFunction(XCopyAction::copyDiskToDisk);
    }
    else if (command == "copyDiskToFlash") {
        xcopy->startFunction(XCopyAction::copyDiskToFlash);
    }
    else if (command == "copyFlashToDisk") {
        xcopy->startFunction(XCopyAction::copyFlashToDisk);
    }
    else if (command == "testDisk") {
        xcopy->startFunction(XCopyAction::testDisk);
    }
    else if (command == "scanBlocks") {
        xcopy->startFunction(XCopyAction::scanBlocks);
    }
    else if (command == "formatDisk") {
        xcopy->startFunction(XCopyAction::formatDisk);
    }
    else if (command == "diskFlux") {
        xcopy->startFunction(XCopyAction::fluxDisk);
    }
    else if (command.startsWith("getSdFiles")) {
        String _param = "/";
        if (command.indexOf(",") > 0) {
            _param = command.substring(command.indexOf(",") + 1);
        }        
        xcopy->startFunction(XCopyAction::getSdFiles, _param);
    }
    else if (command.startsWith(XFER_CMD_SENDFILE)) {
        String path = command.substring(command.indexOf(",")+1);
        xcopy->sendFile(path);
    }
    else if (command.startsWith(XFER_CMD_GETFILE)) {
        // getFile,<path>,<size>[,<overwrite>]
        size_t filesize = 0;
        bool overwrite = false;
        String args = command.substring(command.indexOf(",") + 1);
        String path = args.substring(1, args.indexOf(","));      // strip leading '/'
        String rest = args.substring(args.indexOf(",") + 1);
        sscanf(rest.c_str(), "%zu", &filesize);                  // stops at the next comma
        if (rest.indexOf(",") > 0) {
            overwrite = (rest.substring(rest.indexOf(",") + 1).toInt() != 0);
        }

        xcopy->getFile(path, filesize, overwrite);
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
        size_t count = 0;
        // 1760 blocks packed one bit per block. Zeroed because the loops below and
        // writeBlocksToFile() always read all 220 entries, so a short message would
        // otherwise operate on stack garbage; bounded because the message arrives
        // over the websocket and could carry any number of fields.
        byte blocks[220];
        memset(blocks, 0, sizeof(blocks));
        while (_params.length() > 0 && count < sizeof(blocks)) {
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
        xcopy->startFunction(XCopyAction::diskSearch);
    }
    else if (command.startsWith("modSearch")) {
        xcopy->_searchText = command.substring(command.indexOf(",") + 1);
        xcopy->startFunction(XCopyAction::modSearch);
    }
    else if (command == "debuggingSerialPassThrough") {
            xcopy->startFunction(XCopyAction::debuggingSerialPassThrough);
    }
}

void XCopy::sendFile(String path) {
    setBusy(true);
    _transfer.sendFile(path);
    _menu.redraw();
    setBusy(false);
}

void XCopy::getFile(String path, size_t filesize, bool overwrite) {
    setBusy(true);
    _transfer.getFile(path, filesize, overwrite);
    _menu.redraw();
    setBusy(false);
}

void XCopy::sendBlock(int block) {
    setBusy(true);

    int track = floor(block / 11.0f);
    int sector = block % 11;

    _floppy.gotoLogicTrack(track);
    // int, not uint8_t: readTrack() signals failure with -1, which truncates to 255
    // in a uint8_t and makes the comparison below always true.
    int errors = _floppy.readTrack(true);
    if (errors != -1) {
        Track *track = _floppy.getTrack();
        struct Sector *aSec = (Sector *)&track[sector].sector;

        String webLine = "broadcast sendBlockDetails," + String(block) + "," + String(_floppy.getTrackInfo()) + "," + String(errors) 
            + "," + String(_floppy.getSectorCnt()) + "," + String(_floppy.getBitCount()) +  "," + String(aSec->format_type) + "," 
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

        _floppy.analyseHist(true);
        float time = .0f;
        String line = "broadcast sendBlockHist,";
        int *hist = _floppy.getHist();
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
        Log << F("bitCount: ") + String(_floppy.getBitCount()) + F(" (Read failed!)\r\n");
    }
    setBusy(false);
}

void XCopy::cardChange()
{
    // Runs in interrupt context: mark the audio sample to be played and record when,
    // so update() can wait out the switch bounce before sampling PIN_CARDDETECT.
    _cardChangeMs = millis();
    _playCardSound = true;
}

// void XCopy::copyBlock(byte blocks[]) {
//     _disk.writeBlocksToFile(blocks, _config->getRetryCount());
// }

void XCopy::startFunction(XCopyAction action, String param) {
    // Listing the card is answered on the spot; there is no state to enter.
    if (action == XCopyAction::getSdFiles) {
        setBusy(true);
        _esp->updateWebSdCardFiles(param);
        setBusy(false);
        return;
    }

    // Passthrough needs the setup navigateSelect() does around it (the banner, and
    // for programming mode the baud rate change), so go in through the menu.
    if (action == XCopyAction::debuggingSerialPassThrough) {
        _menu.setCurrentItem(action);
        navigateSelect();
        return;
    }

    setBusy(true);
    XCopyState state = stateForAction(action);
    _esp->setState(state);
    // Silently does nothing for an action with no menu item of its own, which is
    // what browsing the ADF directory wants.
    _menu.setCurrentItem(action);
    _xcopyState = state;
    _drawnOnce = false;
    _audio.playSelect(false);
    _graphics.clearScreen();
}

void XCopy::startCopyADFtoDisk(String path) {
    if (path == "") {
        startFunction(XCopyAction::directorySelection);
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

        // Compare against a copy. Teensy's String::toLowerCase()/toUpperCase()
        // modify in place and return a reference, so calling them on longName
        // permanently rewrote the name shown in the listing.
        String lowerName = item->longName;
        lowerName.toLowerCase();

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
        else if (lowerName.endsWith(".adf"))
        {
            _xcopyState = copyADFToDisk;
            _audio.playSelect(false);
            String itemname = item->longName;
            if (item->source == _sdCard)
                itemname = _directory.getCurrentPath() + itemname;
            else
                itemname.toUpperCase();
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

        switch (item->action)
        {
        case XCopyAction::debuggingTempFile:
        {
            setBusy(true);
            _xcopyState = debuggingTempFile;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingSDFLash:
        {
            setBusy(true);
            _xcopyState = debuggingSDFLash;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingEraseCopy:
        {
            setBusy(true);
            _xcopyState = debuggingEraseCopy;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingFaultFind:
        {
            setBusy(true);
            _xcopyState = debuggingFaultFind;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingEraseFlash:
        {
            setBusy(true);
            _xcopyState = debuggingEraseFlash;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingCompareFlashToSDCard:
        {
            setBusy(true);
            _xcopyState = debuggingCompareFlashToSDCard;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingFlashDetails:
        {
            setBusy(true);
            _xcopyState = debuggingFlashDetails;
            _audio.playSelect(false);
            break;
        }
        case XCopyAction::debuggingSerialPassThrough:
        {
            setBusy(true);
            _xcopyState = debuggingSerialPassThrough;
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 0, ST7735_GREEN, "ESP Passthrough Mode", true);
            break;
        }
        case XCopyAction::debuggingSerialPassThroughProg:
        {
            setBusy(true);

            _xcopyState = debuggingSerialPassThrough; // set as passthrough now ESP is in programming mode
            _espProgMode = true;
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 0, ST7735_GREEN, "ESP Programming Mode", true);

            // esptool has to talk to the ESP bootloader through this passthrough, so
            // Serial1 must run at a rate esptool is happy with -- independent of the
            // (much faster) rate the data link uses. Restored when passthrough exits.
            ESPSerial.begin(ESPProgBaudRate);

            _esp->progMode();
            break;
        }
        case XCopyAction::resetESP:
        {
            setBusy(true);
            _audio.playSelect(false);

            _esp->reset();

            setBusy(false);

            // Resetting the ESP is over by the time this returns. It used to park
            // _xcopyState on a "resetESP" state first, which nothing ever observed
            // before the line below put it back.
            _xcopyState = menus;
            break;
        }
        case XCopyAction::showTime:
        {
            setBusy(true);
            _xcopyState = showTime;
            _audio.playSelect(false);
            _graphics.clearScreen();
            _graphics.drawText(0, 35, ST7735_YELLOW, "    Updating Time via NTP", true);
            _graphics.drawText(0, 35, ST7735_YELLOW, "          Updated Time", true);
            refreshTimeNtp();
            break;
        }
        case XCopyAction::about:
        {
            setBusy(true);
            _xcopyState = about;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
            break;
        }
        case XCopyAction::copyADFToDisk:
        {
            startCopyADFtoDisk();
            break;
        }
        case XCopyAction::copyDiskToADF:
        {
            startFunction(XCopyAction::copyDiskToADF);
            break;
        }
        case XCopyAction::copyDiskToDisk:
        {
            startFunction(XCopyAction::copyDiskToDisk);
            break;
        }
        case XCopyAction::copyDiskToFlash:
        {
            startFunction(XCopyAction::copyDiskToFlash);
            break;
        }
        case XCopyAction::copyFlashToDisk:
        {
            startFunction(XCopyAction::copyFlashToDisk);
            break;
        }
        case XCopyAction::testDisk:
        {
            startFunction(XCopyAction::testDisk);
            break;
        }
        case XCopyAction::scanBlocks:
        {
            startFunction(XCopyAction::scanBlocks);
            break;
        }
        case XCopyAction::formatDisk:
        {
            startFunction(XCopyAction::formatDisk);
            break;
        }
        case XCopyAction::fluxDisk:
        {
            startFunction(XCopyAction::fluxDisk);
            break;
        }
        case XCopyAction::setVerify:
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
            break;
        }
        case XCopyAction::setRetry:
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
            break;
        }
        case XCopyAction::setVolume:
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
            break;
        }
        case XCopyAction::setSSID:
        {
            setBusy(true);
            _audio.playSelect(false);

            ssidMenuItem->text = "SSID: " + _config->getSSID();

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
            break;
        }
        case XCopyAction::setPassword:
        {
            setBusy(true);
            _audio.playSelect(false);

            passwordMenuItem->text = "Password: " + _config->getPassword();

            setBusy(false);
            // redraw menu
            _xcopyState = menus;
            break;
        }
        case XCopyAction::testDrive:
        {
            setBusy(true);
            _xcopyState = testDrive;
            _drawnOnce = false;
            _audio.playSelect(false);
            _graphics.clearScreen();
            break;
        }
        case XCopyAction::setDiskDelay:
        {
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
            break;
        }
        case XCopyAction::setTimeZone:
        {
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
            break;
        }
        case XCopyAction::resetDevice:
        {
            Serial << "Resetting ...";
            pinMode(28, OUTPUT);
            pinMode(28, OUTPUT_OPENDRAIN);
            Serial << " Looks like pin 28 has not been jumpered to the RST pad on your Teensy 3.2\r\n";
            break;
        }

        // Headings, spacers and anything with no menu item of its own.
        default:
            break;
        }
    }
}

void XCopy::processState()
{
    /*
       The if-chain this replaced fell through on purpose: a branch that finished by
       setting _xcopyState -- a debug operation returning to the menus, or the menus
       branch turning itself into idle -- was picked up by a later if in the same
       pass. A switch dispatches once, so the loop settles the state the same way
       rather than leaving the redraw a lap behind.
    */
    XCopyState entered;
    do
    {
        entered = _xcopyState;

        switch (_xcopyState)
        {
        case debuggingTempFile:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugCompareTempFile();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingSDFLash:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugTestFlashSD();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingEraseCopy:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugEraseCopyCompare();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingFaultFind:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugFaultFind();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingEraseFlash:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugEraseFlash();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingCompareFlashToSDCard:
        {
            _graphics.clearScreen();
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugCompare();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingFlashDetails:
        {
            XCopyDebug *_debug = new XCopyDebug(&_graphics, &_audio, PIN_FLASHCS, PIN_CARDDETECT);
            _debug->debugFlashDetails();
            delete _debug;

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case debuggingSerialPassThrough:
        {
            // In programming mode the ESP sits in its ROM bootloader and Serial1 runs at
            // ESPProgBaudRate, so it cannot answer the echo commands -- and sending them
            // pushes stray bytes at esptool's sync.
            if (!_espProgMode)
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

            if (_espProgMode)
            {
                // Serial1 is still at the programming rate. Restore the data-link rate
                // first: the old order sent setEcho(false) at ESPProgBaudRate, which the
                // ESP was never going to hear.
                ESPSerial.begin(ESPBaudRate);
                _espProgMode = false;
            }
            else
            {
                _esp->setEcho(false);
            }
            _cancelOperation = false;
            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case menus:
        {
            _graphics.clearScreen();
            _graphics.drawHeader();
            _menu.drawMenu(_menu.getRoot());
            _xcopyState = idle;
            break;
        }
        case showTime:
        {
            if (_prevSeconds != second())
            {
                char buffer[32];
                sprintf(buffer, "    %02d:%02d:%02d %02d/%02d/%04d", hour(), minute(), second(), day(), month(), year());
                _graphics.drawText(0, 55, ST7735_YELLOW, buffer, true);

                _prevSeconds = second();
            }
            break;
        }
        case copyDiskToADF:
        {
            if (_drawnOnce == false)
            {
                // _config = new XCopyConfig();
                _disk.diskToADF("", _config->getVerify(), _config->getRetryCount(), _sdCard);
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case copyDiskToFlash:
        {
            if (_drawnOnce == false)
            {
                // _config = new XCopyConfig();
                _disk.diskToADF("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case copyDiskToDisk:
        {
            if (_drawnOnce == false)
            {
                // _config = new XCopyConfig();
                _disk.diskToDisk(_config->getVerify(), _config->getRetryCount());
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case copyFlashToDisk:
        {
            if (_drawnOnce == false)
            {
                // _config = new XCopyConfig();
                _disk.adfToDisk("DISKCOPY.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case testDisk:
        {
            if (_drawnOnce == false)
            {
                _disk.testDiskette(_config->getRetryCount());
                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case scanBlocks:
        {
            if (_drawnOnce == false)
            {
                _disk.scanEmptyBlocks(_config->getRetryCount());
                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case fluxDisk:
        {
            if (_drawnOnce == false)
            {
                _disk.diskFlux();

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case formatDisk:
        {
            if (_drawnOnce == false)
            {
                // _config = new XCopyConfig();
                _disk.adfToDisk("BLANK.TMP", _config->getVerify(), _config->getRetryCount(), _flashMemory);
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case directorySelection:
        {
            if (_drawnOnce == false)
            {
                _graphics.clearScreen();
                _directory.drawDirectory();
                _drawnOnce = true;
            }
            break;
        }
        case testDrive:
        {
            if (_drawnOnce == false)
            {
                XCopyDriveTest *driveTest = new XCopyDriveTest();
                driveTest->begin(&_graphics, &_audio, _esp, &_floppy);
                driveTest->draw();
                // Was while (1==1): no exit, so the two lines below were unreachable and
                // the drive test could only be left by resetting the board.
                while (!_cancelOperation) {
                    driveTest->update();
                }
                _cancelOperation = false;
                delete driveTest;
                setBusy(false);
                _drawnOnce = true;
                _xcopyState = menus;
            }
            break;
        }
        case diskSearch:
        {
            if (_drawnOnce == false) {
                _disk.asciiSearch(_searchText, _config->getRetryCount());
                _searchText = "";
                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case modSearch:
        {
            if (_drawnOnce == false) {
                _disk.modSearch(_config->getRetryCount());
                _searchText = "";
                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case about:
        {
            if (_drawnOnce == false)
            {
                _graphics.clearScreen();
                _graphics.drawText(0, 55, ST7735_WHITE, "     (c)2019 iTeC/crAss");
                _graphics.drawText(0, 65, ST7735_GREEN, "           " + String(XCOPYVERSION));
                _graphics.drawText(0, 75, ST7735_YELLOW, "  Insert Demo Effect Here");

                _drawnOnce = true;
            }
            break;
        }
        case idle:
        {
            setBusy(false);
            break;
        }

        default:
            break;
        }
    } while (_xcopyState != entered);
}