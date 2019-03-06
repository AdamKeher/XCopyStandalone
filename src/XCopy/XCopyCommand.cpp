#include "XCopyCommand.h"
#include <Streaming.h>
#include <SerialFlash.h>

XCopyCommandLine::XCopyCommandLine(String version)
{
    _version = version;
}

void XCopyCommandLine::doCommand(String command)
{
    command.replace((char)10, (char)0);
    command.replace((char)13, (char)0);

    String cmd = command.toLowerCase();
    String param = "";
    if (command.indexOf(" ") > 0)
    {
        cmd.remove(command.indexOf(" "), command.length());
        param = command.substring(command.indexOf(" ") + 1);
    }

    if (cmd == "version" || cmd == "ver")
    {
        Serial << "Version: " << _version << "\r\n";
    }

    if (cmd == "help" || cmd == "?")
    {
        Serial << ".----------------------------------------------------------------------------.\r\n";
        Serial << "| X-Copy Standalone                                                          |\r\n";
        Serial << "|----------------------------------------------------------------------------|\r\n";
        Serial << "| Command        | Description                                               |\r\n";
        Serial << "|----------------+-----------------------------------------------------------|\r\n";
        Serial << "| help | ?       | this help                                                 |\r\n";
        Serial << "| version | ver  | XCopy version number                                      |\r\n";
        Serial << "| clear | cls    | clear screen                                              |\r\n";
        Serial << "|----------------+-----------------------------------------------------------|\r\n";
        Serial << "| boot           | print boot block from disk                                |\r\n";
        Serial << "| bootf          | print boot block from flash                               |\r\n";
        Serial << "| flux           | returns histogram of track in binary                      |\r\n";
        Serial << "| hist           | prints histogram of track in ascii                        |\r\n";
        Serial << "| name           | reads track 80 an returns disklabel in ascii              |\r\n";
        Serial << "| print          | prints amiga track with header                            |\r\n";
        Serial << "| read <n>       | read logical track #n from disk                           |\r\n";
        Serial << "| readf <n>      | read logical track #n from flash                          |\r\n";
        Serial << "| weak           | returns retry number for last read in binary format       |\r\n";
        Serial << "`----------------'-----------------------------------------------------------'\r\n";
        /*
        Serial << "| write <n>      | write logical track #n                                    |\r\n";
        Serial << "| testwrite <n>  | write logical track #n filled with 0-255                  |\r\n";
        Serial << "| get <n>        | reads track #n silent                                     |\r\n";
        Serial << "| put <n>        | writes track #n silent                                    |\r\n";
        Serial << "| init           | goto track 0                                              |\r\n";
        Serial << "| hist           | prints histogram of track in ascii                        |\r\n";
        Serial << "| index          | prints index signal timing in ascii                       |\r\n";
        Serial << "| dskcng         | returns disk change signal in binary                      |\r\n";
        Serial << "| dens           | returns density type of inserted disk in ascii            |\r\n";
        Serial << "| info           | prints state of various floppy signals in ascii           |\r\n";
        Serial << "| enc            | encodes data track into mfm                               |\r\n";
        Serial << "| dec            | decodes raw mfm into data track                           |\r\n";
        Serial << "| log            | prints logical track / tracknumber extracted from sectors |\r\n";
        Serial << "`----------------'-----------------------------------------------------------'\r\n";
        */
    }

    if (cmd == "clear" || cmd == "cls")
    {
        Serial << "\033[2J\033[H";
    }

    if (cmd == "hist")
    {
        analyseHist(false);
        printHist();
    }

    if (cmd == "flux")
    {
        analyseHist(true);
        printFlux();
    }

    if (cmd == "weak")
    {
        Serial << getWeakTrack() << "\r\n";
    }

    if (cmd == "name")
    {
        Serial << "Diskname: " << getName() << "\r\n";
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
    }

    if (cmd == "bootf")
    {
        cmd = "boot";
        param = "f";
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
    }

    if (cmd == "print")
    {
        printTrack();
        Serial << "OK\r\n";
    }
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
            Serial << "\033[1D \033[1D";
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
