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

    if (cmd == "version" || cmd == "ver")
    {
        Log << "Version: " << _version << "\r\n";
        return;
    }

    if (cmd == "help" || cmd == "?")
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
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| time                 | show current date & time                             |\r\n");
        Log << F("| settime              | set date & time via NTP server                       |\r\n");
        Log << F("| settime <epoch>      | set date & time with epoch value                     |\r\n");
        Log << F("| timezone             | show current timezone                                |\r\n");
        Log << F("| timezone <-12..12>   | set current time zone                                |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| connect <ssid> <pwd> | connect to wifi network                              |\r\n");
        Log << F("| status               | show wifi status                                     |\r\n");
        Log << F("| ip                   | show wifi ip address                                 |\r\n");
        Log << F("| mac                  | show wifi mac address                                |\r\n");
        Log << F("| ssid                 | show wifi ssid                                       |\r\n");
        Log << F("| websocket <msg>      | broadcast message to webclients                      |\r\n");
        Log << F("| scan                 | scan wireless networks                               |\r\n");
        Log << F("|--------------------- +------------------------------------------------------|\r\n");
        Log << F("| config               | show config settings                                 |\r\n");
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

    if (cmd == "clear" || cmd == "cls")
    {
        Log << XCopyConsole::clearscreen() << XCopyConsole::home();
        return;
    }

    if (cmd == "status")
    {
        String status = _esp->sendCommand("status\r", true);
        Log << status << "\r\n";
        return;
    }

    if (cmd == "ssid")
    {
        String ssid = _esp->sendCommand("ssid\r", true);
        Log << ssid << "\r\n";
        return;
    }

    if (cmd == "ip")
    {
        String ipaddress = _esp->sendCommand("ip\r", true);
        Log << ipaddress << "\r\n";
        return;
    }

    if (cmd == "mac")
    {
        String ipaddress = _esp->sendCommand("mac\r", true);
        Log << ipaddress << "\r\n";
        return;
    }

    if (cmd == "config") {
        _config->dumpConfig();
        return;
    }

    if (cmd == "connect")
    {
        String ssid = param.substring(0, param.indexOf(" "));
        String password = param.substring(param.indexOf(" ") + 1);
        if (ssid == "" || password == "" || param.indexOf(" ") == -1)
        {
            Log << "Error: must supply ssid and password.\r\n";
            return;
        }

        _config->setSSID(ssid);
        _config->setPassword(password);
        _config->writeConfig();

        if (_esp->connect(ssid, password, 20000))
            Log << "Connected to '" << ssid << "'\r\n";
        else
            Log << "Error: Connection to '" << ssid << "' failed\r\n";
        return;
    }

    if (cmd == "hist")
    {
        analyseHist(false);
        printHist();
        return;
    }

    if (cmd == "flux")
    {
        analyseHist(true);
        printFlux();
        return;
    }

    if (cmd == "weak")
    {
        Log << getWeakTrack() << "\r\n";
        return;
    }

    if (cmd == "name")
    {
        Log << "Diskname: " << getName() << "\r\n";
        return;
    }

    if (cmd == "readf")
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

    if (cmd == "read")
    {
        Log.printf("Reading Track %d\t", param.toInt());
        gotoLogicTrack(param.toInt());
        uint8_t errors = readTrack(false);
        if (errors != -1)
        {
            Log << "Sectors found: " << getSectorCnt() << " Errors found: ";
            Serial.print(errors, BIN);
            Log << " Track expected: " << param.toInt() << " Track found: " << getTrackInfo() << " bitCount: " << getBitCount() << " (Read OK)\r\n";
        }
        else
        {
            Log << "bitCount: " << getBitCount() << " (Read failed!)\r\n";
        }
        return;
    }

    if (cmd == "dump")
    {
        char name[128];
        param.toCharArray(name, 128);

        Log << "DEBUG-1\r\n";

        XCopyADFLib *_adfLib = new XCopyADFLib();
        _adfLib->begin(PIN_SDCS);
        _adfLib->mount(name);

        Log << "DEBUG-1\r\n";

        if (_adfLib->getDevice())
        {
            Log << "DEBUG0\r\n";

            _adfLib->printDevice(_adfLib->getDevice());

            Log << "DEBUG1\r\n";

            _adfLib->openVolume(_adfLib->getDevice());
            if (_adfLib->getVolume())
            {
                Log << "DEBUG2\r\n";
                _adfLib->printVolume(_adfLib->getVolume());
            }
            else
                Log << "Error: Failed to open volume '" << name << "'\r\n";
        }
        else
            Log << "Error: Failed to open device '" << name << "'\r\n";

        _adfLib->unmount();
        delete _adfLib;

        return;
    }

    if (cmd == "bootf")
    {
        cmd = "boot";
        param = "f";
        return;
    }

    if (cmd == "boot")
    {
        Log.printf("Reading Track %d\r\n", 0);

        param.toLowerCase();
        if (param == "flash" || param == "f")
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
                Log << "Sectors found: " << getSectorCnt() << " Errors found: ";
                Serial.print(errors, BIN);
                Log << " Track expected: " << param.toInt() << " Track found: " << getTrackInfo() << " bitCount: " << getBitCount() << " (Read OK)\r\n";
            }
            else
            {
                Log << "bitCount: " << getBitCount() << " (Read failed!)\r\n";
            }
        }

        printBootSector();
        return;
    }

    if (cmd == "print")
    {
        printTrack();
        Log << "OK\r\n";
        return;
    }

    if (cmd == "websocket")
    {
        _esp->sendWebSocket(param);
        Log << "broadcast: '" << param << "'\r\n";
        return;
    }

    if (cmd == "time") {
        int timeZone = _config->getTimeZone();
        Serial.printf("%02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        Log.printf("%02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == "settime") {
        int timeZone = _config->getTimeZone();
        Log << "Current Time: " << XCopyTime::getTime() << " (epoch)";
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
        Log << "Updated Time: " << time << " (epoch)";
        Log.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == "timezone") {
        if (param.length() > 0) {
            int timeZone = param.toInt();
            if (timeZone > 12) timeZone = 12;
            if (timeZone < -12) timeZone = -12;
            _config->setTimeZone(timeZone);
        }
        Log << "Time Zone: " << _config->getTimeZone() << "\r\n";            
        return;
    }

    if (cmd == "dir" || cmd == "ls") {
        XCopySDCard *_sdcard = new XCopySDCard();

        if (!_sdcard->cardDetect()) {
            Log << "No SDCard detected\r\n";
            return;
        }

        if (!_sdcard->begin()) {
            Log << "SDCard failed to initialise\r\n";
            return;
        }

        if (!_sdcard->printDirectory(param)) {
            Log << "Could not open directory\r\n";
        }

        delete _sdcard;
        return;
    }

    if (cmd == "scan") {
        Log << "Scanning: \r\n";
        String status = _esp->sendCommand("scan\r", true, 5000);
        Log << status << "\r\n";
        return;
    }

    if (cmd == "test") {
        XCopySDCard *_sdcard = new XCopySDCard();
        
        if (!_sdcard->cardDetect()) {
            Log << "No SDCard detected\r\n";
            return;
        }

        if (!_sdcard->begin()) {
            Log << "SDCard failed to initialise\r\n";
            return;
        }

        _sdcard->getFiles(param);

        // GenericList<XCopyFile> *list = _sdcard->getFiles(param);
        // Node<XCopyFile> *p = list->head;
        // while (p) {
        //     XCopyFile *xFile = p->data;
        //     Serial << "File: " << xFile->filename << "," << xFile->size << "," << xFile->isDirectory << "," << xFile->isADF << "\r\n";
        //     p = p->next;
        // }
        // delete list;
        // delete p;

        delete _sdcard;

        return;
    }

    if (cmd != "")
        Log << "Unknown command: '" << cmd << "'\r\n";
}

void XCopyCommandLine::printPrompt()
{
    Log << ">> ";
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
