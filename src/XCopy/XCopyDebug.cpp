#include "XCopyDebug.h"

XCopyDebug::XCopyDebug(XCopyGraphics *graphics, XCopyAudio *audio, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin)
{
    _graphics = graphics;
    _audio = audio;
    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;
    _cardDetectPin = cardDetectPin;
}

void XCopyDebug::debugCompareFile(File sdFile, SerialFlashFile flashFile)
{
    unsigned long n = sdFile.size();
    char sBuffer[256];
    char fBuffer[256];
    uint16_t errorCount = 0;

    Serial << sdFile.name() << "\t\tSD: " << sdFile.size() << "\t\tFlash: " << flashFile.size() << "\t\t";

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

void XCopyDebug::debugEraseCopyCompare(bool erase)
{
    Serial << "\033[2J\033[H\033[95m\033[106m";
    Serial << "                                                                          \r\n";
    Serial << " XCopy Board Test v0.5                                 (c)2019 Adam Keher \r\n";
    Serial << "                                                                          \033[0m\r\n";
    Serial << "[start]===================================================================\r\n";

    if (!SD.begin(_sdCSPin))
    {
        Serial << "SD Card initialization failed.\r\n";
        return;
    }

    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "Serial Flash initialization failed.\r\n";
        return;
    }

    File rootdir;

    if (erase)
    {
        Serial << "\r\nErase Flash\r\n";
        Serial << "==========================================================================\r\n";

        unsigned char id[5];
        SerialFlash.readID(id);
        unsigned long size = SerialFlash.capacity(id);

        if (size > 0)
        {
            Serial << "Flash Memory has " << size << " bytes.\r\n";
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

        rootdir = SD.open("/");

        while (1)
        {
            // open a file from the SD card
            File f = rootdir.openNextFile();
            if (!f)
                break;

            if (f.isDirectory())
                continue;

            const char *filename = f.name();
            unsigned long length = f.size();

            Serial << filename << "\t\t" << length << "\t\tcopying ";

            bool result;
            if (String(filename) == "DISKCOPY.TMP")
            {
                Serial << " (createErasable) ";
                result = SerialFlash.createErasable(filename, length);
            }
            else
            {
                Serial << " (notErasable) ";
                result = SerialFlash.create(filename, length);
            }

            // create the file on the Flash chip and copy data
            if (result)
            {
                SerialFlashFile ff = SerialFlash.open(filename);
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
                    ff.close();
                }
                else
                {
                    Serial << ("  Error opening freshly created file!");
                }
            }
            else
            {
                Serial.println("  unable to create file");
            }

            Serial << "\r\n";
            f.close();
        }

        rootdir.close();
        Serial << "Finished All Files\r\n";
    }

    Serial << "\r\nList All Flash Files\r\n";
    Serial << "==========================================================================\r\n";

    SerialFlash.opendir();
    while (1)
    {
        char filename[64];
        uint32_t filesize;

        if (SerialFlash.readdir(filename, sizeof(filename), filesize))
            Serial << filename << "\t\t" << filesize << " bytes\r\n";
        else
            break; // no more files
    }

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

    rootdir = SD.open("/");

    while (true)
    {
        File entry = rootdir.openNextFile();

        if (!entry)
        {
            // no more files
            break;
        }
        else
        {
            if (entry.isDirectory())
            {
                entry.close();
                continue;
            }

            SerialFlashFile flashFile = SerialFlash.open(entry.name());
            debugCompareFile(entry, flashFile);
            flashFile.close();

            entry.close();
        }
    }

    rootdir.close();

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
    _graphics->bmpDraw("XCOPY.BMP", 0, 0);
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
        Serial << entry.name();
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

                text = entry.name();
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
