#include "XCopyCommand.h"
#include <Streaming.h>
#include <SerialFlash.h>

XCopyCommandLine::XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config)
{
    _version = version;
    _esp = esp;
    _config = config;
}

void XCopyCommandLine::doCommand(String command)
{
    command.replace((char)10, (char)0);
    command.replace((char)13, (char)0);

    String cmd = command;
    String param = "";
    if (command.indexOf(" ") > 0)
    {
        param = command.substring(command.indexOf(" ") + 1);
        cmd.remove(command.indexOf(" "), command.length());
        cmd.toLowerCase();
    }

    if (cmd == F("version") || cmd == F("ver"))
    {
        Log << F("Version: ") << _version << F("\r\n");
        return;
    }

    if (cmd == F("help") || cmd == F("?"))
    {
        Log << F(".-----------------------------------------------------------------------------.\r\n");
        Log << F("| X-Copy Standalone                                                           |\r\n");
        Log << F("|-----------------------------------------------------------------------------|\r\n");
        Log << F("| Command              | Description                                          |\r\n");
        Log << F("|----------------------+------------------------------------------------------|\r\n");
        Log << F("| help | ?             | this help                                            |\r\n");
        Log << F("| version | ver        | XCopy version number                                 |\r\n");
        Log << F("| clear | cls          | clear screen                                         |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| dir | ls <directory> | list files on SDCard                                 |\r\n");
        Log << F("| boot                 | print boot block from disk                           |\r\n");
        Log << F("| bootf                | print boot block from flash                          |\r\n");
        Log << F("| flux                 | returns histogram of track in binary                 |\r\n");
        Log << F("| hist                 | prints histogram of track in ascii                   |\r\n");
        Log << F("| name                 | reads track 80 an returns disklabel in ascii         |\r\n");
        Log << F("| print                | prints amiga track with header                       |\r\n");
        Log << F("| read <n>             | read logical track #n from disk                      |\r\n");
        Log << F("| readf <n>            | read logical track #n from flash                     |\r\n");
        Log << F("| dump <filename>      | dump ADF file system information                     |\r\n");
        Log << F("| weak                 | returns retry number for last read in binary format  |\r\n");
        Log << F("| cat <filename>       | writes contents of file to terminal                  |\r\n");
        Log << F("| md5 <filename>       | md5 has of file from sdcard                          |\r\n");
        Log << F("| rm <filename>        | delete file from sdcard                              |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| time                 | show current date & time                             |\r\n");
        Log << F("| settime              | set date & time via NTP server                       |\r\n");
        Log << F("| settime <epoch>      | set date & time with epoch value                     |\r\n");
        Log << F("| timezone             | show current timezone                                |\r\n");
        Log << F("| timezone <-12..12>   | set current time zone                                |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| connect <ssid> <pwd> | connect to wifi network                              |\r\n");
        Log << F("| clearwifi            | clears wifi settings from configuration              |\r\n");        
        Log << F("| status               | show wifi status                                     |\r\n");
        Log << F("| ip                   | show wifi ip address                                 |\r\n");
        Log << F("| mac                  | show wifi mac address                                |\r\n");
        Log << F("| ssid                 | show wifi ssid                                       |\r\n");
        Log << F("| websocket <msg>      | broadcast message to webclients                      |\r\n");
        Log << F("| scan                 | scan wireless networks                               |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| config               | show config settings                                 |\r\n");
        Log << F("| mem                  | show memory stats                                    |\r\n");
        Log << F("`----------------------'------------------------------------------------------'\r\n");
        /*
        Log << "| write <n>       | write logical track #n                                    |\r\n";
        Log << "| testwrite <n>   | write logical track #n filled with 0-255                  |\r\n";
        Log << "| get <n>         | reads track #n silent                                     |\r\n";
        Log << "| put <n>         | writes track #n silent                                    |\r\n";
        Log << "| init            | goto track 0                                              |\r\n";
        Log << "| hist            | prints histogram of track in ascii                        |\r\n";
        Log << "| index           | prints index signal timing in ascii                       |\r\n";
        Log << "| dskcng          | returns disk change signal in binary                      |\r\n";
        Log << "| dens            | returns density type of inserted disk in ascii            |\r\n";
        Log << "| info            | prints state of various floppy signals in ascii           |\r\n";
        Log << "| enc             | encodes data track into mfm                               |\r\n";
        Log << "| dec             | decodes raw mfm into data track                           |\r\n";
        Log << "| log             | prints logical track / tracknumber extracted from sectors |\r\n";
        Log << "| dskcng          | returns disk change signal in binary                      |\r\n";
        Log << "`-----------------'-----------------------------------------------------------'\r\n";
        */
        return;
    }

    if (cmd == F("clear") || cmd == F("cls"))
    {
        Log << XCopyConsole::clearscreen() << XCopyConsole::home();
        return;
    }

    if (cmd == F("status"))
    {
        String status = _esp->sendCommand(F("status\r"), true);
        Log << status << F("\r\n");
        return;
    }

    if (cmd == F("ssid"))
    {
        String ssid = _esp->sendCommand(F("ssid\r"), true);
        Log << ssid << F("\r\n");
        return;
    }

    if (cmd == F("ip"))
    {
        String ipaddress = _esp->sendCommand(F("ip\r"), true);
        Log << ipaddress << F("\r\n");
        return;
    }

    if (cmd == F("mac"))
    {
        String ipaddress = _esp->sendCommand(F("mac\r"), true);
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

        String ssid = param.substring(0, param.indexOf(" "));
        String password = param.substring(param.indexOf(" ") + 1);

        _config->setSSID(ssid);
        _config->setPassword(password);
        _config->writeConfig();

        if (_esp->connect(ssid, password, 20000))
            Log << F("Connected to '") << ssid << F("'\r\n");
        else
            Log << F("Error: Connection to '") << ssid << F("' failed\r\n");
        return;
    }

    if (cmd == F("clearwifi")) {
        _config->setSSID("");
        _config->setPassword("");
        _config->writeConfig();
        _config->dumpConfig();
        Log << "WiFi settings cleared\r\n";
        return;
    }

    if (cmd == F("hist"))
    {
        analyseHist(false);
        printHist();
        return;
    }

    if (cmd == F("flux"))
    {
        analyseHist(true);
        printFlux();
        return;
    }

    if (cmd == F("weak"))
    {
        Log << getWeakTrack() << F("\r\n");
        return;
    }

    if (cmd == F("name"))
    {
        Log << F("Diskname: ") << getName() << F("\r\n");
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
            byte sectorData[512]; // = new byte[512*11];
            flashFile.read(sectorData, sizeof(sectorData));

            struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];
            for (uint16_t i2 = 0; i2 < 512; i2++)
            {
                aSec->data[i2] = sectorData[i2];
            }
        }
        setSectorCnt(11);
        return;
    }

    if (cmd == F("read"))
    {
        Log.printf("Reading Track %d\t", param.toInt());
        gotoLogicTrack(param.toInt());
        uint8_t errors = readTrack(false);
        if (errors != -1)
        {
            Log << F("Sectors found: ") << getSectorCnt() << F(" Errors found: ");
            Serial.print(errors, BIN);
            Log << F(" Track expected: ") << param.toInt() << F(" Track found: ") << getTrackInfo() << F(" bitCount: ") << getBitCount() << F(" (Read OK)\r\n");
        }
        else
        {
            Log << F("bitCount: ") << getBitCount() << F(" (Read failed!)\r\n");
        }
        return;
    }

    if (cmd == F("dump"))
    {
        char name[128];
        param.toCharArray(name, 128);

        XCopyADFLib *_adfLib = new XCopyADFLib();
        _adfLib->begin(PIN_SDCS);
        _adfLib->mount(name);

        if (_adfLib->getDevice())
        {
            _adfLib->printDevice(_adfLib->getDevice());

            _adfLib->openVolume(_adfLib->getDevice());
            if (_adfLib->getVolume())
            {
                _adfLib->printVolume(_adfLib->getVolume());
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
                byte sectorData[512]; // = new byte[512*11];
                flashFile.read(sectorData, sizeof(sectorData));

                struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];
                for (uint16_t i2 = 0; i2 < 512; i2++)
                {
                    aSec->data[i2] = sectorData[i2];
                }
            }
            setSectorCnt(11);
        }
        else
        {
            gotoLogicTrack(0);
            uint8_t errors = readTrack(false);
            if (errors != -1)
            {
                Log << F("Sectors found: ") << getSectorCnt() << F(" Errors found: ");
                Serial.print(errors, BIN);
                Log << F(" Track expected: ") << param.toInt() << F(" Track found: ") << getTrackInfo() << F(" bitCount: ") << getBitCount() << F(" (Read OK)\r\n");
            }
            else
            {
                Log << F("bitCount: ") << getBitCount() << F(" (Read failed!)\r\n");
            }
        }

        printBootSector();
        return;
    }

    if (cmd == F("print"))
    {
        printTrack();
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
        printDirectory(param);

        return;
    }

    if (cmd == F("scan")) {
        Log << F("Scanning: \r\n");
        String status = _esp->sendCommand(F("scan\r"), true, 5000);
        Log << status << F("\r\n");
        return;
    }

    if (cmd == F("ping")) {
        String status = _esp->sendCommand(F("ping\r"), true, 5000);
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
        
        XCopySDCard *_sdCard = new XCopySDCard();
        _sdCard->begin();
        
        if (!_sdCard->cardDetect()) {
            Log << _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        if (!_sdCard->begin()) {
            Log <<  _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        FatFile file;
        bool fresult = file.open(param.c_str());
        if (!fresult) {
            Log << F("unable to open: '") + param + F("'\r\n");
            delete _sdCard;
            return;
        }

        size_t bufferSize = 2048;
        char buffer[bufferSize];
        int readsize = 0;

        do {
            readsize = file.read(buffer, bufferSize);            
            Serial.write(buffer, readsize);
        } while (readsize > 0);

        file.close();
        delete _sdCard;

        Log << F("[-- eof]\r\n");

        return;
    }

    if (cmd == F("rm")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }
        
        XCopySDCard *_sdCard = new XCopySDCard();
        _sdCard->begin();
        
        if (!_sdCard->cardDetect()) {
            Log << _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        if (!_sdCard->begin()) {
            Log <<  _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        if (_sdCard->deleteFile(param)) {
            Log << "'" + param + F("' deleted\r\n");
        } else {
            Log << F("unable to delete: '") + param + F("'\r\n");
        }

        delete _sdCard;

        return;
    }

    if (cmd == F("md5")) {
        if (param == "") {
            Log << F("missing file paramater\r\n");
            return;
        }
        
        XCopySDCard *_sdCard = new XCopySDCard();
        _sdCard->begin();
        
        if (!_sdCard->cardDetect()) {
            Log << _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        if (!_sdCard->begin()) {
            Log <<  _sdCard->getError() + "\r\n";
            delete _sdCard;
            return;
        }

        FatFile file;
        bool fresult = file.open(param.c_str());
        if (!fresult) {
            Log << F("unable to open: '") + param + F("'\r\n");
            delete _sdCard;
            return;
        }

        size_t bufferSize = 2048;
        char buffer[bufferSize];
        size_t readsize = 0;

        MD5_CTX ctx;
        MD5::MD5Init(&ctx);
        do {
            readsize = file.read(buffer, bufferSize);            
            MD5::MD5Update(&ctx, buffer, readsize);
        } while (readsize > 0);
        unsigned char result[255];
        MD5::MD5Final(result, &ctx);

        file.close();
        delete _sdCard;

        for (size_t i = 0; i < 16; i++) {
            Serial.printf("%02X", result[i]);
        }
        Serial << "\r\n";

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
        char filesize[12];
        sprintf(filesize, "%11d", _sdcard->getfile().size);
        String filename = _sdcard->getfile().filename;
        if (_sdcard->getfile().isDirectory) {
            filename.append("/");
            if (color) {
                filename = XCopyConsole::high_yellow() + filename + XCopyConsole::reset();
            }
        } else if (_sdcard->getfile().isADF & color) {
                filename = XCopyConsole::high_green()+ filename + XCopyConsole::reset();
        }

        Log << _count << ": " << _sdcard->getfile().date + " " + _sdcard->getfile().time + " " + String(filesize) + " " + filename + "\r\n";

        // slow down to allow transfer to web
        delay(6);
    }

    Log << "file count: " + String(_count) + "\r\n";
    
    delete _sdcard;

    return true;
}

void XCopyCommandLine::Update()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();

        if (inChar == 0x08) // backspace
        {
            if (_command.length() == 0)
                return;

            _command = _command.substring(0, _command.length() - 1);
            Log << XCopyConsole::backspace();
            return;
        }

        if (inChar == 0x0d || inChar == 0x0a)
        {
            Log << "\r\n";
            if (_command != String(0x0d))
            {
                doCommand(_command);
                printPrompt();
            }
            _command = "";
        }
        else
        {
            _command += inChar;
            Log << inChar;
        }
    }
}