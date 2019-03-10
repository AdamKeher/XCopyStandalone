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
    _graphics->drawText(0, 60, ST7735_YELLOW, "           Change Disk");

    bool diskInserted = diskChange();
    while (diskInserted)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 70, ST7735_RED, "            Cancelled");
            return;
        }
        diskInserted = diskChange();
        delay(500);
    }

    _graphics->drawText(0, 60, ST7735_GREEN, "    Disk Ejected. Waiting", true);

    diskInserted = diskChange();
    while (!diskInserted)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 70, ST7735_RED, "            Cancelled");
            return;
        }
        diskInserted = diskChange();
        delay(3000);
    }
}

void XCopyDisk::dateTime(uint16_t *date, uint16_t *time)
{
    // return date using FAT_DATE macro to format fields
    *date = FAT_DATE(year(), month(), day());

    // return time using FAT_TIME macro to format fields
    *time = FAT_TIME(hour(), minute(), second());
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

        if (_cancelOperation)
        {
            return;
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

        if (_cancelOperation)
        {
            return;
        }
    }

    if (errors == -1)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_RED);
    else if (errors == 0 && retries > 0)
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_ORANGE);
}

bool XCopyDisk::diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination)
{
    _cancelOperation = false;

    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName("");
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _audio->playBong(false);
        return false;
    }

    String diskName = getName();
    _graphics->drawDiskName(diskName);

    File ADFFile;
    SerialFlashFile ADFFlashFile;

    // Open SD File or SerialFile
    if (destination == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            _graphics->drawText(0, 10, ST7735_RED, "SD Init Failed");
            _audio->playBong(false);
            return false;
        }

        if (!SD.exists(SD_ADF_PATH))
        {
            SD.mkdir(SD_ADF_PATH);
        }

        char buffer[32];
        sprintf(buffer, "%04d%02d%02d %02d%02d", year(), month(), day(), hour(), minute());

        String fileName = String(buffer) + " " + diskName + ".adf";
        String fullPath = "/" + String(SD_ADF_PATH) + "/" + fileName;

        if (SD.exists(fullPath.c_str()))
            SD.remove(fullPath.c_str());

        SdFile::dateTimeCallback(dateTime);
        ADFFile = SD.open(fullPath.c_str(), FILE_WRITE);
        SdFile::dateTimeCallbackCancel();

        if (!ADFFile)
        {
            ADFFile.close();
            _graphics->drawText(0, 10, ST7735_RED, "SD File Open Failed");
            _audio->playBong(false);

            return false;
        }
    }
    else if (destination == _flashMemory)
    {
        if (!SerialFlash.begin(_flashCSPin))
        {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash Init Failed");
            _audio->playBong(false);
            return false;
        }

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());

        _graphics->drawText(0, 10, ST7735_ORANGE, "Erasing Flash...", true);

        ADFFlashFile.erase();

        while (!SerialFlash.ready())
        {
        }

        _graphics->drawText("Done");

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());
        if (!ADFFlashFile)
        {
            ADFFlashFile.close();
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash File Open Failed", true);
            _audio->playBong(false);
            return false;
        }

        ADFFlashFile.seek(0);
    }

    // YELLOW = Begin, GREEN = Read OK, ORANGE = Read after Retries, RED = Read Error, MAGENTA = Verify Error
    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            OperationCancelled(trackNum);
            return false;
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

    return true;
}

void XCopyDisk::adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source)
{
    _cancelOperation = false;

    if (ADFFileName == "")
        return;

    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName(ADFFileName.substring(ADFFileName.lastIndexOf("/") + 1));
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    if (getWriteProtect())
    {
        _graphics->drawText(0, 10, ST7735_RED, "Disk Write Protected");
        _audio->playBong(false);
        return;
    }

    File ADFFile;
    SerialFlashFile ADFFlashFile;

    if (source == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            _graphics->drawText(0, 10, ST7735_RED, "SD Init Failed");
            _audio->playBong(false);
            return;
        }

        ADFFile = SD.open(ADFFileName.c_str(), FILE_READ);
        if (!ADFFile)
        {
            _graphics->drawText(0, 10, ST7735_RED, "SD File Open Failed");
            _audio->playBong(false);
            ADFFile.close();
            return;
        }
    }
    else if (source == _flashMemory)
    {
        if (!SerialFlash.begin(_flashCSPin))
        {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash Init Failed");
            _audio->playBong(false);
            return;
        }

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());
        if (!ADFFlashFile)
        {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash File Open Failed");
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

    motorOn();
    seek0();
    delay(100);

    // write ADF file
    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            OperationCancelled(trackNum);
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
    _cancelOperation = false;
    _graphics->clearScreen();

    if (!diskChange())
    {
        _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
        _graphics->drawDiskName("");
        _graphics->drawDisk();
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    bool completed = diskToADF("DISKCOPY.TMP", verify, retryCount, _flashMemory);

    if (_cancelOperation || !completed)
        return;

    changeDisk();

    if (_cancelOperation)
        return;

    _graphics->clearScreen();
    adfToDisk("DISKCOPY.TMP", verify, retryCount, _flashMemory);
}

void XCopyDisk::diskFlux()
{
    _cancelOperation = false;

    if (!diskChange())
    {
        _graphics->drawText(0, 0, ST7735_RED, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            OperationCancelled();
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

    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName("");
    _graphics->drawDisk();

    if (!diskChange())
    {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    String diskName = getName();
    _graphics->drawDiskName(diskName);

    for (int trackNum = 0; trackNum < 160; trackNum++)
    {
        if (_cancelOperation)
        {
            OperationCancelled(trackNum);
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

    byte trackBuffer[512];

    if (source == _sdCard)
    {
        if (!SD.begin(_sdCSPin))
        {
            return ">> SD Error";
        }

        File ADFFile = SD.open(ADFFileName.c_str(), FILE_READ);

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

        SerialFlashFile ADFFile = SerialFlash.open(ADFFileName.c_str());

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

void XCopyDisk::cancelOperation()
{
    _cancelOperation = true;
}

void XCopyDisk::OperationCancelled(uint8_t trackNum)
{
    _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);
    if (trackNum >= 0)
        _graphics->drawDisk(trackNum, ST7735_RED);
    _audio->playBong(false);
}