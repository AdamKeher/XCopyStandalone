#include "XCopyDebug.h"

XCopyDebug::XCopyDebug(XCopyGraphics *graphics, XCopyAudio *audio, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin)
{
    _graphics = graphics;
    _audio = audio;
    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;
    _cardDetectPin = cardDetectPin;
}

void XCopyDebug::debugCompareFile(FatFile sdFile, SerialFlashFile flashFile)
{
    unsigned long n = sdFile.fileSize();
    char sBuffer[256];
    char fBuffer[256];
    uint16_t errorCount = 0;

    char lfnBuffer[256];
    char sfnBuffer[20];
    sdFile.getName(lfnBuffer, 256);
    sdFile.getSFN(sfnBuffer);
    strupr(sfnBuffer);
    Serial << lfnBuffer << " (" << sfnBuffer << ") "
           << "\t\tSD: " << sdFile.fileSize() << "\t\tFlash: " << flashFile.size() << "\t\t";

    // FIX: Compensate for boundary error for erasable files
    // if (sdFile.size() != flashFile.size())
    // {
    //     Serial << "File Sizes dont match.";
    //     errorCount++;
    // }

    while (n > 0)
    {
        unsigned long rd = n;
        if (rd > sizeof(sBuffer))
            rd = sizeof(sBuffer);
        sdFile.read(sBuffer, rd);
        flashFile.read(fBuffer, rd);

        if (memcmp(sBuffer, fBuffer, 256) != 0)
            errorCount++;

        n = n - rd;
    }
    Serial << (errorCount == 0 ? "OK" : "FAILED: " + String(errorCount)) << "\r\n";
}

void XCopyDebug::debugCompareTempFile()
{
    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";
    Serial << " XCopy Board Test v0.5                                 (c)2019 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "[start]===================================================================\r\n";

    Serial << "\r\nCompare Temp File\r\n";

    if (!SD.begin(_sdCSPin))
    {
        Serial << "\033[31mSD Card initialization failed!\033[0m\r\n";
        _audio->playBong(true);
        return;
    }

    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "\033[31mSPI Flash initialization failed!\033[0m\r\n";
        _audio->playBong(true);
        return;
    }

    File sdFile = SD.open("XCOPY.ADF");
    SerialFlashFile flashFile = SerialFlash.open("DISKCOPY.TMP");
    debugCompareFile(sdFile, flashFile);
}

// FIX: SdFat seems to have broken SerialFile create
void XCopyDebug::debugEraseCopyCompare(bool erase)
{
    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";
    Serial << " XCopy Board Test v0.5                                 (c)2019 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "[start]===================================================================\r\n";

    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "Serial Flash initialization failed.\r\n";
        return;
    }

    if (erase)
    {
        Serial << "\r\nErase Flash\r\n";
        Serial << "==========================================================================\r\n";

        flashDetails();

        unsigned char buffer[256];
        unsigned long chipsize;

        SerialFlash.readID(buffer);
        chipsize = SerialFlash.capacity(buffer);
        if (chipsize > 0)
        {
            Serial << "\r\nErasing ...";
            SerialFlash.eraseAll();

            unsigned long dotMillis = millis();
            unsigned char dotcount = 0;
            while (!SerialFlash.ready())
            {
                if (millis() - dotMillis > 1000)
                {
                    dotMillis = dotMillis + 1000;
                    // FIX: missing dots
                    Serial << ".";
                    dotcount = dotcount + 1;
                    if (dotcount >= 60)
                    {
                        Serial << "\r\n";
                        dotcount = 0;
                    }
                }
            }
            Serial << "\r\nErase completed\r\n";
        }
        else
        {
            Serial << "Aborted as Flash not reporting a correct size\r\n";
            return;
        }

        Serial << "\r\nCopy SD Root Files to Flash\r\n";
        Serial << "==========================================================================\r\n";

        if (!SD.begin(_sdCSPin))
        {
            Serial << "SD Card initialization failed.\r\n";
            return;
        }

        SdFile root;
        root.open("/");

        while (1)
        {
            // open a file from the SD card
            SdFile f;
            
            if (!f.openNext(&root, O_RDONLY))
                break;

            if (f.isDir())
                continue;

            char sfnBuffer[20];
            char lfnBuffer[255];
            f.getSFN(sfnBuffer);
            f.getName(lfnBuffer, 255);
            strupr(sfnBuffer);
            unsigned long length = f.fileSize();

            Serial << lfnBuffer << " (" << sfnBuffer << ")\t\t" << length << "\t\tcopying ";

            bool result;
            if (String(sfnBuffer) == "DISKCOPY.TMP")
            {
                Serial << "(createErasable) ";
                result = SerialFlash.createErasable(sfnBuffer, length);
            }
            else
            {
                Serial << "(notErasable) ";
                result = SerialFlash.create(sfnBuffer, length);
            }

            // create the file on the Flash chip and copy data
            if (result)
            {
                SerialFlashFile ff = SerialFlash.open(sfnBuffer);
                if (ff)
                {
                    // copy data loop
                    unsigned long count = 0;
                    uint8_t dotcount = 0;
                    while (count < length)
                    {
                        char buf[256];
                        uint16_t n;
                        n = f.read(buf, 256);
                        ff.write(buf, n);

                        count = count + n;
                        if (++dotcount > 400)
                        {
                            // FIX: missing dots
                            Serial << ".";
                            dotcount = 0;
                        }
                    }
                }
                else
                {
                    Serial << ("  Error opening freshly created file!");
                }
                ff.close();
            }
            else
            {
                Serial << "  unable to create file";
            }

            f.close();

            Serial << "\r\n";
        }

        root.close();
        Serial << "Finished All Files\r\n";
    }



    Serial << "\r\nList All Flash Files\r\n";
    Serial << "==========================================================================\r\n";

    SerialFlash.opendir();
    while (1)
    {
        char filename[20];
        uint32_t filesize;

        if (SerialFlash.readdir(filename, sizeof(filename), filesize))
            Serial << filename << "\t\t" << filesize << " bytes\r\n";
        else
            break; // no more files
    }

    return;

    Serial << "\r\nCompare SPI and Flash Files\r\n";
    Serial << "==========================================================================\r\n";

    if (!SD.begin(_sdCSPin))
    {
        Serial << "\033[31mSD Card initialization failed!\033[0m\r\n";
        _audio->playBong(true);
        return;
    }

    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "\033[31mSPI Flash initialization failed!\033[0m\r\n";
        _audio->playBong(true);
        return;
    }

    SdFile root; 
    root.open("/");

    while (true)
    {
        SdFile entry;

        if (!entry.openNext(&root, O_RDONLY))
        {
            // no more files
            break;
        }
        else
        {
            if (entry.isDir())
            {
                entry.close();
                continue;
            }

            char sfnBuffer[20];
            entry.getSFN(sfnBuffer);
            strupr(sfnBuffer);
            SerialFlashFile flashFile = SerialFlash.open(sfnBuffer);

            debugCompareFile(entry, flashFile);

            flashFile.close();
            entry.close();
        }
    }

    root.close();

    Serial << "\r\nErase and Copy Complete\r\n";
}

void XCopyDebug::debug()
{
    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";
    Serial << " XCopy Board Test v0.5                                 (c)2019 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "[start]===================================================================\r\n";
    Serial << "\033[96mTest Cycle: " + String(++_cycles) + "\033[0m\r\n";
    Serial << "\033[96mFlash Errors:   " + String(_flashErrorCount) + " / " + String(_flashReadCount) + "\033[0m\r\n";
    Serial << "\033[96mSD Card Errors: " + String(_sdcardErrorCount) + " / " + String(_sdcardReadCount) + "\033[0m\r\n";

    _cardState = cardDetect();
    Serial << "\033[96mCard Detect:    ";
    Serial << (_cardState ? "TRUE" : "FALSE") << "\r\n";
    Serial << "\033[0m==========================================================================\r\n";

    _graphics->clearScreen();
    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 0);
    String text = "Test Cycle: " + String(_cycles) + "\nFlash Errors: " + String(_flashErrorCount) + "/" + String(_flashReadCount) + "\nSD Card Errors: " + String(_sdcardErrorCount) + "/" + String(_sdcardReadCount) + "\nCard Detect: " + String(_cardState);

    _graphics->drawText(0, 45, ST7735_WHITE, text);

    Serial << "\n--------------------------------------------------------------------------\r\n";
    Serial << "\033[96mSD Card Test:\033[0m\r\n";
    Serial << "--------------------------------------------------------------------------\r\n";
    if (_cardState)
    {
        if (!SD.begin(_sdCSPin))
        {
            Serial << "\033[31mSD Card initialization failed!\033[0m\r\n";
            _audio->playBong(true);
            _sdcardErrorCount++;
        }
        else
        {
            _cardInit = true;
            Serial << "\033[32mSD Card initialized!\033[0m\r\n";
            printDir();
        }
    }
    else
    {
        Serial.println("\033[32mSD Card ejected!\033[0m");
    }

    Serial << "\n--------------------------------------------------------------------------\r\n";
    Serial << "\033[96mSPI Flash Test:\033[0m\r\n";
    Serial << "--------------------------------------------------------------------------\r\n";
    if (!SerialFlash.begin(_flashCSPin))
    {
        _flashErrorCount++;
        Serial << "\033[31mSPI Flash initialization failed!\033[0m\r\n";
    }
    else
    {
        Serial << "\033[32mSPI Flash initialized!\033[0m\r\n";
        flashTest();
    }

    Serial << "[end]===================================================================\r\n";

    _audio->playBoing(true);
}

bool XCopyDebug::cardDetect()
{
    return digitalRead(_cardDetectPin) == 0 ? true : false;
}

// TODO: convert to SdFile
void XCopyDebug::printDirectory(File dir, uint8_t numTabs)
{
    String text;
    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
        {
            // no more files
            break;
        }
        _sdcardReadCount++;

        for (uint8_t i = 0; i < numTabs; i++)
        {
            Serial << "\t";
        }
        char nBuffer[256];
        entry.getName(nBuffer, 256);
        Serial << nBuffer;
        if (entry.isDirectory())
        {
            Serial << "/\r\n";
            printDirectory(entry, numTabs + 1);
        }
        else
        {
            // files have sizes, directories do not
            Serial << "\t\t";
            Serial.print(entry.size(), DEC);
        }

        if ((_mode == sdcard) || (_mode == both))
        {
            if (entry)
            {
                unsigned long usbegin = micros();
                unsigned long n = entry.size();
                char buffer[256];
                while (n > 0)
                {
                    unsigned long rd = n;
                    if (rd > sizeof(buffer))
                        rd = sizeof(buffer);
                    entry.read(buffer, rd);
                    n = n - rd;
                }
                unsigned long usend = micros();
                Serial << ", read in ";
                Serial << usend - usbegin;
                Serial << " us, speed = ";
                Serial << (float)entry.size() * 1000.0 / (float)(usend - usbegin);
                Serial << " kbytes/sec";

                text = buffer;
                text = text + ", " + String((float)entry.size() * 1000.0 / (float)(usend - usbegin)) + " kbytes/sec";
                _graphics->drawText(0, 90, ST7735_RED, "SD Card:", true);
                _graphics->drawText(0, 100, ST7735_GREEN, text, true);

                entry.close();
            }
            else
            {
                Serial << "\033[31m error reading this file!\033[0m";
                _audio->playBong(true);
                _sdcardErrorCount++;
            }
        }

        Serial << "\r\n";
        entry.close();
    }
}

void XCopyDebug::printDir()
{
    Serial << String(_cycles) + " - WP: " + String(digitalRead(3)) + " CD: " + String(digitalRead(4)) << "\r\n";
    Serial << "Directory:\r\n";
    File root = SD.open("/");
    printDirectory(root, 0);
    root.close();
}

void XCopyDebug::flashTest()
{
    SerialFlash.opendir();
    uint16_t filecount = 0;
    while (1)
    {
        char filename[64];
        uint32_t filesize;
        String text;

        if (SerialFlash.readdir(filename, sizeof(filename), filesize))
        {
            _flashReadCount++;
            Serial << "  ";
            Serial << filename;
            Serial << ", ";
            Serial << filesize;
            Serial << " bytes";
            SerialFlashFile file = SerialFlash.open(filename);

            text = String(filename);

            if (_mode == flash || _mode == both)
            {
                if (file)
                {
                    unsigned long usbegin = micros();
                    unsigned long n = filesize;
                    char buffer[256];
                    while (n > 0)
                    {
                        unsigned long rd = n;
                        if (rd > sizeof(buffer))
                            rd = sizeof(buffer);
                        file.read(buffer, rd);
                        n = n - rd;
                    }
                    unsigned long usend = micros();
                    Serial << ", read in ";
                    Serial << usend - usbegin;
                    Serial << " us, speed = ";
                    Serial << (float)filesize * 1000.0 / (float)(usend - usbegin);
                    Serial << " kbytes/sec";

                    text = text + ", " + String((float)filesize * 1000.0 / (float)(usend - usbegin)) + " kbytes/sec";

                    _graphics->drawText(0, 90, ST7735_RED, "Flash", true);
                    _graphics->drawText(0, 100, ST7735_GREEN, text, true);

                    file.close();
                }
                else
                {
                    Serial << "\033[31m error reading this file!\033[0m";
                    _audio->playBong(true);
                    _flashErrorCount++;
                }
            }
            filecount = filecount + 1;
        }
        else
        {
            if (filecount == 0)
            {
                _flashErrorCount++;
                Serial << "\033[31mNo files found in sdcard memory.\033[0m";
                _audio->playBong(true);
            }
            break; // no more files
        }
        Serial << "\r\n";
    }
}

void XCopyDebug::flashDetails()
{
    unsigned char buffer[256];
    unsigned long chipsize, blocksize;

    Serial << "\r\n";

    if (!SerialFlash.begin(_flashCSPin))
        Serial << "Flash Initialization Failed\r\n";
    else
        Serial << "Flash Initialized\r\n";

    SerialFlash.readID(buffer);
    chipsize = SerialFlash.capacity(buffer);
    blocksize = SerialFlash.blockSize();

    Serial << "JEDEC ID: ";
    Serial.print(buffer[0], HEX);
    Serial << " ";
    Serial.print(buffer[1], HEX);
    Serial << " ";
    Serial.println(buffer[2], HEX);
    Serial << "Part Number: ";

    if (buffer[0] == 0xEF)
    {
        // Winbond
        if (buffer[1] == 0x40)
        {
            if (buffer[2] == 0x14)
                Serial << "W25Q80BV";
            if (buffer[2] == 0x15)
                Serial << "W25Q16DV";
            if (buffer[2] == 0x17)
                Serial << "W25Q64FV";
            if (buffer[2] == 0x18)
                Serial << "W25Q128FV";
            if (buffer[2] == 0x19)
                Serial << "W25Q256FV";
        }
    }
    if (buffer[0] == 0x01)
    {
        // Spansion
        if (buffer[1] == 0x02)
        {
            if (buffer[2] == 0x16)
                Serial << "S25FL064A";
            if (buffer[2] == 0x19)
                Serial << "S25FL256S";
            if (buffer[2] == 0x20)
                Serial << "S25FL512S";
        }
        if (buffer[1] == 0x20)
        {
            if (buffer[2] == 0x18)
                Serial << "S25FL127S";
        }
    }
    if (buffer[0] == 0xC2)
    {
        // Macronix
        if (buffer[1] == 0x20)
        {
            if (buffer[2] == 0x18)
                Serial << "MX25L12805D";
        }
    }
    if (buffer[0] == 0x20)
    {
        // Micron
        if (buffer[1] == 0xBA)
        {
            if (buffer[2] == 0x20)
                Serial << "N25Q512A";
            if (buffer[2] == 0x21)
                Serial << "N25Q00AA";
        }
        if (buffer[1] == 0xBB)
        {
            if (buffer[2] == 0x22)
                Serial << "MT25QL02GC";
        }
    }
    if (buffer[0] == 0xBF)
    {
        // SST
        if (buffer[1] == 0x25)
        {
            if (buffer[2] == 0x02)
                Serial << "SST25WF010";
            if (buffer[2] == 0x03)
                Serial << "SST25WF020";
            if (buffer[2] == 0x04)
                Serial << "SST25WF040";
            if (buffer[2] == 0x41)
                Serial << "SST25VF016B";
            if (buffer[2] == 0x4A)
                Serial << "SST25VF032";
        }
        if (buffer[1] == 0x25)
        {
            if (buffer[2] == 0x01)
                Serial << "SST26VF016";
            if (buffer[2] == 0x02)
                Serial << "SST26VF032";
            if (buffer[2] == 0x43)
                Serial << "SST26VF064";
        }
    }

    Serial << "\r\n";
    Serial << "Memory Size: " << chipsize << " bytes\r\n";
    Serial << "Block Size: " << blocksize << " bytes";
    Serial << "\r\n";
}
