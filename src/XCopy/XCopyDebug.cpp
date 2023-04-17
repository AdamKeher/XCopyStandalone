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

    char sfnBuffer[20];
    char lfnBuffer[256];
    char sfnTempBuffer[20];
    sdFile.getName(lfnBuffer, 256);
    sdFile.getSFN(sfnBuffer);
    strupr(sfnBuffer);
    sprintf(lfnBuffer, "%-40s", lfnBuffer);
    sprintf(sfnTempBuffer, "(%s)", sfnBuffer);
    sprintf(sfnTempBuffer, "%-20s", sfnTempBuffer);

    Serial << lfnBuffer << " " << sfnTempBuffer
           << " SD: " << sdFile.fileSize() << "\t\tFlash: " << flashFile.size() << "\t\t";

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
    printBanner("Compare Temp File");

    Serial << "\r\nCompare Temp File\r\n";

    if (!initSdCard()) return;
    if (!initSerialFlash()) return;

    File sdFile = SD.open("XCOPY.ADF");
    SerialFlashFile flashFile = SerialFlash.open("DISKCOPY.TMP");
    debugCompareFile(sdFile, flashFile);
}

void XCopyDebug::debugEraseFlash()
{
    printBanner("Erase Flash");
    
    initSerialFlash();

    printHeading("Reading Flash Details");
    flashDetails();
    
    printHeading("Erasing Flash");
    eraseFlash();
}

void XCopyDebug::debugFaultFind() {
    size_t fileBufferSize = 256;
    unsigned long count = 0;

    printBanner("Fault Find");

    // show flash details
    printHeading("Flash Details");
    if (!initSerialFlash()) return;
    flashDetails();

    // erase flash chip
    printHeading("Erase Flash");
    eraseFlash();

    // write files from sd card to flash
    printHeading("Write Files");

    // init flash & sdcard
    if (!initSdCard()) return;
    if (!initSerialFlash()) return;

    // open sd file
    SdFile f;
    bool fresult = f.open("TEST.TXT");
    if (!fresult) {
        Serial << "SD file open failed";
        return;
    }
    unsigned long length = f.fileSize();
    Serial << "Name: " << f.printName() << " Size: " << f.fileSize() << "\r\n";

    // create & open serial flash file
    bool ffresult = SerialFlash.create("TEST.TXT", length);
    if (!ffresult) {
        Serial << "Flash file creation failed";
        return;
    }
    SerialFlashFile ff = SerialFlash.open("TEST.TXT");
    if (!ff)
    {
        Serial << "Flash file open failed";
        return;
    }

    Serial << "Buffer Size: " << fileBufferSize << "\r\n";

    // copy data from sd file to flash file
    count = 0;
    while (count < length)
    {
        char buf[fileBufferSize];
        uint16_t n;
        n = f.read(buf, fileBufferSize);
        ff.write(buf, n);

        count = count + n;
        for (size_t i = 0; i < n; i++)
        {
            Serial.print(buf[i]);
        }

        delay(2);
    }

    ff.close();
    f.close(); 

    delay(1000);

    printHeading("Read Files");

    // open flash file
    SerialFlashFile ff2 = SerialFlash.open("TEST.TXT");
    if (!ff2)
    {
        Serial << "Flash file open failed";
    }
    Serial << "Name: TEST.TXT" << " Size: " << ff2.size() << "\r\n";

    // read data from flash file
    count = 0;
    while (count < ff2.size())
    {
        char buf[fileBufferSize];
        uint16_t n;
        n = ff2.read(buf, fileBufferSize);

        count = count + n;
        for (size_t i = 0; i < n; i++)
        {
            Serial.print(buf[i]);
        }
    } 
    ff2.close();   
}

bool XCopyDebug::initSerialFlash() {
    if (!SerialFlash.begin(_flashCSPin))
    {
        Serial << "Serial Flash initialization failed.\r\n";
        _audio->playBong(true);
        return false;
    }
    return true;
}

bool XCopyDebug::initSdCard() {
    if (!SD.begin(_sdCSPin))
    {
        Serial << "SD Card initialization failed.\r\n";
        _audio->playBong(true);
        return false;
    }
    return true;
}

// FIX: SdFat seems to have broken SerialFile create
void XCopyDebug::debugEraseCopyCompare()
{
    size_t fileBufferSize = 256;
    int delayMs = 5;

    printBanner("Erase Flash, Copy SD Files to Flash and Compare Flash to SD");

    // show flash details
    printHeading("Flash Details");
    if (!initSerialFlash()) return;
    unsigned long chipSize = flashDetails();
    
    // erase flash chip
    printHeading("Erase Flash");
    eraseFlash();

    // test file size / free flash space
    printHeading("Check flash and SD file size");
    unsigned long totalSize = calculateSize();
    Serial << "Calculating file sizes ... ";
    Serial << "SD File Size: " << totalSize << " SPI Ram Size: " << chipSize << "\r\n";

    if (totalSize >= chipSize) {
        Serial << "Error: SD File size too large.";
        return;
    } else {
        Serial << "OK: SD Files fit on SPI Flash.\r\n";
    }

    // copy files
    printHeading("Copy SD Root Files to Flash");

    if (!initSdCard()) return;
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
        char sfnTempBuffer[20];
        f.getSFN(sfnBuffer);
        f.getName(lfnBuffer, 255);
        strupr(sfnBuffer);
        sprintf(lfnBuffer, "%-40s", lfnBuffer);
        sprintf(sfnTempBuffer, "(%s)", sfnBuffer);
        sprintf(sfnTempBuffer, "%-20s", sfnTempBuffer);
        unsigned long length = f.fileSize();

        Serial << lfnBuffer << " " << sfnTempBuffer << "\t" << length << "\t\tcopying ";

        bool result = false;
        delay(delayMs);

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
                    char buf[fileBufferSize];
                    uint16_t n;
                    n = f.read(buf, fileBufferSize);
                    ff.write(buf, n);

                    count = count + n;
                    if (++dotcount > 400)
                    {
                        // FIX: missing dots
                        Serial << ".";
                        dotcount = 0;
                    }

                    delay(delayMs);
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

    printHeading("List All Flash Files");
    listFlashFiles();

    printHeading("Compare SPI and Flash Files");
    compareFiles();

    Serial << "\r\nErase and Copy Complete\r\n";
}

void XCopyDebug::debugCompare() {
    printBanner("Compare Flash to SD");
    printHeading("Compare SPI and Flash Files");
    compareFiles();
}

void XCopyDebug::debugFlashDetails() {
    printBanner("Flash Details");
    printHeading("Reading Flash Info");
    initSerialFlash();
    flashDetails();
}

void XCopyDebug::debugTestFlashSD()
{
    printBanner("Test Flash & SD Card");

    Serial << XCopyConsole::high_cyan() << "    Test Cycle: " << XCopyConsole::reset() << String(++_cycles) + "\r\n";
    Serial << XCopyConsole::high_cyan() << "  Flash Errors: " << XCopyConsole::reset() << String(_flashErrorCount) + " / " + String(_flashReadCount) + "\r\n";
    Serial << XCopyConsole::high_cyan() << "SD Card Errors: " << XCopyConsole::reset() << String(_sdcardErrorCount) + " / " + String(_sdcardReadCount) + "\r\n";

    _cardState = cardDetect();
    Serial << XCopyConsole::high_cyan() << "Card Detect:    ";
    Serial << (_cardState ? "TRUE" : "FALSE") << "\r\n";
    Serial << XCopyConsole::reset() << "==========================================================================\r\n";

    _graphics->clearScreen();
    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 0);
    String text = "Test Cycle: " + String(_cycles) + "\nFlash Errors: " + String(_flashErrorCount) + "/" + String(_flashReadCount) + "\nSD Card Errors: " + String(_sdcardErrorCount) + "/" + String(_sdcardReadCount) + "\nCard Detect: " + String(_cardState);

    _graphics->drawText(0, 45, ST7735_WHITE, text);

    Serial << "\n--------------------------------------------------------------------------\r\n";
    Serial << XCopyConsole::high_cyan() << "SD Card Test:\r\n" << XCopyConsole::reset();
    Serial << "--------------------------------------------------------------------------\r\n";
    if (_cardState)
    {
        if (!SD.begin(_sdCSPin))
        {
            Serial << XCopyConsole::error("SD Card initialization failed!\r\n");
            _audio->playBong(true);
            _sdcardErrorCount++;
        }
        else
        {
            _cardInit = true;
            Serial << XCopyConsole::success("SD Card initialized!\r\n");
            SdTest();
        }
    }
    else
    {
        Serial << XCopyConsole::success("SD Card ejected!");
    }

    Serial << "\n--------------------------------------------------------------------------\r\n";
    Serial << XCopyConsole::high_cyan() << "SPI Flash Test:\r\n" << XCopyConsole::reset();
    Serial << "--------------------------------------------------------------------------\r\n";
    if (!SerialFlash.begin(_flashCSPin)) {
        _flashErrorCount++;
        Serial << XCopyConsole::error("SPI Flash initialization failed!\r\n");
    }
    else {
        Serial << XCopyConsole::success("SPI Flash initialized.\r\n");
        flashTest();
    }

    Serial << "[end]===================================================================\r\n";

    _audio->playBoing(true);
}

unsigned long XCopyDebug::calculateSize() {
    if (!initSdCard()) return 0;

    unsigned long totalSize = 0;

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

        totalSize += f.fileSize();

        f.close();
    }
    root.close();

    return totalSize;
}

bool XCopyDebug::eraseFlash() {
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
        return false;
    }

    return true;
}

bool XCopyDebug::cardDetect()
{
    return digitalRead(_cardDetectPin) == 0 ? true : false;
}

void XCopyDebug::printBanner(const char* heading) {
    Serial << XCopyConsole::clearscreen() << XCopyConsole::home() << XCopyConsole::high_purple() << XCopyConsole::high_cyan();
    Serial << "                                                                          \r\n";
    Serial << " XCopy Board Test " << debugVersion << "                                 (c)" << debugYear << " Adam Keher \r\n";
    Serial << "                                                                          \r\n" << XCopyConsole::reset();
    Serial << "==========================================================================\r\n";
    Serial << ">> " << heading << " <<\r\n";
    Serial << "==========================================================================\r\n";
}

void XCopyDebug::printHeading(const char* heading) {
    Serial << "\r\n> " << XCopyConsole::success(heading) << " <\r\n";
    Serial << "--------------------------------------------------------------------------\r\n";
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
                Serial << XCopyConsole::error(" error reading this file!");
                _audio->playBong(true);
                _sdcardErrorCount++;
            }
        }

        Serial << "\r\n";
        entry.close();
    }
}

void XCopyDebug::listFlashFiles() {
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
}

void XCopyDebug::compareFiles() {
    if (!initSdCard()) return;
    if (!initSerialFlash()) return;

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
}

void XCopyDebug::SdTest()
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
                    Serial << XCopyConsole::error(" error reading this file!\r\n");
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
                Serial << XCopyConsole::error("No files found in spi flash memory.\r\n");
                _audio->playBong(true);
            }
            break; // no more files
        }
        Serial << "\r\n";
    }
}

unsigned long XCopyDebug::flashDetails()
{
    unsigned char buffer[256];
    unsigned long chipsize, blocksize;

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
    if (buffer[0] == 0xC8)
    {
        // GigaDevice
        if (buffer[1] == 0x40)
        {
            if (buffer[2] == 0x17)
                Serial << "GD25Q64CSIGR";
        }
    }
    Serial << "\r\n";
    Serial << "Memory Size: " << chipsize << " bytes\r\n";
    Serial << "Block Size: " << blocksize << " bytes";
    Serial << "\r\n";

    return chipsize;
}
