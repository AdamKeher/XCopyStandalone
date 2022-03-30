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
        Serial << "Version: " << _version << "\r\n";
        return;
    }

    if (cmd == "help" || cmd == "?")
    {
        Serial << F(".-----------------------------------------------------------------------------.\r\n");
        Serial << F("| X-Copy Standalone                                                           |\r\n");
        Serial << F("|-----------------------------------------------------------------------------|\r\n");
        Serial << F("| Command              | Description                                          |\r\n");
        Serial << F("|----------------------+------------------------------------------------------|\r\n");
        Serial << F("| help | ?             | this help                                            |\r\n");
        Serial << F("| version | ver        | XCopy version number                                 |\r\n");
        Serial << F("| clear | cls          | clear screen                                         |\r\n");
        Serial << F("|--------------------- +------------------------------------------------------|\r\n");
        Serial << F("| dir | ls <directory> | list files on SDCard                                 |\r\n");
        Serial << F("| boot                 | print boot block from disk                           |\r\n");
        Serial << F("| bootf                | print boot block from flash                          |\r\n");
        Serial << F("| flux                 | returns histogram of track in binary                 |\r\n");
        Serial << F("| hist                 | prints histogram of track in ascii                   |\r\n");
        Serial << F("| name                 | reads track 80 an returns disklabel in ascii         |\r\n");
        Serial << F("| print                | prints amiga track with header                       |\r\n");
        Serial << F("| read <n>             | read logical track #n from disk                      |\r\n");
        Serial << F("| readf <n>            | read logical track #n from flash                     |\r\n");
        Serial << F("| dump <filename>      | dump ADF file system information                     |\r\n");
        Serial << F("| weak                 | returns retry number for last read in binary format  |\r\n");
        Serial << F("|--------------------- +------------------------------------------------------|\r\n");
        Serial << F("| time                 | show current date & time                             |\r\n");
        Serial << F("| settime              | set date & time via NTP server                       |\r\n");
        Serial << F("| settime <epoch>      | set date & time with epoch value                     |\r\n");
        Serial << F("| timezone             | show current timezone                                |\r\n");
        Serial << F("| timezone <-12..12>   | set current time zone                                |\r\n");
        Serial << F("|--------------------- +------------------------------------------------------|\r\n");
        Serial << F("| connect <ssid> <pwd> | connect to wifi network                              |\r\n");
        Serial << F("| status               | show wifi status                                     |\r\n");
        Serial << F("| ip                   | show wifi ip address                                 |\r\n");
        Serial << F("| ssid                 | show wifi ssid                                       |\r\n");
        Serial << F("| websocket <msg>      | broadcast message to webclients                      |\r\n");
        Serial << F("|--------------------- +------------------------------------------------------|\r\n");
        Serial << F("| config               | show config settings                                 |\r\n");
        Serial << F("`----------------------'------------------------------------------------------'\r\n");
        /*
        Serial << "| write <n>       | write logical track #n                                    |\r\n";
        Serial << "| testwrite <n>   | write logical track #n filled with 0-255                  |\r\n";
        Serial << "| get <n>         | reads track #n silent                                     |\r\n";
        Serial << "| put <n>         | writes track #n silent                                    |\r\n";
        Serial << "| init            | goto track 0                                              |\r\n";
        Serial << "| hist            | prints histogram of track in ascii                        |\r\n";
        Serial << "| index           | prints index signal timing in ascii                       |\r\n";
        Serial << "| dskcng          | returns disk change signal in binary                      |\r\n";
        Serial << "| dens            | returns density type of inserted disk in ascii            |\r\n";
        Serial << "| info            | prints state of various floppy signals in ascii           |\r\n";
        Serial << "| enc             | encodes data track into mfm                               |\r\n";
        Serial << "| dec             | decodes raw mfm into data track                           |\r\n";
        Serial << "| log             | prints logical track / tracknumber extracted from sectors |\r\n";
        Serial << "| dskcng          | returns disk change signal in binary                      |\r\n";
        Serial << "`-----------------'-----------------------------------------------------------'\r\n";
        */
        return;
    }

    if (cmd == "clear" || cmd == "cls")
    {
        Serial << XCopyConsole::clearscreen() << XCopyConsole::home();
        return;
    }

    if (cmd == "status")
    {
        String status = _esp->sendCommand("status\r", true);
        Serial << status << "\r\n";
        return;
    }

    if (cmd == "ssid")
    {
        String ssid = _esp->sendCommand("ssid\r", true);
        Serial << ssid << "\r\n";
        return;
    }

    if (cmd == "ip")
    {
        String ipaddress = _esp->sendCommand("ip\r", true);
        Serial << ipaddress << "\r\n";
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
            Serial << "Error: must supply ssid and password.\r\n";
            return;
        }

        _config->setSSID(ssid);
        _config->setPassword(password);
        _config->writeConfig();

        if (_esp->connect(ssid, password, 20000))
            Serial << "Connected to '" << ssid << "'\r\n";
        else
            Serial << "Error: Connection to '" << ssid << "' failed\r\n";
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
        Serial << getWeakTrack() << "\r\n";
        return;
    }

    if (cmd == "name")
    {
        Serial << "Diskname: " << getName() << "\r\n";
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
        Serial.printf("Reading Track %d\t", param.toInt());
        gotoLogicTrack(param.toInt());
        uint8_t errors = readTrack(false);
        if (errors != -1)
        {
            Serial << "Sectors found: " << getSectorCnt() << " Errors found: ";
            Serial.print(errors, BIN);
            Serial << " Track expected: " << param.toInt() << " Track found: " << getTrackInfo() << " bitCount: " << getBitCount() << " (Read OK)\r\n";
        }
        else
        {
            Serial << "bitCount: " << getBitCount() << " (Read failed!)\r\n";
        }
        return;
    }

    if (cmd == "dump")
    {
        char name[128];
        param.toCharArray(name, 128);

        Serial << "DEBUG-1\r\n";

        XCopyADFLib *_adfLib = new XCopyADFLib();
        _adfLib->begin(PIN_SDCS);
        _adfLib->mount(name);

        Serial << "DEBUG-1\r\n";

        if (_adfLib->getDevice())
        {
            Serial << "DEBUG0\r\n";

            _adfLib->printDevice(_adfLib->getDevice());

            Serial << "DEBUG1\r\n";

            _adfLib->openVolume(_adfLib->getDevice());
            if (_adfLib->getVolume())
            {
                Serial << "DEBUG2\r\n";
                _adfLib->printVolume(_adfLib->getVolume());
            }
            else
                Serial << "Error: Failed to open volume '" << name << "'\r\n";
        }
        else
            Serial << "Error: Failed to open device '" << name << "'\r\n";

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
        Serial.printf("Reading Track %d\r\n", 0);

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
                Serial << "Sectors found: " << getSectorCnt() << " Errors found: ";
                Serial.print(errors, BIN);
                Serial << " Track expected: " << param.toInt() << " Track found: " << getTrackInfo() << " bitCount: " << getBitCount() << " (Read OK)\r\n";
            }
            else
            {
                Serial << "bitCount: " << getBitCount() << " (Read failed!)\r\n";
            }
        }

        printBootSector();
        return;
    }

    if (cmd == "print")
    {
        printTrack();
        Serial << "OK\r\n";
        return;
    }

    if (cmd == "websocket")
    {
        _esp->sendWebSocket(param);
        Serial << "broadcast: '" << param << "'\r\n";
        return;
    }

    if (cmd == "time") {
        int timeZone = _config->getTimeZone();
        Serial.printf("%02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == "settime") {
        int timeZone = _config->getTimeZone();
        Serial << "Current Time: " << XCopyTime::getTime() << " (epoch)";
        Serial.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
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
        Serial << "Updated Time: " << time << " (epoch)";
        Serial.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
        return;
    }

    if (cmd == "timezone") {
        if (param.length() > 0) {
            int timeZone = param.toInt();
            if (timeZone > 12) timeZone = 12;
            if (timeZone < -12) timeZone = -12;
            _config->setTimeZone(timeZone);
        }
        Serial << "Time Zone: " << _config->getTimeZone() << "\r\n";            
        return;
    }

    if (cmd == "dir" || cmd == "ls") {
        XCopySDCard *_sdcard = new XCopySDCard();

        if (!_sdcard->cardDetect()) {
            Serial << "No SDCard detected\r\n";
            return;
        }

        if (!_sdcard->begin()) {
            Serial << "SDCard failed to initialise\r\n";
            return;
        }

        if (!_sdcard->printDirectory(param)) {
            Serial << "Could not open directory\r\n";
        }

        delete _sdcard;
        return;
    }

    if (cmd != "")
        Serial << "Unknown command: '" << cmd << "'\r\n";
}

void XCopyCommandLine::printPrompt()
{
    Serial << ">> ";
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
            Serial << XCopyConsole::backspace();
            return;
        }

        if (inChar == 0x0d || inChar == 0x0a)
        {
            Serial << "\r\n";
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
            Serial << inChar;
        }
    }
}
