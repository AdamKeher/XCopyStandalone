#include "XCopyCommand.h"
#include "XCopyScratch.h"
#include <Streaming.h>
#include <SerialFlash.h>

XCopyCommandLine::XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config, XCopyDisk* disk, XCopyFloppy* floppy)
{
    _version = version;
    _esp = esp;
    _config = config;
    _disk = disk;
    _floppy = floppy;
}

void XCopyCommandLine::doCommand(String command)
{
    // Arduino's char/char replace substitutes bytes without changing the length, so
    // the previous replace((char)10, (char)0) pair embedded NULs instead of removing
    // anything. Replacing with an empty String actually shortens it.
    command.replace("\r", "");
    command.replace("\n", "");

    String cmd = command;
    String param = "";
    if (command.indexOf(" ") > 0)
    {
        param = command.substring(command.indexOf(" ") + 1);
        cmd.remove(command.indexOf(" "), command.length());
    }
    // Outside the if: this used to run only when a parameter was present, so
    // parameterless commands such as "HELP" were case sensitive.
    cmd.toLowerCase();

    if (cmd == F("version") || cmd == F("ver"))
    {
        Log << F("Version: ") << _version << F("\r\n");
        return;
    }

    if (cmd == F("help") || cmd == F("?"))
    {
        Log << F(".--------------------------------------------------------------------------------------.\r\n");
        Log << F("| X-Copy Standalone                                                                    |\r\n");
        Log << F("|--------------------------------------------------------------------------------------|\r\n");
        Log << F("| Command                        | Description                                         |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| help | ?                       | this help                                           |\r\n");
        Log << F("| version | ver                  | XCopy version number                                |\r\n");
        Log << F("| clear | cls                    | clear screen                                        |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| dir | ls <directory>           | list files on SDCard                                |\r\n");
        Log << F("| cat <filename>                 | writes contents of file to terminal                 |\r\n");
        Log << F("| rm <filename>                  | delete file from sdcard                             |\r\n");
        Log << F("| md5 <filename|flash>           | md5 has of file from sdcard or flash                |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| readadf [<filename>]           | read floppy disk to adf file on sdcard              |\r\n");
        Log << F("| writeadf <filename>            | write adf file to floppy disk                       |\r\n");
        Log << F("| readscp [<a-b>] [<n>] [<file>] | read floppy disk to scp flux image on sdcard        |\r\n");
        Log << F("|                                | optional cylinder range <a-b> and <n> revolutions   |\r\n");
        Log << F("| writeflash                     | read floppy disk into flash memory                  |\r\n");
        Log << F("| writebin <filename> <block>    | write binary file to disk starting at block         |\r\n");
        Log << F("| live                           | hand this usb session to a host over the binary     |\r\n");
        Log << F("|                                | live streaming protocol - see XCopyLiveProtocol.h   |\r\n");
        Log << F("| testdisk                       | test floppy disk                                    |\r\n");
        Log << F("| scanblocks                     | scan floppy disk for free blocks                    |\r\n");
        Log << F("| search <searchtext>            | search disk for case sensative ascii text           |\r\n");
        Log << F("| modsearch                      | search disk for tracker modules                     |\r\n");
        Log << F("| modrip <block> <offset> <size> | rip tracker mod starting at <block> with a byte     |\r\n");
        Log << F("|                                | <offset> for <size> total bytes.                    |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| boot                           | print boot block from disk                          |\r\n");
        Log << F("| bootf                          | print boot block from flash                         |\r\n");
        Log << F("| hist                           | prints histogram of track in ascii                  |\r\n");
        Log << F("| rpm <ms>                       | drive speed from index pulses, every <ms>           |\r\n");
        Log << F("| headcal | hc [<cyl>]           | continuous head calibration test, ATK style         |\r\n");
        Log << F("|                                | r:reseek +/-:1 []:10 {}:40 h:head a:auto s:sound    |\r\n");
        Log << F("| name                           | reads track 80 an returns disklabel in ascii        |\r\n");
        Log << F("| print                          | prints amiga track with header                      |\r\n");
        Log << F("| read <n>                       | read logical track #n from disk                     |\r\n");
        Log << F("| readf <n>                      | read logical track #n from flash                    |\r\n");
        Log << F("| dump <filename>                | dump ADF file system information                    |\r\n");
        Log << F("| weak                           | returns retry number for last read in binary format |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| time                           | show current date & time                            |\r\n");
        Log << F("| settime                        | set date & time via NTP server                      |\r\n");
        Log << F("| settime <epoch>                | set date & time with epoch value                    |\r\n");
        Log << F("| timezone                       | show current timezone                               |\r\n");
        Log << F("| timezone <-12..12>             | set current time zone                               |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| connect <ssid> <pwd>           | connect to wifi network                             |\r\n");
        Log << F("| clearwifi                      | clears wifi settings from configuration             |\r\n");        
        Log << F("| status                         | show wifi status                                    |\r\n");
        Log << F("| ip                             | show wifi ip address                                |\r\n");
        Log << F("| mac                            | show wifi mac address                               |\r\n");
        Log << F("| ssid                           | show wifi ssid                                      |\r\n");
        Log << F("| websocket <msg>                | broadcast message to webclients                     |\r\n");
        Log << F("| scan                           | scan wireless networks                              |\r\n");
        Log << F("| pass                           | enter ESP passthrough mode                          |\r\n");
        Log << F("|--------------------------------+-----------------------------------------------------|\r\n");
        Log << F("| config                         | show config settings                                |\r\n");
        Log << F("| mem                            | show memory stats                                   |\r\n");
        Log << F("| reboot                         | restart the device                                  |\r\n");
        Log << F("`--------------------------------'-----------------------------------------------------'\r\n");
        /*
        Log << F("| write <n>       | write logical track #n                                    |\r\n");
        Log << F("| testwrite <n>   | write logical track #n filled with 0-255                  |\r\n");
        Log << F("| get <n>         | reads track #n silent                                     |\r\n");
        Log << F("| put <n>         | writes track #n silent                                    |\r\n");
        Log << F("| init            | goto track 0                                              |\r\n");
        Log << F("| hist            | prints histogram of track in ascii                        |\r\n");
        Log << F("| index           | prints index signal timing in ascii                       |\r\n");
        Log << F("| dskcng          | returns disk change signal in binary                      |\r\n");
        Log << F("| dens            | returns density type of inserted disk in ascii            |\r\n");
        Log << F("| info            | prints state of various floppy signals in ascii           |\r\n");
        Log << F("| enc             | encodes data track into mfm                               |\r\n");
        Log << F("| dec             | decodes raw mfm into data track                           |\r\n");
        Log << F("| log             | prints logical track / tracknumber extracted from sectors |\r\n");
        Log << F("| dskcng          | returns disk change signal in binary                      |\r\n");
        Log << F("`-----------------'-----------------------------------------------------------'\r\n");
        */
        return;
    }

    // Same reboot the TFT settings menu and the browser's Tools menu ask for, so
    // there is one of them rather than three that have to agree.
    if (cmd == F("reboot"))
    {
        _callback(_caller, "rebootDevice");
        return;
    }

    if (cmd == F("clear") || cmd == F("cls"))
    {
        Log << XCopyConsole::clearscreen() << XCopyConsole::home();
        return;
    }

    if (cmd == F("status"))
    {
        String status = _esp->sendCommand(F("status"), true);
        Log << status << F("\r\n");
        return;
    }

    if (cmd == F("ssid"))
    {
        String ssid = _esp->sendCommand(F("ssid"), true);
        Log << ssid << F("\r\n");
        return;
    }

    if (cmd == F("ip"))
    {
        String ipaddress = _esp->sendCommand(F("ip"), true);
        Log << ipaddress << F("\r\n");
        return;
    }

    if (cmd == F("mac"))
    {
        String ipaddress = _esp->sendCommand(F("mac"), true);
        Log << ipaddress << F("\r\n");
        return;
    }

    if (cmd == F("config")) {
        _config->dumpConfig();
        return;
    }

    if (cmd == F("connect"))
    {
        if (param == "" || param.indexOf(" ") == -1) {
            Log << F("Error: ssid and password parameters required to connect to WiFi.\r\n");
            return;
        }

        setBusy(true);

        String ssid = param.substring(0, param.indexOf(" "));
        String password = param.substring(param.indexOf(" ") + 1);

        _config->setSSID(ssid);
        _config->setPassword(password);
        _config->writeConfig();

        if (_esp->connect(ssid, password, 20000))
            Log << F("Connected to '") << ssid << F("'\r\n");
        else
            Log << F("Error: Connection to '") << ssid << F("' failed\r\n");

        setBusy(false);
        return;
    }

    if (cmd == F("clearwifi")) {
        _config->setSSID("");
        _config->setPassword("");
        _config->writeConfig();
        _config->dumpConfig();
        // The ESP keeps its own copy now, so that it can rejoin the network after
        // a reset without the Teensy. Clearing only this side would leave it
        // reconnecting to a network the device no longer thinks it has.
        _esp->sendCommand("forget", true);
        Log << "WiFi settings cleared\r\n";
        return;
    }

    if (cmd == F("hist"))
    {
        _floppy->analyseHist(false);
        _floppy->printHist();
        return;
    }

    if (cmd == F("rpm"))
    {
        if (!_floppy->diskChange())
        {
            Log << F("Disk not inserted into floppy\r\n");
            return;
        }

        uint32_t interval = param == "" ? 1000 : (uint32_t)param.toInt();
        if (interval < 100) interval = 100;

        setBusy(true);
        _floppy->motorOn();
        // The newline that submitted this command is still queued, so the
        // wait-for-keypress loop below would exit before taking a reading.
        while (Serial.available()) Serial.read();
        _floppy->beginRPM();

        Log << F("Measuring drive speed, press any key to stop ...\r\n");

        uint32_t lastReading = millis() - interval;
        while (!Serial.available())
        {
            // motorTimeout() shuts the drive down after motorMaxTick idle
            // seconds; keep resetting the tick or it stops mid measurement
            _floppy->motorOn();

            if (millis() - lastReading < interval) continue;
            lastReading = millis();

            float rpm = _floppy->readRPM();
            if (rpm == 0.0f)
                Log << XCopyConsole::error(F("No index signal detected\r\n"));
            else
                Log << F("Drive speed: ") << String(rpm, 2) << F(" RPM\r\n");
        }
        while (Serial.available()) Serial.read();

        _floppy->endRPM();
        _floppy->motorOff();
        setBusy(false);

        return;
    }

    // if (cmd == F("flux"))
    // {
    //     analyseHist(true);
    //     printFlux();
    //     return;
    // }

    if (cmd == F("weak"))
    {
        Log << _floppy->getWeakTrack() << F("\r\n");
        return;
    }

    if (cmd == F("name"))
    {
        Log << F("Diskname: ") << _floppy->getName() << F("\r\n");
        return;
    }

    if (cmd == F("readf"))
    {
        if (param == "")
            param = "0";

        SerialFlashFile flashFile = SerialFlash.open("DISKCOPY.TMP");
        flashFile.seek(param.toInt() * 11 * 512);

        for (uint8_t i = 0; i < 11; i++)
        {
            // Straight into the sector. The old code read into a 512 byte local and
            // then byte copied it across, which cost a frame this deep in doCommand()
            // and bought nothing - the destination is already contiguous and exactly
            // this size.
            struct Sector *aSec = (Sector *)&_floppy->getTrack()[i].sector[0];
            flashFile.read(aSec->data, sizeof(aSec->data));
        }
        _floppy->setSectorCnt(11);
        return;
    }

    if (cmd == F("read"))
    {
        Log.printf("Reading Track %2d:\r\n", param.toInt());
        _floppy->gotoLogicTrack(param.toInt());
        // int, not uint8_t: readTrack() signals failure with -1.
        int errors = _floppy->readTrack(false);
        if (errors != -1)
        {
            Log << F("Sectors found: ") << _floppy->getSectorCnt() << F(" Errors found: ");
            Log << String(errors, BIN);
            Log << F(" Track expected: ") + String(param.toInt()) + F(" Track found: ") + String(_floppy->getTrackInfo()) + F(" bitCount: ") + String(_floppy->getBitCount()) + F(" (Read OK)\r\n");
        }
        else
        {
            Log << F("bitCount: ") + String(_floppy->getBitCount()) + F(" (Read failed!)\r\n");
        }
        return;
    }

    if (cmd == F("dump")) {
        const char *name = param.c_str();

        XCopyADFLib *_adfLib = new XCopyADFLib();
        _adfLib->begin();
        _adfLib->mount(name);

        if (_adfLib->getDevice())
        {
            // _adfLib->printDevice(_adfLib->getDevice());
            Log << _adfLib->printDevice(_adfLib->getDevice());
            _adfLib->openVolume(_adfLib->getDevice());
            if (_adfLib->getVolume()) {
                Log << _adfLib->printVolume(_adfLib->getVolume());
                Log << _adfLib->printDirectory(_adfLib->getVolume());
            }
            else
                Log << F("Error: Failed to open volume '") << name << F("'\r\n");
        }
        else
            Log << F("Error: Failed to open device '") << name << F("'\r\n");

        _adfLib->unmount();
        delete _adfLib;

        return;
    }

    if (cmd == F("bootf"))
    {
        cmd = F("boot");
        param = F("f");
        return;
    }

    if (cmd == F("boot"))
    {
        Log.printf("Reading Track %d\r\n", 0);

        param.toLowerCase();
        if (param == F("flash") || param == F("f"))
        {
            SerialFlashFile flashFile = SerialFlash.open("DISKCOPY.TMP");
            flashFile.seek(0 * 11 * 512);

            for (uint8_t i = 0; i < 11; i++)
            {
                // Straight into the sector, as in "readflash" above.
                struct Sector *aSec = (Sector *)&_floppy->getTrack()[i].sector[0];
                flashFile.read(aSec->data, sizeof(aSec->data));
            }
            _floppy->setSectorCnt(11);
        }
        else
        {
            _floppy->gotoLogicTrack(0);
            // int, not uint8_t: readTrack() signals failure with -1.
            int errors = _floppy->readTrack(false);
            if (errors != -1)
            {
                Log << F("Sectors found: ") << _floppy->getSectorCnt() << F(" Errors found: ");
                Serial.print(errors, BIN);
                Log << F(" Track expected: ") << param.toInt() << F(" Track found: ") << _floppy->getTrackInfo() << F(" bitCount: ") << _floppy->getBitCount() << F(" (Read OK)\r\n");
            }
            else
            {
                Log << F("bitCount: ") << _floppy->getBitCount() << F(" (Read failed!)\r\n");
            }
        }

        _floppy->printBootSector();

        Log << "\r\nScanning boot block for match ...\r\n";
        
        uint32_t crc32 = _floppy->bootSectorCRC32();
        Track *track = _floppy->getTrack();
        struct Sector *block0 = (Sector *)&track[0].sector;
        struct Sector *block1 = (Sector *)&track[0].sector;
        XCopyBrainFile::identifyBootblock(block0->data, block1->data, crc32);

        return;
    }

    if (cmd == F("print"))
    {
        _floppy->printTrack();
        Log << F("OK\r\n");
        return;
    }

    if (cmd == F("websocket"))
    {
        _esp->sendWebSocket(param);
        Log << F("broadcast: '") << param << F("'\r\n");
        return;
    }

    if (cmd == F("time")) {
        int timeZone = _config->getTimeZone();
        Log.printf("%02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == F("settime")) {
        int timeZone = _config->getTimeZone();
        Log << F("Current Time: ") << XCopyTime::getTime() << F(" (epoch)");
        Log.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        time_t time = 0;        
        if (param.length() == 0) {
            time = _esp->getTime();
        } else {
            time = strtol(param.c_str(), nullptr, 10);
        }
        time = time + (timeZone * 60 * 60);
        XCopyTime::syncTime(false);
        XCopyTime::setTime(time);
        delay(2000);
        XCopyTime::syncTime(true);
        delay(2000);
        Log << F("Updated Time: ") << time << F(" (epoch)");
        Log.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == F("timezone")) {
        if (param.length() > 0) {
            int timeZone = param.toInt();
            if (timeZone > 12) timeZone = 12;
            if (timeZone < -12) timeZone = -12;
            _config->setTimeZone(timeZone);
        }
        Log << F("Time Zone: ") << _config->getTimeZone() << F("\r\n");            
        return;
    }

    if (cmd == F("dir") || cmd == F("ls")) {
        setBusy(true);
        printDirectory(param);
        setBusy(false);

        return;
    }

    if (cmd == F("scan")) {
        setBusy(true);
        Log << F("Scanning: \r\n");
        String status = _esp->sendCommand(F("scan"), true, 5000);
        Log << status << F("\r\n");
        setBusy(false);
        return;
    }

    if (cmd == F("ping")) {
        String status = _esp->sendCommand(F("ping"), true, 5000);
        Log << status << F("\r\n");
        return;
    }

    if (cmd == F("mem")) {
        uint32_t stackTop;
        uint32_t heapTop;

        // current position of the stack.
        stackTop = (uint32_t) &stackTop;

        // current position of heap.
        void* hTop = malloc(1);
        heapTop = (uint32_t) hTop;
        free(hTop);

        char sStackTop[12];
        char sHeapTop[12];

        sprintf(sStackTop, "0x%08" PRIx32, stackTop);
        sprintf(sHeapTop, "0x%08" PRIx32,  heapTop);

        // sprintf(sStackTop, "0x%08X", stackTop);
        // sprintf(sHeapTop, "0x%08X", heapTop);

        // The difference is (approximately) the free, available ram.        
        Log << F("Stack Top: ") << sStackTop << F("\r\n");
        Log << F("Heap Top: ") << sHeapTop << F("\r\n");
        Log << F("Free: ") << (stackTop - heapTop) << F(" bytes free\r\n");

        return;
    }

    if (cmd == F("cat")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }
        
        XCopySDCard *_sdcard = new XCopySDCard();
        _sdcard->begin();
        
        if (!_sdcard->cardDetect()) {
            Log << _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->begin()) {
            Log <<  _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        FatFile file;
        bool fresult = file.open(param.c_str());
        if (!fresult) {
            Log << F("unable to open: '") + param + F("'\r\n");
            delete _sdcard;
            return;
        }

        const size_t bufferSize = 512;
        XCopyScratch::Guard scratch("command.dump", bufferSize);
        if (!scratch.valid()) {
            Log << F("track buffer busy\r\n");
            file.close();
            delete _sdcard;
            return;
        }
        char *buffer = (char *)scratch.get();

        // read() returns -1 on error; the old loop tested readsize only after using
        // it, so a failed read appended garbage and then looped forever on (size_t)-1.
        int readsize = 0;
        while ((readsize = file.read(buffer, bufferSize)) > 0) {
            String line = "";
            for (int i = 0; i < readsize; i++) {
                line.append(buffer[i]);
            }
            Log << line;
        }

        file.close();
        delete _sdcard;

        Log << F("[-- eof]\r\n");

        return;
    }

    if (cmd == F("rm")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }
        
        XCopySDCard *_sdcard = new XCopySDCard();
        _sdcard->begin();
        
        if (!_sdcard->cardDetect()) {
            Log << _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->begin()) {
            Log <<  _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (_sdcard->deleteFile(param)) {
            Log << "'" + param + F("' deleted\r\n");
        } else {
            Log << F("unable to delete: '") + param + F("'\r\n");
        }

        delete _sdcard;

        return;
    }

    if (cmd == F("md5")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }
        
        if (param == "flash") {
            Log << _disk->flashToMD5() + "\r\n";
            return;
        }

        XCopySDCard *_sdcard = new XCopySDCard();
        _sdcard->begin();
        
        if (!_sdcard->cardDetect()) {
            Log << _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->begin()) {
            Log <<  _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->fileExists(param)) {
            Log <<  "File not found: '" + param + "'\r\n";
            delete _sdcard;
            return;
        }

        delete _sdcard;

        Log << _disk->adfToMD5(param) + "\r\n";

        return;
    }

    if (cmd == F("readadf")) {
        if (!_floppy->diskChange()) {
            Log << F("Disk not inserted into floppy\r\n");
            return;
        }

        // No filename given: diskToADF() names the file after the disk label and
        // the current date & time, the same as the menu and the web UI do.
        if (param != "") {
            // On a copy: toLowerCase() mutates in place and param is the destination
            // path, which has to keep the case the user typed.
            String extension = param;
            if (!extension.toLowerCase().endsWith(".adf")) {
                Log << F("The file must be an ADF file\r\n");
                return;
            }

            // A bare filename lands in the ADF folder alongside the generated ones;
            // diskToADF() creates that directory if it is missing.
            if (param.indexOf("/") == -1) {
                param = "/" + String(SD_ADF_PATH) + "/" + param;
            }
        }

        // Reported here rather than left to diskToADF(), so the failure lands on the
        // prompt instead of behind the copy screen it would otherwise draw first.
        XCopySDCard _sdcard;

        if (!_sdcard.cardDetect() || !_sdcard.begin()) {
            Log << _sdcard.getError() + "\r\n";
            return;
        }

        _callback(_caller, "copyDiskToADF," + param);

        return;
    }

    if (cmd == F("readscp")) {
        if (!_floppy->diskChange()) {
            Log << F("Disk not inserted into floppy\r\n");
            return;
        }

        /*
           readscp [<first>-<last>] [<revs>] [<filename>]

           Options lead and the filename is whatever is left, taken verbatim - the
           generated names contain spaces ("20260831 1830 Workbench.scp"), so the
           filename cannot be tokenised the way the options are.

           An option is recognised by shape, and the shapes are narrow enough that
           neither can swallow a filename: a range is digits, one dash, digits; a
           revolution count is a single digit, which is all SCP_MAX_REVS needs.
        */
        uint8_t firstCylinder = 0;
        uint8_t lastCylinder = 0;
        uint8_t revolutions = 0;

        String rest = param;
        rest.trim();

        while (rest.length()) {
            int space = rest.indexOf(" ");
            String token = space < 0 ? rest : rest.substring(0, space);

            int dash = token.indexOf("-");
            bool isRange = dash > 0 && dash < (int)token.length() - 1;
            for (unsigned int i = 0; isRange && i < token.length(); i++) {
                if ((int)i != dash && !isDigit(token.charAt(i))) isRange = false;
            }

            bool isCount = token.length() == 1 && isDigit(token.charAt(0));

            if (!isRange && !isCount) break;

            if (isRange) {
                firstCylinder = (uint8_t)token.substring(0, dash).toInt();
                lastCylinder = (uint8_t)token.substring(dash + 1).toInt();

                if (lastCylinder >= MAX_CYLINDERS || firstCylinder > lastCylinder) {
                    Log << F("Cylinder range must be within 0-") << (MAX_CYLINDERS - 1)
                        << F(", lowest first\r\n");
                    return;
                }
            }
            else {
                revolutions = (uint8_t)token.toInt();

                if (revolutions < 1 || revolutions > SCP_MAX_REVS) {
                    Log << F("Revolutions must be 1-") << SCP_MAX_REVS << F("\r\n");
                    return;
                }
            }

            rest = space < 0 ? "" : rest.substring(space + 1);
            rest.trim();
        }

        String filename = rest;

        if (filename != "") {
            // On a copy: toLowerCase() mutates in place and filename is the
            // destination path, which has to keep the case the user typed.
            String extension = filename;
            if (!extension.toLowerCase().endsWith(".scp")) {
                Log << F("The file must be an SCP file\r\n");
                return;
            }

            // A bare filename lands in the SCP folder alongside the generated ones;
            // diskToSCP() creates that directory if it is missing.
            if (filename.indexOf("/") == -1) {
                filename = "/" + String(SD_SCP_PATH) + "/" + filename;
            }
        }

        // Reported here rather than left to diskToSCP(), so the failure lands on the
        // prompt instead of behind the capture screen it would otherwise draw first.
        XCopySDCard _sdcard;

        if (!_sdcard.cardDetect() || !_sdcard.begin()) {
            Log << _sdcard.getError() + "\r\n";
            return;
        }

        // "<first>-<last>,<revs>,<path>", with the path last because a filename may
        // contain a comma and the two numeric fields never can. Zero means "not
        // given" and the saved setting applies.
        _callback(_caller, "copyDiskToSCP," + String(firstCylinder) + "-" + String(lastCylinder) +
                               "," + String(revolutions) + "," + filename);

        return;
    }

    if (cmd == F("writeadf")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }

        XCopySDCard *_sdcard = new XCopySDCard();
        _sdcard->begin();
        
        if (!_sdcard->cardDetect()) {
            Log << _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->begin()) {
            Log <<  _sdcard->getError() + "\r\n";
            delete _sdcard;
            return;
        }

        if (!_sdcard->fileExists(param)) {
            Log <<  "file does not exist\r\n";
            delete _sdcard;
            return;
        }

        delete _sdcard;

        if (!param.toLowerCase().endsWith(".adf")) {
            Log << "The file must be an ADF file\r\n";
            return;
        }

        _callback(_caller, "writeADFFile," + param);

        return;        
    }

    if (cmd == F("writeflash")) {
        // onWebCommand() matches "copyDiskToFlash"; the old "copyDisktoFlash"
        // spelling fell through every branch and the command did nothing.
        _callback(_caller, "copyDiskToFlash");

        return;
    }

    if (cmd == F("testdisk")) {
        if (!_floppy->diskChange()) {
            Log << "Disk not inserted into floppy\r\n";
            return;
        }

        _callback(_caller, "testDisk");

        return;        
    }

    if (cmd == F("headcal") || cmd == F("hc")) {
        // Starts the state and returns, like testdisk. Deliberately not the rpm
        // pattern of owning the console in a loop: that would starve the ESP link
        // and the TFT for as long as somebody was adjusting the drive.
        _callback(_caller, "headCalibration," + param);

        return;
    }

    if (cmd == F("live")) {
        /*
           Hands this USB session to XCopyLive, which switches it to the binary protocol
           in shared/XCopyLiveProtocol.h. Everything after the banner XCopyLive prints
           is frames, so nothing may print here on the way in.
        */
        _callback(_caller, "liveStream");
        return;
    }

    if (cmd == F("pass")) {
        _callback(_caller, "debuggingSerialPassThrough");
    }

    if (cmd == F("scanblocks")) {
        if (!_floppy->diskChange()) {
            Log << "Disk not inserted into floppy\r\n";
            return;
        }

        _callback(_caller, "scanBlocks");

        return;        
    }

    if (cmd == F("search")) {
        if (param == "") {
            Log << "search paramater required.\r\n";
            return;
        }

        _callback(_caller, "asciiSearch," + param);

        return;        
    }

    if (cmd == F("modsearch")) {
        if (!_floppy->diskChange()) {
            Log << "Disk not inserted into floppy\r\n";
            return;
        }

        _callback(_caller, "modSearch");

        return;        
    }

    if (cmd == F("writebin")) {
        if (!_floppy->diskChange()) {
            Log << "Disk not inserted into floppy\r\n";
            return;
        }

        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }

        if (param.indexOf(" ") == -1) {
            Log << F("missing block paramater\r\n");
            return;
        }

        String filename = param.substring(0, param.indexOf(" "));
        int startBlock = param.substring(param.indexOf(" ") + 1).toInt();
        
        setBusy(true);
        _disk->writeFileToBlocks(filename, startBlock, _config->getRetryCount());
        setBusy(false);

        return;        
    }

    if (cmd == F("modrip")) {
        if (param == "" || param.indexOf(" ") == -1) {
            Log << F("Error: block, offset and size are required.\r\n");
            return;
        }

        setBusy(true);

        int block = param.substring(0, param.indexOf(" ")).toInt();
        param = param.substring(param.indexOf(" ") + 1);
        int offset = param.substring(0, param.indexOf(" ")).toInt();
        if (param.indexOf(" ") == -1) {
            Log << F("Error: block, offset and size are required.\r\n");
            return;
        }
        param = param.substring(param.indexOf(" ") + 1);        
        int size = param.substring(0, param.indexOf(" ")).toInt();

        if (block > 1759) {
            Log << F("Error: block must be less than 1759 bytes.\r\n");
            return;
        }

        if (offset >= 512) {
            Log << F("Error: offset must be less than 512 bytes.\r\n");
            return;
        }

        if (size <= 0) {
            Log << F("Error: size must be greater than 0 bytes.\r\n");
            return;
        }

        DiskLocation dl;
        dl.setBlock(block);
        if (_disk->modRip(dl, offset, size, _config->getRetryCount())) {
            Log << "Mod file written successfully\r\n";
        }
        else {
            Log << "Wrtiing mod file failed\r\n";
        };

        setBusy(false);

        return;
    }

    if (cmd != "")
        Log << "Unknown command: '" << cmd << "'\r\n";
}

void XCopyCommandLine::printPrompt()
{
    Log << ">> ";
}

bool XCopyCommandLine::printDirectory(String directory, bool color) {    
    XCopySDCard *_sdcard = new XCopySDCard();

    if (!_sdcard->cardDetect()) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    if (!_sdcard->begin()) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    if (!_sdcard->open(directory)) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    int _count = 0;
    while (_sdcard->next()) {
        _count++;
        // Bound once. This used to be six getfile() calls, each deep copying the
        // entry's three Strings. Valid until the next next().
        const XCopyFile &file = _sdcard->getfile();

        char filesize[12];
        sprintf(filesize, "%11lu", file.size);

        String filename = file.filename;
        if (file.isDirectory) {
            filename.append("/");
            if (color) {
                filename = XCopyConsole::high_yellow() + filename + XCopyConsole::reset();
            }
        } else if (file.isADF && color) {
            filename = XCopyConsole::high_green() + filename + XCopyConsole::reset();
        }

        // One buffer sized up front rather than a chain of + temporaries, each of
        // which allocated and copied the whole line so far.
        String line;
        line.reserve(file.date.length() + file.time.length() + filename.length() + 16);
        line += file.date;
        line += " ";
        line += file.time;
        line += " ";
        line += filesize;
        line += " ";
        line += filename;
        line += "\r\n";
        Log << line;
    }

    Log << "file count: " + String(_count) + "\r\n";
    
    delete _sdcard;

    return true;
}

void XCopyCommandLine::setCallBack(void* caller, OnWebCommand function)
{
    _caller = caller;
    _callback = function;
}

void XCopyCommandLine::setRawKeys(void* caller, OnRawKey function)
{
    _rawCaller = caller;
    _rawKeys = function;
}

void XCopyCommandLine::processKey(char key) {
    // A key screen has the console for the duration. Nothing below this runs, so
    // the line buffer is left exactly as the operator had it before they started.
    if (_rawKeys != nullptr) {
        _rawKeys(_rawCaller, key);
        return;
    }

    // backspace
    if (key == 0x08)  {
        if (_command.length() == 0)
            return;

        _command = _command.substring(0, _command.length() - 1);
        Log << XCopyConsole::backspace();
        return;
    }
    // linefeed
    else if (key == 0x0d || key == 0x0a) {
        Log << "\r\n";
        // Was "if (_command != String(0x0d))". String(int) formats the number, so
        // that compared against "13" rather than a carriage return: entering the
        // command "13" was silently swallowed, prompt and all. doCommand() already
        // ignores an empty command, so no guard is needed.
        doCommand(_command);
        printPrompt();
        _command = "";
    }
    else {
        _command += key;
        Log << key;
    }
}

void XCopyCommandLine::processKeys(String keys) {
    keys.replace("\033[^M", "\r");
    keys.replace("\033[^J", "\n");
    keys.replace("\033[^H", char(0x08));

    // filter out cursor keys
    if (keys == "\033[A") return;
    if (keys == "\033[B") return;
    if (keys == "\033[C") return;
    if (keys == "\033[D") return;

    for(size_t i = 0; i < keys.length(); i++) {
        processKey(keys[i]);
    }
}

void XCopyCommandLine::setBusy(bool state) {
    String sstate = state ? "true" : "false";
    _callback(_caller, "setBusy," + sstate);
}

void XCopyCommandLine::Update()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        processKey(inChar);
    }
}

