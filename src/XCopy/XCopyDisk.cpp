#include "XCopyDisk.h"

// #define XCOPY_DEBUG 1

XCopyDisk::XCopyDisk()
{
}

void XCopyDisk::begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp)
{
    _graphics = graphics;
    _audio = audio;
    _esp = esp;

    setupDrive();
}

void XCopyDisk::changeDisk()
{
    _graphics->clearScreen();
    _graphics->drawText(0, 60, ST7735_YELLOW, "           Change Disk");
    _esp->setStatus("Change Disk");

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
    _esp->setStatus("Disk Ejected. Waiting ...");

    diskInserted = diskChange();
    while (!diskInserted)
    {
        if (_cancelOperation)
        {
            _graphics->drawText(0, 70, ST7735_RED, "            Cancelled");
            _esp->setStatus("Cancelled");
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

//

void XCopyDisk::drawFlux(uint8_t trackNum, uint8_t scale, uint8_t yoffset)
{
    // web interface
    String data = "";
    for (int i = 0; i < 255; i++) {
        data = data + String(getHist()[i]);
        data = data + "|";
    }
    _esp->print("broadcast flux," + String(trackNum) + "," + data + "\r\n");

    // tft screen
    int scaled = 0;
    for (int i = 0; i < 255; i = i + scale)
    {
        // scale hist value
        for (int s = 0; s < scale; s++) { scaled += getHist()[i + s]; }

        // draw hist
        if (scaled > 0)
        {
            scaled = (scaled / (32 * scale));
            scaled = scaled < 255 ? scaled : 255;
            float ratio = scaled / 255.0f;

            if (scaled < 5)
                _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_WHITE, ST7735_YELLOW, ratio));
            else if (scaled < 50)
                _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_YELLOW, ST7735_ORANGE, ratio));
            else
                _graphics->getTFT()->drawPixel(trackNum, yoffset, _graphics->LerpRGB(ST7735_ORANGE, ST7735_RED, ratio));
        }
        yoffset++;
    }
}

String XCopyDisk::getADFVolumeName(String ADFFileName, ADFFileSource source)
{
    _esp->setStatus("Geting Disk Name");

    String volumeName;
    volumeName = "NDOS";

    if (ADFFileName == "")
        return ">> File Error";

    byte trackBuffer[512];

    if (source == _sdCard)
    {
        XCopySDCard *_sdcard = new XCopySDCard();
    
        if (_sdcard->begin()) { return ">> SD Error"; }

        File ADFFile;
        ADFFile.open(ADFFileName.c_str(), FILE_READ);

        if (!ADFFile) { return ">> File Error"; }

        ADFFile.seek(40 * 2 * 11 * 512);
        ADFFile.read(trackBuffer, sizeof(trackBuffer));
        ADFFile.close();

        delete _sdcard;
    }
    else if (source == _flashMemory) {
        if (!SerialFlash.begin(PIN_FLASHCS)) { return ">> Flash Error"; }

        SerialFlashFile ADFFile = SerialFlash.open(ADFFileName.c_str());

        if (!ADFFile) { return ">> File Error"; }

        ADFFile.seek(40 * 2 * 11 * 512);
        ADFFile.read(trackBuffer, sizeof(trackBuffer));
        ADFFile.close();
    }

    int nameLen = trackBuffer[432];
    if (nameLen > 30) { return "NDOS"; }
    int temp = 0;
    for (int i = 0x04; i < 0x0c; i++) {
        temp += trackBuffer[i];
    }
    for (int i = 0x10; i < 0x14; i++) {
        temp += trackBuffer[i];
    }
    for (int i = 463; i < 472; i++) {
        temp += trackBuffer[i];
    }
    for (int i = 496; i < 504; i++) {
        temp += trackBuffer[i];
    }
    if (temp != 0) { return "NDOS"; }
    for (int i = 0; i < 4; i++) {
        temp += trackBuffer[i];
    }
    if (temp != 2) { return "NDOS"; }
    temp = 0;
    for (int i = 508; i < 512; i++) {
        temp += trackBuffer[i];
    }
    if (temp != 1) { return "NDOS"; }
    volumeName = "";
    for (int i = 0; i < nameLen; i++) {
        volumeName += (char)trackBuffer[433 + i];
    }

    return volumeName;
}

void XCopyDisk::cancelOperation() {
    _cancelOperation = true;
}

void XCopyDisk::OperationCancelled(uint8_t trackNum) {
    _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);
    if (trackNum >= 0)
    {
        _graphics->drawDisk(trackNum, ST7735_RED);
        _esp->sendWebSocket("resetTracks,red," + String(trackNum));
    }
    _audio->playBong(false);
}

//

int XCopyDisk::readDiskTrack(uint8_t trackNum, bool verify, uint8_t retryCount, bool silent) {
    // white = seek
    // green = OK - no retries
    // yellow = ~OK - quick retry required
    // orange = ~OK - retry required
    // red = ERROR - track did not read correctly
    // return 0 = OK, -1 = ERROR, >0 = OK w/ RETRY Count

    int retries = 0;
    int readResult = -1;

    while (readResult == -1 && retries < retryCount) {
        // read track
        if (!silent) _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_WHITE);
        _esp->setTrack(trackNum, "white", verify ? "V" : "");
        gotoLogicTrack(trackNum);
        readResult = readTrack(true);

        if (readResult != -1) {
            // read OK
            if (getWeakTrack() > 0) {
                if (!silent) _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_YELLOW);
                _esp->setTrack(trackNum, "yellow", verify ? "V" : "");
            }
            else {
                if (!silent) _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_GREEN);
                _esp->setTrack(trackNum, "green", verify ? "V" : "");
            }
        }
        else {
            // read error
            if (!silent) _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_RED);            
            _esp->setTrack(trackNum, "red", verify ? "V" : String(retries + 1));
            retries++;
            _audio->playBong(false);
            delay(1000);
        }

        if (_cancelOperation) { return -1; }
    }

    if (readResult == 0 && retries > 0) {
        if (!silent) _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, verify, ST7735_ORANGE);
        _esp->setTrack(trackNum, "orange", verify ? "V" : String(retries));
    }

    return readResult == -1 ? readResult : retries;
}

int XCopyDisk::writeDiskTrack(uint8_t trackNum, uint8_t retryCount)
{
    // white = seek
    // green = OK
    // orange = ~OK - retry required
    // red = ERROR - track did not write correctly
    // return: 0 = OK, -1 = ERROR

    int retries = 0;
    int writeResult = -1;

    while (writeResult == -1 && retries < retryCount) {
        // write track
        _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_WHITE);
        _esp->setTrack(trackNum, "white");
        gotoLogicTrack(trackNum);    
        writeResult = writeTrack(); // returns 0 = OK, -1 = ERROR

        if (writeResult == 0) {
            // write OK
            if (retries == 0) {
                // write OK w/ no retries
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_GREEN);
                _esp->setTrack(trackNum, "green");
            }
            else {
                // write OK w/ retries
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_ORANGE);
                _esp->setTrack(trackNum, "orange", String(retries + 1));
            }
        }
        else {
            // write error
            _graphics->drawTrack(trackNum / 2, trackNum % 2, true, true, retries + 1, false, ST7735_RED);
            _esp->setTrack(trackNum, "red", String(retries + 1));
            Serial << "Write failed! - Try: << " << retries + 1 << "\r\n";
            retries++;
            _audio->playBong(false);
            delay(1000);
        }        

        if (_cancelOperation) { return -1; }
    }

    return writeResult == -1 ? writeResult : retries;
}

//

String XCopyDisk::generateADFFileName(String diskname) {
    String path = String(SD_ADF_PATH);
    char dtBuffer[32];
    sprintf(dtBuffer, "%04d%02d%02d %02d%02d", year(), month(), day(), hour(), minute());
    String datetime = String(dtBuffer);
    return "/" + path + "/" + datetime + " " + diskname + ".adf";
}

// TODO: Create XCopyLogFile object? Move CRC code into function. 
bool XCopyDisk::diskToADF(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource destination, bool setEsp) {
    FastCRC32 CRC32;
    FastCRC16 CRC16;
    uint32_t disk_crc32 = 0;
    uint16_t track_crc16 = 0;
    _cancelOperation = false;
    String statusText = "";
    String diskName = "";

    int readErrors = 0;
    int weakTracks = 0;
    int totalReadErrors = 0;
    int totalWeakTracks = 0;
    
    File ADFFile;
    File ADFLogFile;
    SerialFlashFile ADFFlashFile;

    // setup TFT and WebUI
    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName("");
    _graphics->drawDisk();

    // dont update the mode and state for functions such as Disk to Disk
    if (setEsp) {
        _esp->setMode(destination == _sdCard ? "Copy Disk to ADF" : "Copy Disk to Flash");
        _esp->setState(destination == _sdCard ? copyDiskToADF : copyDiskToFlash);
    }
    _esp->resetDisk();
    _esp->setTab("diskcopy");

    // check if disk is present in floppy
    if (!diskChange()) {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _esp->setStatus("No disk inserted");
        _audio->playBong(false);
        return false;
    }

    // get and set diskname
    diskName = getName();
    _graphics->drawDiskName(diskName);
    _esp->setDiskName(diskName);

    // get filesnames
    String fullPath = generateADFFileName(diskName);
    String logfileName = fullPath.substring(0, fullPath.length() - 4).append(".log");

    // set status on WebUI
    statusText = String("Copying floppy disk to ").append(destination == _sdCard ? "'"+ fullPath +"' on SD card" : "flash memory");   
    _esp->setStatus(statusText);

    // Open SD File or SerialFile
    XCopySDCard *_sdcard = new XCopySDCard();
    if (destination == _sdCard) {
        // card detect
        if (!_sdcard->cardDetect()) {
            _graphics->drawText(0, 10, ST7735_RED, "No SDCard detected");
            _esp->setStatus("SD card not inserted");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        // init
        if (!_sdcard->begin()) {
            _graphics->drawText(0, 10, ST7735_RED, "SD Init Failed");
            _esp->setStatus("SD card initialisation failed");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        // make ADF path
        if (!_sdcard->fileExists(SD_ADF_PATH)) _sdcard->makeDirectory(SD_ADF_PATH);

        // remove sdcard adf & log files if they exist
        if (_sdcard->fileExists(fullPath)) _sdcard->deleteFile(fullPath.c_str());
        if (_sdcard->fileExists(logfileName)) _sdcard->deleteFile(logfileName.c_str());

        // open sdcard files
        SdFile::dateTimeCallback(dateTime);
        ADFFile = _sdcard->getSdFat().open(fullPath.c_str(), FILE_WRITE);
        ADFLogFile = _sdcard->getSdFat().open(logfileName.c_str(), FILE_WRITE);
        SdFile::dateTimeCallbackCancel();

        // if adf file failed top open
        if (!ADFFile) {
            ADFFile.close();
            ADFLogFile.close();
            _graphics->drawText(0, 10, ST7735_RED, "SD ADF File Open Failed");
            _esp->setStatus("File '" + ADFFileName + "' failed to open on the SD card");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        // if log file failed to open
        if (!ADFLogFile) {
            ADFFile.close();
            ADFLogFile.close();
            _graphics->drawText(0, 10, ST7735_RED, "SD Log File Open Failed");
            _esp->setStatus("File '" + logfileName + "' failed to open on the SD card");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        // create log file header
        ADFLogFile.println("{");
        ADFLogFile.println("\t\"volume\": \"" + diskName + "\",");
        char buffer[256];
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
        ADFLogFile.println("\t\"date\": \"" + String(buffer) + "\",");
        ADFLogFile.println("\t\"origin\": \"XCopy Standalone\",");
        ADFLogFile.println("\t\"timestamp\": " + String(now()) + ",");
        ADFLogFile.println("\t\"tracks\": [");
    }
    else if (destination == _flashMemory) {
        if (!SerialFlash.begin(PIN_FLASHCS)) {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash Init Failed");
            _esp->setStatus("Onboard serial Flash failed to initialise");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());

        _graphics->drawText(0, 10, ST7735_ORANGE, "Erasing Flash...", true);
        _esp->setStatus("Erasing Flash ...");

        ADFFlashFile.erase();

        while (!SerialFlash.ready()) { }

        _graphics->drawText("Done");
        _esp->setStatus("Erasing Flash ... Done");

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());
        if (!ADFFlashFile) {
            ADFFlashFile.close();
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash File Open Failed", true);
            _esp->setStatus("Serial flash file failed to open");
            _audio->playBong(false);
            delete _sdcard;
            return false;
        }

        ADFFlashFile.seek(0);
    }

    // clear XCopy logo for flux
    _graphics->getTFT()->fillRect(0, 85, _graphics->getTFT()->width(), _graphics->getTFT()->height()-85, ST7735_BLACK);
    _graphics->getTFT()->drawFastHLine(0, 85, _graphics->getTFT()->width(), ST7735_GREEN);

    // MD5 setup
    MD5_CTX ctx;
    MD5::MD5Init(&ctx);

    // YELLOW = Begin, GREEN = Read OK, ORANGE = Read after Retries, RED = Read Error, MAGENTA = Verify Error
    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled(trackNum);
            ADFLogFile.println("\r\nCancelled.");
            ADFLogFile.close();
            ADFFile.close();
            delete _sdcard;
            return false;
        }

        // read track
        int readResult = readDiskTrack(trackNum, false, retryCount);

        // draw flux
        analyseHist(true);
        drawFlux(trackNum, 6, 85);

        if (getWeakTrack()) {
            weakTracks += getWeakTrack();
            totalWeakTracks++;
        }

        if (readResult != 0) {
            // if there has been error on either side, set error for whole cylinder else increment retry count
            if (readErrors == -1 || readResult == -1)
                readErrors = -1;
            else
                readErrors += readResult;
            totalReadErrors++;
        }

        // calculate CRC
        if (destination == _sdCard) {
            uint8_t side = trackNum % 2;

            for (int sec = 0; sec < 11; sec++) {
                struct Sector *aSec = (Sector *)&getTrack()[sec].sector;

                // total disk CRC
                if (sec == 0 && trackNum == 0)
                    disk_crc32 = CRC32.crc32(aSec->data, sizeof(aSec->data));
                else
                    disk_crc32 = CRC32.crc32_upd(aSec->data, sizeof(aSec->data));

                // cyl CRC16
                if (sec == 0 && side == 0) {
                    // Serial << "trackNum: " << trackNum << " Side: " << side << " Sec: " << sec << " CCITT\r\n";
                    track_crc16 = CRC16.ccitt(aSec->data, sizeof(aSec->data));
                }
                else {
                    // Serial << "trackNum: " << trackNum << " Side: " << side << " Sec: " << sec << " CCITT_UPD\r\n";
                    track_crc16 = CRC16.ccitt_upd(aSec->data, sizeof(aSec->data));
                }
            }

            if (side == 1) {
                uint8_t cylinder = (trackNum - 1) / 2;
                ADFLogFile.print("\t\t{ \"track\": " + String(cylinder) + ", \"crc16\": \"0x");
                ADFLogFile.print(track_crc16, HEX);
                ADFLogFile.print("\", \"retries\": " + String(readErrors) + ", \"weak\": " + String(weakTracks) + " }");
                ADFLogFile.println(cylinder != 79 ? "," : "");

                weakTracks = 0;
                readErrors = 0;
            }
        }

        // write track (11 sectors per track)
        for (int i = 0; i < 11; i++) {
            const struct Sector *aSec = (Sector *)&getTrack()[i].sector;
            // calculate MD5
            MD5::MD5Update(&ctx, aSec->data, 512);
            if (destination == _sdCard) {
                ADFFile.write(aSec->data, 512);
            }
            else if (destination == _flashMemory) {
                ADFFlashFile.write(aSec->data, 512);
                ADFFlashFile.flush();
                while (!SerialFlash.ready()) { }
            }
        }

        // verify
        if (verify) {
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
            for (int i = 0; i < 11; i++) {
                const struct Sector *aSec = (Sector *)&getTrack()[i].sector;

                if (destination == _sdCard)
                    ADFFile.read(buffer, sizeof(buffer));
                else if (destination == _flashMemory)
                    ADFFlashFile.read(buffer, sizeof(buffer));

                if (memcmp(aSec->data, buffer, 512))
                    compareError = true;
            }

            if (compareError) {
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, true, ST7735_MAGENTA);
                _esp->setTrack(trackNum, "magenta,");
                _audio->playBong(false);
            }
            else {
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, true, ST7735_GREEN);
                _esp->setTrack(trackNum, "green");
            }
        }
    }

    String sMD5 = ctxToMD5(&ctx);

    if (destination == _sdCard) {
        ADFLogFile.println("\t],");
        ADFLogFile.println("\t\"readErrors\": " + String(totalReadErrors) + ",");
        ADFLogFile.println("\t\"weakTracks\": " + String(totalWeakTracks) + ",");
        ADFLogFile.print("\t\"crc32\": \"0x");
        ADFLogFile.print(disk_crc32, HEX);
        ADFLogFile.println("\",");
        ADFLogFile.println("\t\"md5\": \"" + sMD5 + "\"");
        ADFLogFile.println("}");
    }

    ADFFile.close();
    ADFLogFile.close();
    ADFFlashFile.close();
    _audio->playBoing(false);

    statusText = String("Completed copying floppy disk to ").append(destination == _sdCard ? "'<a href=\"/sdcard" + fullPath + "\">" + fullPath + "</a>' on SD card. <a target=\"_blank\" href=\"/sdcard" + logfileName  + "\">Log File</a> <i class=\"fa-solid fa-hashtag\"></i> MD5: " + sMD5 : "flash memory. <i class=\"fa-solid fa-hashtag\"></i> MD5: " + sMD5);
    _esp->setStatus(statusText);

    delete _sdcard;

    return true;
}

void XCopyDisk::adfToDisk(String ADFFileName, bool verify, uint8_t retryCount, ADFFileSource source, bool setEsp) {
    if (source == _flashMemory) {
        // flash memory
        if (setEsp) {
            _esp->setMode(ADFFileName == "BLANK.TMP" ? "Format Disk" : "Copy Flash to Disk");
            _esp->setState(copyFlashToDisk);
        }
        _esp->setStatus("Copying disk from flash memory to floppy disk");
    }
    else {
        // sdcard
        if (setEsp) {
            _esp->setMode("Copy ADF to Disk");
            _esp->setState(copyADFToDisk);
        }
        _esp->setStatus("Copying ADF '" + ADFFileName + "' from SD card to floppy disk");
    }
    _esp->setTab("diskcopy");
    
    _cancelOperation = false;

    if (ADFFileName == "") { return; }

    _graphics->clearScreen();
    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName(ADFFileName.substring(ADFFileName.lastIndexOf("/") + 1));
    _graphics->drawDisk();

    _esp->resetDisk();

    if (!diskChange()) {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _esp->setStatus("No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    if (getWriteProtect()) {
        _graphics->drawText(0, 10, ST7735_RED, "Disk Write Protected");
        _esp->setStatus("Disk Write Protected");
        _audio->playBong(false);
        return;
    }

    File ADFFile;
    SerialFlashFile ADFFlashFile;
    XCopySDCard *_sdcard = new XCopySDCard();

    if (source == _sdCard) {
        if (!_sdcard->begin()) {
            _graphics->drawText(0, 10, ST7735_RED, "SD Init Failed");
            _esp->setStatus("SD Init Failed");
            _audio->playBong(false);
            delete _sdcard;
            return;
        }

        ADFFile = _sdcard->getSdFat().open(ADFFileName.c_str(), FILE_READ);
        if (!ADFFile) {
            _graphics->drawText(0, 10, ST7735_RED, "SD File Open Failed");
            _esp->setStatus("SD File Open Failed");
            _audio->playBong(false);
            ADFFile.close();
            delete _sdcard;
            return;
        }
    }
    else if (source == _flashMemory) {
        if (!SerialFlash.begin(PIN_FLASHCS)) {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash Init Failed");
            _esp->setStatus("Serial Flash Init Failed");
            _audio->playBong(false);
            delete _sdcard;
            return;
        }

        ADFFlashFile = SerialFlash.open(ADFFileName.c_str());
        if (!ADFFlashFile) {
            _graphics->drawText(0, 10, ST7735_RED, "Serial Flash File Open Failed");
            _esp->setStatus("Serial Flash File Open Failed");
            _audio->playBong(false);
            ADFFlashFile.close();
            delete _sdcard;
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

    // MD5 setup
    MD5_CTX ctx;
    MD5::MD5Init(&ctx);

    // write ADF file
    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled(trackNum);
            delete _sdcard;
            return;
        }

        // read track from ADF file into track buffer
        for (int i = 0; i < 11; i++) {
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
        int result = writeDiskTrack(trackNum, retryCount);

        // verify track
        if (result != -1 && verify) {
            // read track
            readDiskTrack(trackNum, true, retryCount);

            bool compareError = false;
            byte buffer[512];

            // move file pointer pack 1 track
            if (source == _sdCard)
                ADFFile.seek(trackNum * 11 * 512);
            else if (source == _flashMemory)
                ADFFlashFile.seek(trackNum * 11 * 512);

            // compare sectors to file
            for (int i = 0; i < 11; i++) {
                if (source == _sdCard)
                    ADFFile.read(buffer, sizeof(buffer));
                else if (source == _flashMemory)
                    ADFFlashFile.read(buffer, sizeof(buffer));

                struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];

                if (memcmp(aSec->data, buffer, 512))
                    compareError = true;

                // calculate MD5
                MD5::MD5Update(&ctx, aSec->data, 512);
            }

            if (compareError) {
                _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_MAGENTA);
                _esp->setTrack(trackNum, "magenta");
                _audio->playBong(false);
            }
            else {
                if (result == 0) {
                    // write OK w/ no retries
                    _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_GREEN);
                    _esp->setTrack(trackNum, "green");
                }
                else {
                    // write OK w/ retries
                    _graphics->drawTrack(trackNum / 2, trackNum % 2, true, false, 0, false, ST7735_ORANGE);
                    _esp->setTrack(trackNum, "orange", String(result + 1));
                }
            }
        }
    }

    String status = "'" + ADFFileName + "' file written to disk";

    if (verify) {
        // output MD5
        String sMD5 = ctxToMD5(&ctx);
        Serial << "Verify data MD5: " << sMD5 << "\r\n";
        status.append(". Verify data <i class=\"fa-solid fa-hashtag\"></i> MD5: ").append(sMD5);
    }

    ADFFile.close();
    ADFFlashFile.close();
    _audio->playBoing(false);

    delete _sdcard;

    _esp->setStatus(status);
}

void XCopyDisk::diskToDisk(bool verify, uint8_t retryCount) {
    _esp->setTab("diskcopy");
    _esp->setMode("Copy Disk to Disk");
    _esp->setStatus("Copying Disk to Disk");
    _esp->setState(copyDiskToDisk);

    _cancelOperation = false;
    _graphics->clearScreen();

    if (!diskChange()) {
        _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
        _graphics->drawDiskName("");
        _graphics->drawDisk();
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _esp->setStatus("No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    bool completed = diskToADF("DISKCOPY.TMP", verify, retryCount, _flashMemory, false);

    if (_cancelOperation || !completed) { return; }

    changeDisk();

    if (_cancelOperation) { return; }

    adfToDisk("DISKCOPY.TMP", verify, retryCount, _flashMemory, false);
}

void XCopyDisk::diskFlux() {
    _esp->setTab("diskcopy");
    _esp->setMode("Disk Flux");
    _esp->setStatus("Drawing Disk Flux");
    _esp->setState(fluxDisk);
    _esp->resetDisk();

    _cancelOperation = false;

    if (!diskChange()) {
        _graphics->drawText(0, 0, ST7735_RED, "No Disk Inserted");
        _esp->setStatus("No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    String diskName = getName();
    _esp->setDiskName(diskName);

    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled();
            return;
        }

        // read track
        gotoLogicTrack(trackNum);
        int errors = readDiskTrack(trackNum, false, 1, true);
        // int errors = readTrack(true);

        if (errors != -1) {
            analyseHist(true);
            drawFlux(trackNum);
        }
        else {
            _audio->playBong(false);
            _graphics->getTFT()->drawFastVLine(trackNum, 0, _graphics->getTFT()->height(), ST7735_MAGENTA);
        }
    }

    _audio->playBoing(false);

    _esp->setStatus("Disk Flux Complete");
}

void XCopyDisk::testDiskette(uint8_t retryCount) {
    _esp->setMode("Test Disk");
    _esp->setStatus("Testing Disk");  
    _esp->setState(testDisk);
    _esp->resetDisk();
    _esp->setTab("diskcopy");

    _cancelOperation = false;

    _graphics->drawDiskName("");
    _graphics->drawDisk();

    if (!diskChange()) {
        _graphics->drawText(0, 10, ST7735_RED, "No Disk Inserted");
        _esp->setStatus("No Disk Inserted");

        _audio->playBong(false);
        return;
    }

    String diskName = getName();
    _graphics->drawDiskName(diskName);
    _graphics->getTFT()->drawFastHLine(0, 85, _graphics->getTFT()->width(), ST7735_GREEN);
    _esp->setDiskName(diskName);

    // MD5 setup
    MD5_CTX ctx;
    MD5::MD5Init(&ctx);

    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled(trackNum);
            return;
        }

        // read track
        readDiskTrack(trackNum, false, retryCount);

        // calculate MD5
        for (int sec = 0; sec < 11; sec++) {
            struct Sector *aSec = (Sector *)&getTrack()[sec].sector;
            // calculate MD5
            MD5::MD5Update(&ctx, aSec->data, 512);
        }

        // draw flux
        analyseHist(true);
        drawFlux(trackNum, 6, 85);
    }

    String sMD5 = ctxToMD5(&ctx);
    Serial << "\r\nFloppy disk test MD5:\r\n" << sMD5 << "\r\n";

    _audio->playBoing(false);

    _esp->setStatus("Test Complete. <i class=\"fa-solid fa-hashtag\"></i> MD5: " + sMD5);
}

//

String XCopyDisk::ctxToMD5(MD5_CTX *ctx) {
    String sMD5 = "";
    unsigned char result[20];
    MD5::MD5Final(result, ctx);
    char bMD5[3];
    for (size_t i = 0; i < 16; i++) {
        sprintf(bMD5, "%02X", result[i]);
        sMD5.append(String(bMD5));
    }
    return sMD5;
}

String XCopyDisk::adfToMD5(String ADFFileName) {
        FatFile file;
        bool fresult = file.open(ADFFileName.c_str());
        if (!fresult) {
            Serial << F("unable to open: '") + ADFFileName + F("'\r\n");
            return "";
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
        
        return ctxToMD5(&ctx);
}

String XCopyDisk::flashToMD5() {
    SerialFlashFile ADFFlashFile;

   if (!SerialFlash.begin(PIN_FLASHCS)) {
        _graphics->drawText(0, 10, ST7735_RED, "Serial Flash Init Failed");
        _esp->setStatus("Serial Flash Init Failed");
        _audio->playBong(false);
        return "";
    }

    ADFFlashFile = SerialFlash.open("DISKCOPY.TMP");
    if (!ADFFlashFile) {
        _graphics->drawText(0, 10, ST7735_RED, "Serial Flash File Open Failed");
        _esp->setStatus("Serial Flash File Open Failed");
        _audio->playBong(false);
        ADFFlashFile.close();
        return "";
    }

    ADFFlashFile.seek(0);

    // MD5 setup
    MD5_CTX ctx;
    MD5::MD5Init(&ctx);

    // write ADF file
    for (int trackNum = 0; trackNum < 160; trackNum++) {
        // read track from ADF file into track buffer
        for (int i = 0; i < 11; i++) {
            byte buffer[512];
            ADFFlashFile.read(buffer, sizeof(buffer));
            // calculate MD5
            MD5::MD5Update(&ctx, buffer, 512);

        }
    }

    return ctxToMD5(&ctx);
}