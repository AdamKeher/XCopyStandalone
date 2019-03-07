#include "XCopyDisk.h"

// #define XCOPY_DEBUG 1

XCopyDisk::XCopyDisk()
{
}

void XCopyDisk::begin(XCopyGraphics *graphics, XCopyAudio *audio, uint8_t sdCSPin, uint8_t flashCSPin, uint8_t cardDetectPin)
{
    _graphics = graphics;
    _audio = audio;
    _sdCSPin = sdCSPin;
    _flashCSPin = flashCSPin;
    _cardDetectPin = cardDetectPin;

    setupDrive();
}

void XCopyDisk::changeDisk()
{
    _graphics->clearScreen();
    _graphics->drawText(0, 60, ST7735_YELLOW, "         Change Disk");

    bool diskInserted = diskChange();
    while (diskInserted)
    {
        diskInserted = diskChange();
        delay(500);
    }

    _graphics->drawText(0, 60, ST7735_GREEN, "  Disk Ejected. Waiting.", true);

    diskInserted = diskChange();
    while (!diskInserted)
    {
        diskInserted = diskChange();
        delay(3000);
    }
}

void XCopyDisk::readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount)
{
    int retries = 0;
    int errors = -1;

    while (errors == -1 && retries < retryCount)
    {
        // read track
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_YELLOW);
        gotoLogicTrack(trackNum);
        errors = readTrack(true);

        if (errors != -1)
        {
            _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_GREEN);
        }
        else
        {
            _graphics->drawTrack(trackNum / 2, trackNum % 2, true, true, retries + 1, verify, ST7735_RED);
            Serial << "Read failed! - Try: << " << retries + 1 << "\r\n";
            retries++;
            _audio->playBong(false);
            delay(1000);
        }
    }

    if (errors == -1)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_RED);
    else if (errors == 0 && retries > 0)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_ORANGE);
}

void XCopyDisk::writeDiskTrack(uint8_t trackNum, uint8_t retryCount)
{
    int retries = 0;
    int errors = -1;

    while (errors == -1 && retries < retryCount)
    {
        // write track
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_YELLOW);
        gotoLogicTrack(trackNum);
        errors = writeTrack();

        if (errors != -1)
        {
            _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_GREEN);
        }
        else
        {
            _graphics->drawTrack(trackNum / 2, trackNum % 2, true, true, retries + 1, false, ST7735_RED);
            Serial << "Write failed! - Try: << " << retries + 1 << "\r\n";
            retries++;
            _audio->playBong(false);
            delay(1000);
        }
    }

    if (errors == -1)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_RED);
    else if (errors == 0 && retries > 0)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_ORANGE);
}

void XCopyDisk::diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination)
{
    _cancelOperation = false;

    _graphics->drawDiskName("");
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_WHITE, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    String diskName = getName();
    _graphics->drawDiskName(diskName);

    char buffer[ADFFileName.length() + 1];
    memset(buffer, 0, sizeof(buffer));
    ADFFileName.toCharArray(buffer, sizeof(buffer));

    File ADFFile;
    SerialFlashFile ADFFlashFile;

    // Open SD File or SerialFile
    if (destination == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "SD Init Failed");
            _audio->playBong(false);
            return;
        }

        if (SD.exists(buffer))
            SD.remove(buffer);

        ADFFile = SD.open(buffer, FILE_WRITE);

        if (!ADFFile)
        {
            ADFFile.close();
            _graphics->drawText(0, 10, ST7735_WHITE, "SD File Open Failed");
            _audio->playBong(false);

            return;
        }
    }
    else if (destination == _flashMemory)
    {
        if (!SerialFlash.begin(_flashCSPin))
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "Serial Flash Init Failed");
            _audio->playBong(false);
            return;
        }

        ADFFlashFile = SerialFlash.open(buffer);

        _graphics->drawText(0, 10, ST7735_ORANGE, "Erasing Flash...", true);

        ADFFlashFile.erase();

        while (!SerialFlash.ready())
        {
        }

        _graphics->drawText("Done");

        ADFFlashFile = SerialFlash.open(buffer);
        if (!ADFFlashFile)
        {
            ADFFlashFile.close();
            _graphics->drawText(0, 10, ST7735_WHITE, "Serial Flash File Open Failed");
            _audio->playBong(false);
            return;
        }

        ADFFlashFile.seek(0);
    }

    // YELLOW = Begin, GREEN = Read OK, ORANGE = Read after Retries, RED = Read Error, MAGENTA = Verify Error
    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);
            _graphics->drawDisk(trackNum, ST7735_RED);
            _audio->playBong(false);
            return;
        }

        // read track
        readDiskTrack(trackNum, false, retryCount);

        // write track (11 sectors per track)
        for (int i = 0; i < 11; i++)
        {
            const struct Sector *aSec = (Sector *)&getTrack()[i].sector;
            if (destination == _sdCard)
            {
                ADFFile.write(aSec->data, 512);
            }
            else if (destination == _flashMemory)
            {
                ADFFlashFile.write(aSec->data, 512);
                ADFFlashFile.flush();
                while (!SerialFlash.ready())
                {
                }
            }
        }

        // verify
        if (verify)
        {
            // reread track to compare to file
            readDiskTrack(trackNum, true, retryCount);

            bool compareError = false;
            char buffer[512];

            // move file pointer pack 1 track
            if (destination == _sdCard)
                ADFFile.seek(ADFFile.position() - 11 * 512);
            else if (destination == _flashMemory)
                ADFFlashFile.seek(ADFFlashFile.position() - 11 * 512);

            // compare sectors to file
            for (int i = 0; i < 11; i++)
            {
                const struct Sector *aSec = (Sector *)&getTrack()[i].sector;

                if (destination == _sdCard)
                    ADFFile.read(buffer, sizeof(buffer));
                else if (destination == _flashMemory)
                    ADFFlashFile.read(buffer, sizeof(buffer));

                if (memcmp(aSec->data, buffer, 512))
                    compareError = true;
            }

            if (compareError)
            {
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, true, ST7735_MAGENTA);
                _audio->playBong(false);
            }
            else
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, true, ST7735_GREEN);
        }
    }

    ADFFile.close();
    ADFFlashFile.close();
    _audio->playBoing(false);
}

void XCopyDisk::adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source)
{
    _cancelOperation = false;

    if (ADFFileName == "")
        return;

    _graphics->drawDiskName(ADFFileName.substring(ADFFileName.lastIndexOf("/") + 1));
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_WHITE, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    if (getWriteProtect())
    {
        _graphics->drawText(0, 10, ST7735_WHITE, "Disk Write Protected");
        _audio->playBong(false);
        return;
    }

    char buffer[ADFFileName.length() + 1];
    memset(buffer, 0, sizeof(buffer));
    ADFFileName.toCharArray(buffer, sizeof(buffer));

    File ADFFile;
    SerialFlashFile ADFFlashFile;

    if (source == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "SD Init Failed");
            _audio->playBong(false);
            return;
        }

        ADFFile = SD.open(buffer, FILE_READ);
        if (!ADFFile)
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "SD File Open Failed");
            _audio->playBong(false);
            ADFFile.close();
            return;
        }
    }
    else if (source == _flashMemory)
    {
        if (!SerialFlash.begin(_flashCSPin))
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "Serial Flash Init Failed");
            _audio->playBong(false);
            return;
        }

        ADFFlashFile = SerialFlash.open(buffer);
        if (!ADFFlashFile)
        {
            _graphics->drawText(0, 10, ST7735_WHITE, "Serial Flash File Open Failed");
            _audio->playBong(false);
            ADFFlashFile.close();
            return;
        }

        ADFFlashFile.seek(0);
    }

    setAutoDensity(false);
    setMode(DD); // DD
    delay(5);
    setCurrentTrack(-1);

    if (diskChange() == 1)
    {
        motorOn();
        seek0();
    }
    else
    { /* wait for disk insertion */
    }
    delay(100);

    // write track
    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);
            _graphics->drawDisk(trackNum, ST7735_RED);
            _audio->playBong(false);
            return;
        }

        // read track from ADF file into track buffer
        for (int i = 0; i < 11; i++)
        {
            byte buffer[512];

            if (source == _sdCard)
                ADFFile.read(buffer, sizeof(buffer));
            else if (source == _flashMemory)
                ADFFlashFile.read(buffer, sizeof(buffer));

            struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];

            memcpy(aSec->data, buffer, 512);
        }

        // encode track
        floppyTrackMfmEncode(trackNum, (byte *)getTrack(), getStream());

        // write track
        writeDiskTrack(trackNum, retryCount);

        // verify track
        if (verify)
        {
            // read track
            readDiskTrack(trackNum, trackNum, retryCount);

            bool compareError = false;
            byte buffer[512];

            // move file pointer pack 1 track
            if (source == _sdCard)
                ADFFile.seek(trackNum * 11 * 512);
            else if (source == _flashMemory)
                ADFFlashFile.seek(trackNum * 11 * 512);

            // compare sectors to file
            for (int i = 0; i < 11; i++)
            {
                if (source == _sdCard)
                    ADFFile.read(buffer, sizeof(buffer));
                else if (source == _flashMemory)
                    ADFFlashFile.read(buffer, sizeof(buffer));

                struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];

                if (memcmp(aSec->data, buffer, 512))
                    compareError = true;
            }

            if (compareError)
            {
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_MAGENTA);
                _audio->playBong(false);
            }
            else
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_GREEN);
        }
    }

    ADFFile.close();
    ADFFlashFile.close();
    _audio->playBoing(false);
}

void XCopyDisk::diskToDisk(bool verify, uint8_t retryCount)
{
    _graphics->clearScreen();
    diskToADF("DISKCOPY.TMP", verify, retryCount, _flashMemory);
    changeDisk();
    _graphics->clearScreen();
    adfToDisk("DISKCOPY.TMP", verify, retryCount, _flashMemory);
}

void XCopyDisk::diskFlux()
{
    _cancelOperation = false;

    if (!diskChange())
    {
        _graphics->drawText(0, 0, ST7735_WHITE, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 0, ST7735_RED, "Operation Cancelled", true);
            _audio->playBong(false);
            return;
        }

        // read track
        gotoLogicTrack(trackNum);
        int errors = readTrack(true);

        if (errors != -1)
        {
            analyseHist(true);

            uint8_t yoffset = 0;
            for (int i = 0; i < 255; i = i + 2)
            {
                int hist = getHist()[i] + getHist()[i + 1];
                if (hist > 0)
                {
                    hist = (hist / 64);
                    hist = hist < 255 ? hist : 255;
                    float ratio = hist / 255.0f;

                    if (hist < 5)
                        _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_WHITE, ST7735_YELLOW, ratio));
                    else if (hist < 50)
                        _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_YELLOW, ST7735_ORANGE, ratio));
                    else
                        _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_ORANGE, ST7735_RED, ratio));
                }
                yoffset++;
            }
        }
        else
        {
            _audio->playBong(false);
            _graphics->getTFT()->drawFastVLine(trackNum, 0, _graphics->getTFT()->height(), ST7735_MAGENTA);
        }
    }

    _audio->playBoing(false);
}

void XCopyDisk::testDisk(uint8_t retryCount)
{
    _cancelOperation = false;

    _graphics->drawDiskName("");
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_WHITE, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    String diskName = getName();
    _graphics->drawDiskName(diskName);

    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);
            _graphics->drawDisk(trackNum, ST7735_RED);
            _audio->playBong(false);
            return;
        }

        // read track
        readDiskTrack(trackNum, false, retryCount);
    }

    _audio->playBoing(false);
}

String XCopyDisk::getADFVolumeName(String ADFFileName, ADFFileSource source)
{

    String volumeName;
    volumeName = "NDOS";

    if (ADFFileName == "")
        return ">> File Error";

    char buffer[ADFFileName.length() + 1];
    memset(buffer, 0, sizeof(buffer));
    ADFFileName.toCharArray(buffer, sizeof(buffer));

    byte trackBuffer[512];

    if (source == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            return ">> SD Error";
        }

        File ADFFile = SD.open(buffer, FILE_READ);

        if (!ADFFile)
            return ">> File Error";

        ADFFile.seek(40 * 2 * 11 * 512);
        ADFFile.read(trackBuffer, sizeof(trackBuffer));
        ADFFile.close();
    }
    else if (source == _flashMemory)
    {
        if (!SerialFlash.begin(_flashCSPin))
        {
            return ">> Flash Error";
        }

        SerialFlashFile ADFFile = SerialFlash.open(buffer);

        if (!ADFFile)
            return ">> File Error";

        ADFFile.seek(40 * 2 * 11 * 512);
        ADFFile.read(trackBuffer, sizeof(trackBuffer));
        ADFFile.close();
    }

    int nameLen = trackBuffer[432];
    if (nameLen > 30)
        return "NDOS";
    int temp = 0;
    for (int i = 0x04; i < 0x0c; i++)
    {
        temp += trackBuffer[i];
    }
    for (int i = 0x10; i < 0x14; i++)
    {
        temp += trackBuffer[i];
    }
    for (int i = 463; i < 472; i++)
    {
        temp += trackBuffer[i];
    }
    for (int i = 496; i < 504; i++)
    {
        temp += trackBuffer[i];
    }
    if (temp != 0)
        return "NDOS";
    for (int i = 0; i < 4; i++)
    {
        temp += trackBuffer[i];
    }
    if (temp != 2)
        return "NDOS";
    temp = 0;
    for (int i = 508; i < 512; i++)
    {
        temp += trackBuffer[i];
    }
    if (temp != 1)
        return "NDOS";
    volumeName = "";
    for (int i = 0; i < nameLen; i++)
    {
        volumeName += (char)trackBuffer[433 + i];
    }

    return volumeName;
}

void XCopyDisk::flashDetails()
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
    Serial << "Part Nummber: ";

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

void XCopyDisk::cancelOperation()
{
    _cancelOperation = true;
}