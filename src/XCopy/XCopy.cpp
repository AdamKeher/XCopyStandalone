#include "XCopy.h"
#include "XCopyFixed.h"

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

    /*
       A solid block of purple, so all three rows have to come out exactly the same
       width or it has a bite taken out of the corner. The middle row carries the
       version string, and that is not a fixed length: the gap after it was counted
       by hand for a version two characters longer than the current one, and the
       block has been two short on the right ever since. Measured now, so it cannot
       drift again the next time the format changes.
    */
    static const uint8_t bannerWidth = 74;
    static const char *const bannerLeft = " X-Copy Standalone ";
    static const char *const bannerRight = "(c)2022 Adam Keher ";

    char blank[bannerWidth + 1];
    memset(blank, ' ', bannerWidth);
    blank[bannerWidth] = 0;

    /*
       Written into a row of spaces rather than assembled with a padding format:
       the title and version go in from the left, the copyright is placed against
       the right hand edge, and the row is the width it is regardless. A version
       long enough to reach across would overlap the copyright rather than push the
       row wider, which is the right way round - the block staying square is what
       matters here, and a version string that long is a different problem.
    */
    char banner[bannerWidth + 1];
    memcpy(banner, blank, sizeof(blank));
    memcpy(banner, bannerLeft, strlen(bannerLeft));
    memcpy(banner + strlen(bannerLeft), XCOPYVERSION, strlen(XCOPYVERSION));
    memcpy(banner + bannerWidth - strlen(bannerRight), bannerRight, strlen(bannerRight));

    Log << XCopyConsole::clearscreen() << XCopyConsole::home() << XCopyConsole::background_purple() << XCopyConsole::high_yellow();
    Log << blank << F("\r\n");
    Log << banner << F("\r\n");
    Log << blank << F("\r\n");
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
    _diskInfo.begin(&_graphics, &_audio, _esp, &_floppy);
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
        _graphics.drawText(47, 75, ST7735_RED, "Floppy Cable", true);
        _graphics.drawText(45, 85, ST7735_RED, "Upside Down!", true);
        _graphics.drawText(44, 95, ST7735_RED, "Fix and Reset", true);
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
                Log << F("Signal strength: ") << _esp->signal() << F("\r\n");
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
    _menu.addChild("Copy Disk  to SCP", XCopyAction::copyDiskToSCP, parentItem);
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
    _menu.addChild("Head Calibration", XCopyAction::headCalibration, parentItem);
    _menu.addChild("Drive Toolkit", XCopyAction::driveToolkit, parentItem);

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
    volumeMenuItem = _menu.addChild("Set Volume: " + twoDecimals(_config->getVolume()), XCopyAction::setVolume, parentItem);

    XCopyMenuItem *timeParentItem = _menu.addChild("Time", XCopyAction::none, parentItem);
    _menu.addChild("Set Time from NTP", XCopyAction::showTime, timeParentItem);
    int timeZone = _config->getTimeZone();
    timeZoneMenuItem = _menu.addChild("Set Time Zone: " + String(timeZone >= 0 ? "+" : "") + String(timeZone), XCopyAction::setTimeZone, timeParentItem);

    XCopyMenuItem *diskParentItem = _menu.addChild("Disk", XCopyAction::none, parentItem);
    retryCountMenuItem = _menu.addChild("Set Retry Count: " + String(_config->getRetryCount()), XCopyAction::setRetry, diskParentItem);
    verifyMenuItem = _menu.addChild("Set Verify: " + (_config->getVerify() ? String("True") : String("False")), XCopyAction::setVerify, diskParentItem);
    diskDelayMenuItem = _menu.addChild("Set Disk Delay: " + String(_config->getDiskDelay()) + "ms", XCopyAction::setDiskDelay, diskParentItem);
    scpRevolutionsMenuItem = _menu.addChild("Set SCP Revs: " + String(_config->getScpRevolutions()), XCopyAction::setScpRevolutions, diskParentItem);
    scpCylindersMenuItem = _menu.addChild("Set SCP Cyls: 0-" + String(_config->getScpEndCylinder()), XCopyAction::setScpCylinders, diskParentItem);


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

    drawMenuScreen();
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

/*
   The top level menu, plus the firmware version in the bottom right corner.

   Right aligned against the panel width rather than a fixed column, so it stays
   in the corner whatever the version string says and whichever landscape
   orientation the panel is in.

   The width has to be MEASURED, not assumed. defaultFont is
   RLE_proportional - every glyph is its own width - so a fixed pixels-per-
   character guess is wrong by however much the particular characters differ from
   it, and guessing low pushes the last character off the right hand edge. The
   driver keeps its string measuring private, but the cursor is readable: draw the
   string in the background colour, where it cannot be seen, and read back how far
   the cursor moved.

   That throwaway pass happens BEFORE the menu is drawn, on purpose. It writes
   black pixels across the bottom row, which is also where the last menu entry
   sits, and drawMenu() then paints that entry back over the top.
*/
uint8_t XCopy::versionRow()
{
    const uint16_t panelH = _graphics.getTFT()->height();
    return (panelH > 10) ? (uint8_t)(panelH - 10) : 0;
}

/*
   Where the version string starts, worked out once and remembered.

   Measuring is the expensive half and it is also the destructive half: it prints
   the string in black across the bottom row and reads the cursor back, because the
   font is RLE_proportional and the driver keeps its string measuring private. Every
   character is its own width, so a fixed pixels-per-character guess is wrong by
   however much the particular characters differ from it, and guessing low pushes
   the last character off the right hand edge.

   It has to happen BEFORE the menu is drawn: those black pixels land on the bottom
   row, which is also where the last entry of a long level sits, and drawMenu()
   paints that entry back over the top. The answer cannot change while the firmware
   is running, so after the first screen this costs nothing and touches nothing.
*/
void XCopy::measureVersion()
{
    if (_versionX != kVersionXUnknown)
        return;

    TFT_ST7735 *tft = _graphics.getTFT();
    const String version = XCOPYVERSION;
    const uint16_t panelW = tft->width();

    int16_t startX = 0, startY = 0, endX = 0, endY = 0;
    tft->setCursor(0, versionRow());
    tft->getCursor(startX, startY);
    tft->setTextColor(ST7735_BLACK);
    tft->print(version);
    tft->getCursor(endX, endY);

    const int16_t measured = endX - startX;
    // Fall back to a deliberately generous estimate if the cursor told us nothing,
    // since erring wide only costs a few pixels of margin while erring narrow
    // truncates.
    const uint16_t textW =
        (measured > 0) ? (uint16_t)measured : (uint16_t)(version.length() * 8);

    _versionX = (panelW > textW + 4) ? (uint8_t)(panelW - textW - 4) : 0;
}

void XCopy::drawVersion()
{
    _graphics.drawText(_versionX, versionRow(), ST7735_MAGENTA, XCOPYVERSION);
}

void XCopy::drawMenuScreen()
{
    measureVersion();
    _menu.drawMenu(_menu.getRoot());
    // After the list: the bottom entry of a long level and this share a line, and
    // whichever is drawn last is the one that survives the overlap.
    drawVersion();
}

/*
   Moving the cursor is two entries changing colour, not a new screen.

   drawMenuScreen() was called for every click of the stick, which reprinted every
   entry in the level over an identical copy of itself and re-measured the version
   string to put it back on top. redrawSelection() touches the two rows that
   actually changed and says so; when it cannot - the list itself moved, not the
   cursor in it - the full draw is still there behind it.
*/
void XCopy::drawMenuSelection(XCopyMenuItem *previous)
{
    // Nothing has drawn a full menu screen yet, so there is no measurement to reuse
    // and nowhere safe to take one. Draw the lot.
    if (_versionX == kVersionXUnknown || !_menu.redrawSelection(previous))
    {
        drawMenuScreen();
        return;
    }

    // The bottom entry of a long level runs under the version string, so it is put
    // back over whichever of the two rows was just repainted.
    drawVersion();
}

/**
 * @brief Restart the Teensy. Does not return.
 *
 * Two routes, tried in that order. Pin 28 pulled low resets the board exactly the
 * way the button does, but only on a Teensy that has been jumpered to the RST pad,
 * which is an optional modification and most boards do not have it. All the old
 * code did was pulse the pin and then print a line saying that if you are reading
 * this, it was not wired.
 *
 * So if the board is still running a moment later, the core is asked to reset
 * itself: SYSRESETREQ, the vector key in the top half of AIRCR and the request in
 * the bottom, which needs no wire at all. The difference between the two is what
 * else on the board sees the reset - nothing here hangs off the RST pad, and the
 * ESP has its own reset line and is deliberately left running, so the browser
 * keeps its connection and simply watches the device go quiet for a second.
 */
void XCopy::reboot()
{
    // Said before the reset rather than after it: whichever route works, nothing
    // below that point runs. The delay is for the ESP, which has to get the line
    // out over the serial link and into the websocket before the far end stops
    // talking to it.
    Log << F("Rebooting ...\r\n");
    if (_esp != nullptr)
        _esp->setStatus("Rebooting ...");
    Serial.flush();
    delay(50);

    // The jumpered route.
    pinMode(PIN_TEENSYRESETPIN, OUTPUT);
    pinMode(PIN_TEENSYRESETPIN, OUTPUT_OPENDRAIN);
    digitalWriteFast(PIN_TEENSYRESETPIN, LOW);
    delay(10);

    // Not jumpered.
    SCB_AIRCR = 0x05FA0004;

    while (true)
    {
    }
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
    case liveStream:
        _cancelOperation = true;
        break;
    /*
       headCalibration is deliberately absent.

       On PCB v2 ISR_CANCEL is wired to the LEFT and UP joystick pins, so a case
       here would spend two of the five inputs the calibration screen needs. With
       no case the interrupt is a harmless no-op and all five stay usable; leaving
       the screen is navigateLeft(), which calls exitHeadCalibration() properly.

       The browser Cancel button pulses that same pin, so it does nothing here
       either - which is why the web panel has its own Exit button sending
       headCalExit rather than borrowing the shared one.
    */
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
    /*
       The analyser owns its own cancel flag rather than borrowing _disk's: it is
       not an XCopyDisk operation, and a survey has to put the sync census bound
       back before it returns, which only its own loop can do.
    */
    case analyseDisk:
        _diskInfo.cancelOperation();
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

/*
   The single way out of a head calibration session.

   Every surface funnels through here - the joystick, the console key handler and
   the browser - because leaving the state is not enough on its own: the drive is
   spinning, the index interrupt is attached and the console is in raw key mode,
   and all three have to be put back.
*/
void XCopy::exitHeadCalibration()
{
    if (_xcopyState != headCalibration)
        return;

    _command->setRawKeys(nullptr, nullptr);
    _headCal.panelEnd();
    _headCal.end();
    _headCal.sendClosed();
    _audio.playBack(false);
    setBusy(false);
    _xcopyState = menus;
    _command->printPrompt();
}

void XCopy::onHeadCalKey(void *obj, char key)
{
    XCopy *xcopy = (XCopy *)obj;

    if (!xcopy->_headCal.handleKey(key))
        xcopy->exitHeadCalibration();
}

/*
   The single way out of a drive toolkit session.

   Every surface funnels through here - the joystick, the console key handler and
   the browser - because leaving the state is not enough on its own. The toolkit
   is holding select, motor and density wherever the operator left them, the index
   interrupt is attached and the console is in raw key mode, and a drive left
   selected with its motor running is how the next operation inherits a fault that
   has nothing to do with it. end() releases the outputs; this puts back the rest.
*/
void XCopy::exitDriveToolkit()
{
    if (_xcopyState != driveToolkit)
        return;

    _command->setRawKeys(nullptr, nullptr);
    _driveToolkit.panelEnd();
    _driveToolkit.end();
    _driveToolkit.sendClosed();
    _audio.playBack(false);
    setBusy(false);
    _xcopyState = menus;
    _command->printPrompt();
}

void XCopy::onDriveToolkitKey(void *obj, char key)
{
    XCopy *xcopy = (XCopy *)obj;

    if (!xcopy->_driveToolkit.handleKey(key))
        xcopy->exitDriveToolkit();
}

void XCopy::onWebCommand(void* obj, const String command)
{
    // Log << "DEBUG::ESPCALLBACK::(" << command << ")\r\n";
    XCopy* xcopy = (XCopy*)obj;
    
    // "copyDiskToADF" alone lets diskToADF() generate the filename; the serial
    // "readadf <filename>" form appends the destination path after a comma.
    if (command.startsWith("copyDiskToADF")) {
        String path = "";
        if (command.indexOf(",") > 0) {
            path = command.substring(command.indexOf(",") + 1);
        }
        xcopy->startFunction(XCopyAction::copyDiskToADF, path);
    }
    // Same shape as copyDiskToADF: bare from the web UI, with a destination path and
    // optionally "<first>-<last>" and a revolution count from "readscp".
    else if (command.startsWith("copyDiskToSCP")) {
        String param = "";
        if (command.indexOf(",") > 0) {
            param = command.substring(command.indexOf(",") + 1);
        }
        xcopy->startFunction(XCopyAction::copyDiskToSCP, param);
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
    /*
       Not through startFunction(). Every other command here queues an action for the
       state machine to pick up on the next pass of the loop, and there is no next
       pass after this one - so it is called where it can still say something first.
       Sent by the browser's Tools menu and by the console's "reboot".
    */
    else if (command == "rebootDevice") {
        xcopy->reboot();
    }
    // Only ever sent by the serial console's "live" command. The web UI has no button
    // for it: a live session stops servicing the ESP link for its whole duration, so
    // starting one from the browser would strand the browser that asked for it.
    else if (command == "liveStream") {
        xcopy->startFunction(XCopyAction::liveStream);
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
    /*
       Disk info. Two sources, one state.

       "<first>-<last>,<side>" for the drive, and the same with a path appended for
       an image. Parsed here rather than carried through startFunction()'s single
       String because the survey wants three numbers and sometimes a path, and
       stuffing those back into one string only to take them apart again in
       processState() would put the wire format in two places.
    */
    else if (command.startsWith("diskInfoScan") || command.startsWith("diskInfoFile")) {
        const bool fromFile = command.startsWith("diskInfoFile");
        String param = "";
        if (command.indexOf(",") > 0) param = command.substring(command.indexOf(",") + 1);

        xcopy->_diskInfoFirst = 0;
        xcopy->_diskInfoLast = MAX_CYLINDERS - 1;
        xcopy->_diskInfoSide = -1;
        xcopy->_diskInfoPath = "";

        const int dash = param.indexOf('-');
        const int comma = param.indexOf(',');
        if (dash > 0 && comma > dash) {
            xcopy->_diskInfoFirst = (uint8_t)param.substring(0, dash).toInt();
            xcopy->_diskInfoLast = (uint8_t)param.substring(dash + 1, comma).toInt();

            // The path may itself contain a comma, so the side is read up to the
            // next one and everything after it is the path, untouched.
            const int comma2 = param.indexOf(',', comma + 1);
            if (comma2 > 0) {
                xcopy->_diskInfoSide = (int8_t)param.substring(comma + 1, comma2).toInt();
                xcopy->_diskInfoPath = param.substring(comma2 + 1);
            }
            else {
                xcopy->_diskInfoSide = (int8_t)param.substring(comma + 1).toInt();
            }
        }

        xcopy->startFunction(fromFile ? XCopyAction::analyseScp : XCopyAction::analyseDisk);
    }
    // Head calibration. Only the start is accepted from any state; everything
    // else is ignored unless a session is actually running, so a browser tab left
    // open on the panel cannot reach into the drive during a disk copy.
    else if (command.startsWith("headCalibration")) {
        String param = "";
        if (command.indexOf(",") > 0) {
            param = command.substring(command.indexOf(",") + 1);
        }
        xcopy->startFunction(XCopyAction::headCalibration, param);
    }
    else if (command.startsWith("headCal") && xcopy->_xcopyState == headCalibration) {
        String param = "";
        if (command.indexOf(",") > 0) {
            param = command.substring(command.indexOf(",") + 1);
        }

        if (command.startsWith("headCalCyl")) {
            xcopy->_headCal.setCylinder(param.toInt());
        }
        else if (command.startsWith("headCalNudge")) {
            xcopy->_headCal.nudgeCylinder(param.toInt());
        }
        else if (command.startsWith("headCalHead")) {
            xcopy->_headCal.setHead((XCopyHeadCalibration::HeadSel)param.toInt());
        }
        else if (command.startsWith("headCalAuto")) {
            xcopy->_headCal.setAutoReseek(param.toInt() != 0);
        }
        else if (command.startsWith("headCalSound")) {
            xcopy->_headCal.setSound(param.toInt() != 0);
        }
        else if (command.startsWith("headCalPause")) {
            xcopy->_headCal.setPaused(param.toInt() != 0);
        }
        else if (command.startsWith("headCalStep")) {
            xcopy->_headCal.setStepSize((uint8_t)param.toInt());
        }
        else if (command == "headCalReseek") {
            xcopy->_headCal.reseek();
        }
        else if (command == "headCalExit") {
            xcopy->exitHeadCalibration();
        }
    }
    // Drive toolkit. Only the start is accepted from any state; every control is
    // ignored unless a session is actually running, so a browser tab left open on
    // the panel cannot drive the interface lines during a disk copy.
    else if (command == "driveToolkit") {
        xcopy->startFunction(XCopyAction::driveToolkit);
    }
    else if (command.startsWith("dt") && xcopy->_xcopyState == driveToolkit) {
        String param = "";
        if (command.indexOf(",") > 0) {
            param = command.substring(command.indexOf(",") + 1);
        }

        if (command.startsWith("dtSel")) {
            xcopy->_driveToolkit.setSelect(param.toInt() != 0);
        }
        else if (command.startsWith("dtMot")) {
            xcopy->_driveToolkit.setMotor(param.toInt() != 0);
        }
        else if (command.startsWith("dtDir")) {
            xcopy->_driveToolkit.setDirection(param.toInt() != 0);
        }
        else if (command.startsWith("dtSide")) {
            xcopy->_driveToolkit.setSideUpper(param.toInt() == 0);
        }
        else if (command.startsWith("dtDens")) {
            xcopy->_driveToolkit.setDensity(param.toInt() != 0);
        }
        else if (command.startsWith("dtSticky")) {
            xcopy->_driveToolkit.setSticky(param.toInt() != 0);
        }
        else if (command.startsWith("dtCyl")) {
            xcopy->_driveToolkit.seekCylinder(param.toInt());
        }
        else if (command.startsWith("dtNudge")) {
            xcopy->_driveToolkit.nudgeCylinder(param.toInt());
        }
        else if (command == "dtStep") {
            xcopy->_driveToolkit.pulseStep();
        }
        else if (command == "dtRecal") {
            xcopy->_driveToolkit.recalibrate();
        }
        else if (command == "dtClear") {
            xcopy->_driveToolkit.clearCounters();
        }
        else if (command == "dtSafe") {
            xcopy->_driveToolkit.releaseOutputs();
        }
        else if (command == "dtExit") {
            xcopy->exitDriveToolkit();
            return;
        }

        // Every control above changes something the three surfaces render, and the
        // browser holds no state of its own - so it is told the outcome here rather
        // than waiting up to a refresh interval to be told by update().
        xcopy->_driveToolkit.sendState();
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
                line += twoDecimals(time) + "|" + String(hist[i]) + "&";
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

    // Consumed by processState()'s copyDiskToADF case. Always assigned so a path
    // left over from an earlier read cannot leak into a menu driven copy.
    if (action == XCopyAction::copyDiskToADF) {
        _adfFilePath = param;
    }

    /*
       Same for the SCP capture, which also carries a cylinder range and a revolution
       count. The payload is "<first>-<last>,<revs>,<path>" or empty, deliberately
       with the path last: a filename may legitimately contain a comma, the two
       numeric fields never can, so only this order parses unambiguously.

       Zero means "not given" and processState() falls back to the saved setting.
       Everything is reset first, so nothing from an earlier run leaks into a capture
       started from the menu.
    */
    if (action == XCopyAction::copyDiskToSCP) {
        _scpFilePath = "";
        _scpStartCylinder = 0;
        _scpEndCylinder = 0;
        _scpRevolutions = 0;
        _scpRangeGiven = false;

        if (param != "") {
            int firstComma = param.indexOf(",");
            String range = firstComma < 0 ? param : param.substring(0, firstComma);
            String rest = firstComma < 0 ? "" : param.substring(firstComma + 1);

            int dash = range.indexOf("-");
            if (dash > 0) {
                _scpStartCylinder = (uint8_t)range.substring(0, dash).toInt();
                _scpEndCylinder = (uint8_t)range.substring(dash + 1).toInt();
                _scpRangeGiven = true;
            }

            int secondComma = rest.indexOf(",");
            if (secondComma >= 0) {
                _scpRevolutions = (uint8_t)rest.substring(0, secondComma).toInt();
                _scpFilePath = rest.substring(secondComma + 1);
            }
            else {
                _scpRevolutions = (uint8_t)rest.toInt();
            }
        }
    }

    /*
       A calibration session owns the drive, the index interrupt and the console
       key mode. Starting anything else - from the menu, the console or the web -
       has to close it down rather than just overwrite the state underneath it.
    */
    if (_xcopyState == headCalibration && action != XCopyAction::headCalibration) {
        exitHeadCalibration();
    }

    /*
       And a toolkit session owns all of the same things, plus the state of every
       output line. Leaving it by starting something else has to go through the
       exit that releases them, or the next operation inherits a drive with select
       and motor asserted behind its back.
    */
    if (_xcopyState == driveToolkit && action != XCopyAction::driveToolkit) {
        exitDriveToolkit();
    }

    // Consumed by processState() when the session opens. Always assigned, so a
    // cylinder given on one run cannot leak into the next.
    if (action == XCopyAction::headCalibration) {
        _headCalCylinder = param == "" ? XCopyHeadCalibration::kDefaultCylinder
                                       : (uint8_t)param.toInt();
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

void XCopy::navigateDown(bool repeat)
{
    if (_xcopyState == headCalibration)
    {
        // Not on a repeat. Up and down are the field adjusters here, so holding the
        // stick would cycle the head selection or toggle the sound over and over
        // rather than walk down a list.
        if (repeat)
            return;
        _headCal.adjustField(-1);
        _audio.playClick(false);
        return;
    }

    if (_xcopyState == menus || _xcopyState == idle)
    {
        XCopyMenuItem *previous = _menu.getCurrentItem();
        if (_menu.down())
        {
            _audio.playClick(false);
            drawMenuSelection(previous);
        }
    }

    if (_xcopyState == directorySelection)
    {
        XCopyDirectoryEntry *previous = _directory.getCurrentItem();
        const uint16_t previousTop = _directory.windowTop();
        if (_directory.down())
        {
            _audio.playClick(false);
            if (!_directory.redrawSelection(previous, previousTop))
                _directory.drawDirectory();
        }
    }
}

void XCopy::navigateUp(bool repeat)
{
    if (_xcopyState == headCalibration)
    {
        // See navigateDown().
        if (repeat)
            return;
        _headCal.adjustField(+1);
        _audio.playClick(false);
        return;
    }

    if (_xcopyState == menus || _xcopyState == idle)
    {
        XCopyMenuItem *previous = _menu.getCurrentItem();
        if (_menu.up())
        {
            _audio.playClick(false);
            drawMenuSelection(previous);
        }
    }

    if (_xcopyState == directorySelection)
    {
        XCopyDirectoryEntry *previous = _directory.getCurrentItem();
        const uint16_t previousTop = _directory.windowTop();
        if (_directory.up())
        {
            _audio.playClick(false);
            if (!_directory.redrawSelection(previous, previousTop))
                _directory.drawDirectory();
        }
    }
}

void XCopy::navigateLeft()
{
    // Before the generic "any other state goes back to the menus" fallback at the
    // bottom, which would drop the state and leave the drive spinning.
    if (_xcopyState == headCalibration)
    {
        exitHeadCalibration();
        return;
    }

    // The toolkit's screen is display only, so left is the one joystick control
    // it has - and it must be this and not the generic fallback below, which
    // would leave select and motor asserted.
    if (_xcopyState == driveToolkit)
    {
        exitDriveToolkit();
        return;
    }

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
        _directory.drawDirectory();

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
            _directory.drawDirectory();

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
    // The one place right and push differ: on the calibration screen right is the
    // re-seek, which is ATK's F1 and the control an operator reaches for most.
    if (_xcopyState == headCalibration)
    {
        _headCal.reseek();
        _audio.playClick(false);
        return;
    }

    navigateSelect();
}

void XCopy::navigateSelect()
{
    if (_xcopyState == headCalibration)
    {
        _headCal.nextField();
        _audio.playClick(false);
        return;
    }

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
            _directory.drawDirectory();
        }
        else if (item->isDirectory() && item->source == _flashMemory)
        {
            _audio.playBack(false);
            _directory.getDirectoryFlash(false, &_disk, ".adf");
            _directory.drawDirectory();
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
            // setCurrentItem() takes _root down to the child level with it.
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

            volumeMenuItem->text = "Set Volume: " + twoDecimals(_config->getVolume());
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
        case XCopyAction::headCalibration:
        {
            // Through startFunction() rather than setting the state here, which is
            // what the drive test used to do: that skipped _esp->setState() and so
            // the browser was never told the machine had entered the screen.
            startFunction(XCopyAction::headCalibration);
            break;
        }
        case XCopyAction::driveToolkit:
        {
            startFunction(XCopyAction::driveToolkit);
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
        case XCopyAction::setScpRevolutions:
        {
            setBusy(true);

            // 1..SCP_MAX_REVS, wrapping. More revolutions catch weak bits that differ
            // between reads, at a proportional cost in time and card space.
            uint8_t revolutions = _config->getScpRevolutions();
            revolutions++;
            if (revolutions > SCP_MAX_REVS)
                revolutions = 1;
            _config->setScpRevolutions(revolutions);

            scpRevolutionsMenuItem->text = "Set SCP Revs: " + String(revolutions);
            _config->writeConfig();

            _audio.playSelect(false);

            setBusy(false);
            _xcopyState = menus;
            break;
        }
        case XCopyAction::setScpCylinders:
        {
            setBusy(true);

            // 79 is AmigaDOS. The steps past it are where long track protections sit,
            // and 83 is the last cylinder SCP can address - but a drive that cannot
            // reach them will simply fail those tracks, so this stays opt in.
            uint8_t cylinder = _config->getScpEndCylinder();
            switch (cylinder)
            {
            case 79: cylinder = 81; break;
            case 81: cylinder = 83; break;
            default: cylinder = 79; break;
            }
            _config->setScpEndCylinder(cylinder);

            scpCylindersMenuItem->text = "Set SCP Cyls: 0-" + String(cylinder);
            _config->writeConfig();

            _audio.playSelect(false);

            setBusy(false);
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
            reboot();
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
        case liveStream:
        {
            /*
               A live streaming session. XCopyLive::run() blocks for the whole of it and
               nothing else is serviced while it does - no TFT redraw, no ESP poll, no
               audio. That is the point rather than an oversight: the host is paying for
               the timing, and every one of those is put back on the way out.

               The session's buffers are in bss, not in this object - see the note above
               liveTx in XCopyLive.h for why that is not negotiable on this part. What
               is left here is a few hundred bytes, but it is still checked: a null new
               would fault inside run() before the session had printed a single byte,
               which is a fault with no symptom to debug from.
            */
            XCopyLive *live = new XCopyLive(&_floppy, &_graphics);

            if (live == nullptr)
            {
                Log << F("Out of memory starting the live session\r\n");
            }
            else
            {
                live->run(&_cancelOperation);
                delete live;
            }

            // The ESP has been talking to a Serial1 nobody was reading. Whatever is
            // left in the ring is the tail of a line, so drop it rather than hand
            // XCopyESP8266's parser half a command.
            while (ESPSerial.available())
                ESPSerial.read();

            _cancelOperation = false;
            setBusy(false);
            _xcopyState = menus;
            _command->printPrompt();
            break;
        }
        case menus:
        {
            _graphics.clearScreen();
            _graphics.drawHeader();
            drawMenuScreen();
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
                _disk.diskToADF(_adfFilePath, _config->getVerify(), _config->getRetryCount(), _sdCard);
                // delete _config;

                setBusy(false);
                _drawnOnce = true;
            }
            break;
        }
        case copyDiskToSCP:
        {
            if (_drawnOnce == false)
            {
                // A range or revolution count supplied for this run wins; zero means
                // nothing was given and the saved setting applies.
                uint8_t revolutions = _scpRevolutions ? _scpRevolutions : _config->getScpRevolutions();
                uint8_t endCylinder = _scpRangeGiven ? _scpEndCylinder : _config->getScpEndCylinder();

                _disk.diskToSCP(_scpFilePath, revolutions, _scpStartCylinder, endCylinder,
                                _config->getRetryCount());

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
        case analyseDisk:
        {
            /*
               One shot, like fluxDisk above: the survey runs to completion inside
               this visit and reports as it goes, rather than being pumped a track
               at a time. It cannot service web commands mid-survey - nothing in
               here may call _esp->update() while the capture buffer is in use -
               so cancelling goes through the interrupt, not through a command.
            */
            if (_drawnOnce == false)
            {
                if (_diskInfoPath == "")
                    _diskInfo.surveyDisk(_diskInfoFirst, _diskInfoLast, _diskInfoSide);
                else
                    _diskInfo.surveyScp(_diskInfoPath, _diskInfoFirst, _diskInfoLast, _diskInfoSide);

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
        case headCalibration:
        {
            /*
               Shaped like showTime rather than like the blocking loop this
               replaces: one bounded unit of work per visit and then straight back
               out, so update() keeps pumping _command->Update() and _esp->Update()
               between passes. That is the whole reason the test can be driven from
               the console and the browser at all.

               Nothing here may touch _xcopyState except on the way out, or the
               do/while around this switch will spin.
            */
            if (_drawnOnce == false)
            {
                _headCal.begin(&_graphics, &_audio, _esp, &_floppy, _headCalCylinder);
                _headCal.drawStatic();
                _headCal.panelBegin();
                _headCal.sendConfig();
                _esp->setTab("headcal");
                // Both the USB console and the browser terminal funnel through
                // processKey(), so one hook serves both.
                _command->setRawKeys(this, onHeadCalKey);
                _drawnOnce = true;
            }

            _headCal.update();
            break;
        }
        case driveToolkit:
        {
            /*
               Same shape as headCalibration above: one bounded sample per visit
               and then straight back out, so update() keeps pumping
               _command->Update() and _esp->Update() between refreshes. That is the
               whole reason the toolkit can be driven from the console and the
               browser at all.

               Nothing here may touch _xcopyState except on the way out, or the
               do/while around this switch will spin.
            */
            if (_drawnOnce == false)
            {
                _driveToolkit.begin(&_graphics, _esp, &_floppy);
                _driveToolkit.drawStatic();
                _driveToolkit.panelBegin();
                _driveToolkit.sendState();
                _esp->setTab("drivetoolkit");
                // Both the USB console and the browser terminal funnel through
                // processKey(), so one hook serves both.
                _command->setRawKeys(this, onDriveToolkitKey);
                _drawnOnce = true;
            }

            _driveToolkit.update();
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