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

void XCopyDisk::drawFlux(uint8_t trackNum, uint8_t scale, uint8_t yoffset, bool updateWebUI)
{
    // web interface
    String data = "";
    for (int i = 0; i < 255; i++) {
        data = data + String(getHist()[i]);
        data = data + "|";
    }
    if (updateWebUI) _esp->print("broadcast flux," + String(trackNum) + "," + data + "\r\n");

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

void XCopyDisk::scanEmptyBlocks(uint8_t retryCount) {
    _esp->setMode("Scan Empty Blocks");
    _esp->setStatus("Scanning Disk");  
    _esp->setState(scanBlocks);
    _esp->resetDisk();
    _esp->setTab("diskview");

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
    _esp->print("broadcast resetEmptyBlocks\r\n");

    Log << "\r\nScan blocks:\r\n";

    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled(trackNum);
            return;
        }

        // read track
        readDiskTrack(trackNum, false, retryCount);
        
        char track[3] = "";
        sprintf(track, "%02d", trackNum / 2);
        if (trackNum % 2 == 0) Log << "Track " + String(track) + " |";

        Log << " Side: " << trackNum % 2 << " | ";

        for (int sec = 0; sec < 11; sec++) {
            struct Sector *aSec = (Sector *)&getTrack()[sec].sector;

            // MD5 setup
            MD5_CTX ctx;
            MD5::MD5Init(&ctx);
            MD5::MD5Update(&ctx, aSec->data, 512);
            String sMD5 = ctxToMD5(&ctx);
            
            bool empty = sMD5 == "BF619EAC0CDF3F68D496EA9344137E8B";
            Log << (empty ? XCopyConsole::background_green() + " " + String(sec) + " " + XCopyConsole::reset() : XCopyConsole::background_red() + " " + String(sec) + " " + XCopyConsole::reset());
            _esp->print("broadcast setEmptyBlock," + String(trackNum / 2) + "," + String(trackNum % 2) + "," + String(sec) + "," + (empty ? "true" : "false") + "\r\n");
            delay(5);
        }

        if (trackNum % 2) Log << "\r\n";

        // draw flux
        analyseHist(true);
        drawFlux(trackNum, 6, 85, false);
    }

    _audio->playBoing(false);
}

bool XCopyDisk::writeBlocksToFile(byte blocks[], uint8_t retryCount) {
    _cancelOperation = false;
    String statusText = "";
    String diskName = "";

    int totalReadErrors = 0;
    
    File ADFFile;
    File ADFLogFile;
    SerialFlashFile ADFFlashFile;

    // check if disk is present in floppy
    if (!diskChange()) {
        Serial << "No disk inserted" << "\r\n";
        _audio->playBong(false);
        return false;
    }

    // get and set diskname
    diskName = getName();
    Serial << "Diskname: " << diskName << "\r\n";

    // get filesnames
    String fullPath = generateADFFileName(diskName).replace(".adf", ".bin");
    Serial << "Filename: " << fullPath << "\r\n";

    // Open SD File
    XCopySDCard *_sdcard = new XCopySDCard();

    // card detect
    if (!_sdcard->cardDetect()) {
        Serial << "No SDCard detected" << "\r\n";
        _audio->playBong(false);
        delete _sdcard;
        return false;
    }

    // init
    if (!_sdcard->begin()) {
        Serial << "SD Init Failed" << "\r\n";
        _audio->playBong(false);
        delete _sdcard;
        return false;
    }

    // make ADF path
    if (!_sdcard->fileExists(SD_ADF_PATH)) _sdcard->makeDirectory(SD_ADF_PATH);

    // remove sdcard adf & log files if they exist
    if (_sdcard->fileExists(fullPath)) _sdcard->deleteFile(fullPath.c_str());

    // open sdcard files
    SdFile::dateTimeCallback(dateTime);
    ADFFile = _sdcard->getSdFat().open(fullPath.c_str(), FILE_WRITE);
    SdFile::dateTimeCallbackCancel();

    // if adf file failed top open
    if (!ADFFile) {
        ADFFile.close();
        Serial << "File '" + fullPath + "' failed to open on the SD card" << "\r\n";
        _audio->playBong(false);
        delete _sdcard;
        return false;
    }

    DiskLocation dl;
    int currentTrack = -1;
    for(size_t index = 0; index < 220; index++) {
        for (size_t bit = 0; bit < 8; bit++) {
            int mask = 1 << bit;

            // if write block
            if ((blocks[index] & mask) > 0) {
                int block = (index * 8) + bit;
                dl.setBlock(block);

                if (dl.logicalTrack != currentTrack) {
                    int readResult = readDiskTrack(dl.logicalTrack, false, retryCount, true);
                    if (readResult != 0) {
                        // if there has been error on either side, set error for whole cylinder else increment retry count
                        Serial << "Read Errors: " << ++totalReadErrors << "\r\n";
                    }
                }

                const struct Sector *aSec = (Sector *)&getTrack()[dl.sector].sector;
                ADFFile.write(aSec->data, 512);
                currentTrack = dl.logicalTrack;

                // Serial << block << " | logtrack: " << dl.logicalTrack << " | track: " << dl.track << " | side: " << dl.side << " | sector: " << dl.sector << "\r\n";
            }
        }
    }

    ADFFile.close();
    _audio->playBoing(false);

    delete _sdcard;

    return true;
}

// TODO: write verify routine
bool XCopyDisk::writeFileToBlocks(String BinFileName, int startBlock, uint8_t retryCount) {
    if (!diskChange()) {
        Log << "No Disk Inserted\r\n";
        _audio->playBong(false);
        return false;
    }

    if (getWriteProtect()) {
        _graphics->drawText(0, 10, ST7735_RED, "Disk Write Protected");
        Log << "Disk Write Protected";
        _audio->playBong(false);
        return false;
    }

    if (startBlock < 0 || startBlock > 1759) {
        Log << "Invalid block number: " + String(startBlock) + "\r\n";
        _audio->playBong(false);
        return false;
    }

    XCopySDCard *_sdcard = new XCopySDCard();

    if (!_sdcard->begin()) {
        Log << "SD Init Failed\r\n";
        _audio->playBong(false);
        delete _sdcard;
        return false;
    }

    if (!_sdcard->fileExists(BinFileName)) {
        Log << "File: '" + BinFileName + "' does not exist.\r\n";
        _audio->playBong(false);
        delete _sdcard;
        return false;
    }

    File BinFile;
    BinFile = _sdcard->getSdFat().open(BinFileName.c_str(), FILE_READ);

    if (!BinFile) {
        Log << "SD File Open Failed\r\n";
        _audio->playBong(false);
        BinFile.close();
        delete _sdcard;
        return false;
    }

    _esp->setMode("Write File to Disk");
    _esp->setStatus("Writing File");  
    _esp->resetDisk();
    _esp->setTab("diskcopy");
    _graphics->clearScreen();
    _graphics->drawDisk();
    _graphics->drawText(0, 0, ST7735_WHITE, "Write File", true);
    _graphics->getTFT()->drawFastHLine(0, 85, _graphics->getTFT()->width(), ST7735_GREEN);

    setAutoDensity(false);
    setMode(DD); // DD
    delay(5);
    setCurrentTrack(-1);

    motorOn();
    seek0();
    delay(100);

    DiskLocation startdl;
    startdl.setBlock(startBlock);   
    int blockCount = ceil(BinFile.size() / 512.0f);
    DiskLocation enddl;
    enddl.setBlock(startBlock + blockCount);

    Log << "File Size: " + String(BinFile.size()) + "\r\nWriting " + String(blockCount) + " blocks\r\n";
    Log << "----------------------------------------------------------\r\n";
    Log << "Start | Block: " + String(startdl.block) + " Track: " + String(startdl.track) + " Side: " + String(startdl.side) + " Sector: " + String(startdl.sector) + "\r\n";
    Log << "  End | Block: " + String(enddl.block) + " Track: " + String(enddl.track) + " Side: " + String(enddl.side) + " Sector: " + String(enddl.sector) + "\r\n";
    Log << "----------------------------------------------------------\r\n";

    for (int logicalTrack = startdl.logicalTrack; logicalTrack <= enddl.logicalTrack; logicalTrack++) {
        int startSector = logicalTrack == startdl.logicalTrack ? startdl.sector : 0;
        int endSector = logicalTrack == enddl.logicalTrack ? enddl.sector : 11;

        // if writing a partial track, read existing track into track buffer
        if (startSector != 0 || endSector || 11) {
            int readResult = readDiskTrack(logicalTrack, false, retryCount);

            // read error
            if (readResult != 0) {
                Log << "Read error reading existing track for partial write. Aborted.\r\n";
                return false;
            }
        }

        // console output
        DiskLocation tempdl;
        tempdl.setBlock(logicalTrack, startSector);
        Log << "Write | Track: " + String(tempdl.track) + " Side: " + String(tempdl.side) + " Sectors: ";

        // replace blocks in track buffer with blocks from binary file
        for (int i = startSector; i < endSector; i++) {
            Log << String(i) + (i != endSector - 1 ? "," : "" );
            byte buffer[512];
            memset(buffer, 0, 512);
            int read = BinFile.read(buffer, sizeof(buffer));
            if (read == 0) {
                Log << "Error: 0 Bytes read\r\n";
            }
            struct Sector *aSec = (Sector *)&getTrack()[i].sector[0];
            memcpy(aSec->data, buffer, 512);
        }
        Log << "\r\n";

        // encode track
        floppyTrackMfmEncode(logicalTrack, (byte *)getTrack(), getStream());

        // write track
        int result = writeDiskTrack(logicalTrack, retryCount);
        // read error
        if (result == -1) {
            Log << "Error writing track. Aborted.\r\n";
            return false;
        }
        if (result > 0) {
            Log << "Writing track required " + String(result) + " retries.\r\n";
            return false;
        }

        delay(100);
    }


    BinFile.close();
    _audio->playBoing(false);

    delete _sdcard;

    return true;
}

int XCopyDisk::searchMemory(String searchText, byte* memory, size_t memorySize) { 
    size_t sSize = searchText.length();
    size_t sIndex = 0;
    searchText = searchText.toLowerCase();

    for(size_t mIndex = 0; mIndex < memorySize; mIndex++) {
        byte m1 = memory[mIndex];
        byte m2 = (m1 >= 65 && m1 <= 90) ? m1 + 32 : m1;

        if (m1 == searchText[sIndex] || m2 == searchText[sIndex]) {
            if (++sIndex == sSize) return (mIndex - sSize) + 1;
        } 
        else { sIndex = 0; }
    }

    return -1;
}

SearchResult XCopyDisk::processModule(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount) {
    int mod_start = 1080 - offset;                                  // bytes before 0th bytes of track containing M.K.
    int mod_startblock = dl.block - ceil(mod_start / 512.0f);       // block that MOD starts on
    offset = 512 - (mod_start % 512);                           // byte offset of block that MOD starts on

    SearchResult sr;
    sr.block = mod_startblock;
    sr.offset = offset;

    dl.setBlock(mod_startblock);
    Log << "Mod Location: | Block: " + String(dl.block) + " Logical Track: " + String(dl.logicalTrack) +  " Track: " + String(dl.track) + " Side: " + String(dl.side) + " Sector: " + String(dl.sector) + " Offset: 0x";
    Serial.print(offset, HEX);
    Log << "\r\n";

    size_t header_size = 1080 + 4;
    size_t size = 0;
    byte modheader[header_size];
    memset(&modheader, 0, header_size);

    obj->readDiskTrack(dl.logicalTrack, false, retryCount);
    struct Sector *aSec = (Sector *)&getTrack()[dl.sector].sector;    
    memcpy(&modheader[0], &aSec->data[offset], 512 - offset);
    size += 512 - offset;

    int currentTrack = dl.logicalTrack;
    dl.setBlock(dl.block + 1);
    if (dl.logicalTrack != currentTrack) {
        obj->readDiskTrack(dl.logicalTrack, false, retryCount);
    }
    aSec = (Sector *)&getTrack()[dl.sector].sector;
    memcpy(&modheader[size], &aSec->data[0], 512);
    size += 512;
    currentTrack = dl.logicalTrack;

    if (size < 1084) {
        currentTrack = dl.logicalTrack;
        dl.setBlock(dl.block + 1);
        if (dl.logicalTrack != currentTrack) {
            obj->readDiskTrack(dl.logicalTrack, false, retryCount);
        } 
        aSec = (Sector *)&getTrack()[dl.sector].sector;
        memcpy(&modheader[size], &aSec->data[0], 1084 - size);
    }

    char* modname = &modheader[0];
    char f_modname[21];
    sprintf(f_modname, "%-20s", modname);

    Log << "\r\n";
    Log << ".-----------------------------------------------------------------------------.\r\n";
    Log << "| Mod Name: " + String(f_modname) + "                                              |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n";   
    Log << "| Header Dump                                                                 |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n";
    for (int i=0; i<1084; i++) {
        if (i % 64 == 0) Serial << "| ";
        Serial << byte2char(modheader[i]);
        if ((i + 1) % 64 == 0) Serial << "            |\r\n";
    }
    Log << "                |\r\n";

    Log << "+-----------------------------------------------------------------------------+\r\n";
    Log << "| Samples:                                                                    |\r\n";
    offset = 20;
    int modFileSize = 1084;
    String sample_line;

    for (int i=0; i<31; i++) {
        char* samplename = &modheader[offset];
    
        uint16_t sample_size = 0;
        byte *bss = (byte*)&sample_size;
        bss[0] = modheader[offset + 23];
        bss[1] = modheader[offset + 22];

        char f_samplename[23];
        sprintf(f_samplename, "%-22s", samplename);
        char f_samplenumber[3];
        sprintf(f_samplenumber, "%02d", i + 1);
        char f_temp[7];
        char f_samplesize[7];
        sprintf(f_temp, "%d", sample_size * 2);
        sprintf(f_samplesize, "%6s", f_temp);

        if (i % 2 == 0)
            Log << "| #" + String(f_samplenumber) + " '" + String(f_samplename) + "' " + String(f_samplesize);
        else
            Log << "     #" + String(f_samplenumber) + " '" + String(f_samplename) + "' " + String(f_samplesize) + " |\r\n";

        modFileSize += sample_size * 2;
        offset += 30;
    }
    Log << "     #-- '                      '      - |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n";

    char f_songlength[4];
    int songlength = modheader[offset];
    sprintf(f_songlength, "%-3d", songlength);
    Log << "| Song Length: " + String(f_songlength) + "                                                            |\r\n";
    
    Log << "| Song Patterns:                                                              |\r\n";   
    offset += 2;

    int highestPattern = 0;
    for (int i=0; i<128; i++) {
        if (i % 6 == 0) Log << "|  ";
        char pattern[10];
        if (modheader[offset] > highestPattern) highestPattern = modheader[offset];
        if (i + 1 <= songlength)
            sprintf(pattern, "%3d: %03d", i + 1, modheader[offset]);
        else
            sprintf(pattern, "%3d: ---", i + 1);
        offset++;
        Log << String(pattern);
        if ((i + 1) % 6 == 0) 
            Log << "  |\r\n";
        else
            Log << "  .  ";
    }
    Log << "                                                 |\r\n";
    modFileSize += ((highestPattern + 1) * 1024);

    Log << "+-----------------------------------------------------------------------------+\r\n";
    Log << "| Signature: '";
    Log << byte2char(modheader[offset++]);
    Log << byte2char(modheader[offset++]);
    Log << byte2char(modheader[offset++]); 
    Log << byte2char(modheader[offset++]);
    Log << "'                                                           |\r\n";

    char temp[8];
    char f_filesize[8];
    sprintf(temp, "%d", modFileSize);
    sprintf(f_filesize, "%6s", temp);
    Log << "| File Size: " + String(f_filesize) + "                                                           |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n\r\n";

    sr.size = modFileSize;
    return sr;
}

SearchResult XCopyDisk::processAscii(XCopyDisk* obj, String text, DiskLocation dl, int offset, uint8_t retryCount) {
    Log << "Found: '" + text + "' | Block: " + String(dl.block) + " Logical Track: " + String(dl.logicalTrack) +  " Track: " + String(dl.track) + " Side: " + String(dl.side) + " Sector: " + String(dl.sector) + " Offset: 0x";
    Serial.print(offset, HEX);
    Log << "\r\n";
    printAmigaSector(dl.sector);
    SearchResult sr;
    sr.block = dl.block;
    sr.offset = offset;
    return sr;
}

void XCopyDisk::moduleInfo(int logicalTrack, int sec, int offset, uint8_t retryCount) {
    DiskLocation dl;
    dl.setBlock(logicalTrack, sec);
    size_t header_size = 1080 + 4;
    size_t size = 0;
    byte modheader[header_size];
    memset(&modheader, 0, header_size);

    readDiskTrack(dl.logicalTrack, false, retryCount);
    struct Sector *aSec = (Sector *)&getTrack()[dl.sector].sector;    
    memcpy(&modheader[0], &aSec->data[offset], 512 - offset);
    size += 512 - offset;

    dl.setBlock(dl.block + 1);
    if (dl.logicalTrack != logicalTrack) readDiskTrack(dl.logicalTrack, false, retryCount);
    aSec = (Sector *)&getTrack()[dl.sector].sector;
    memcpy(&modheader[size], &aSec->data[0], 512);
    size += 512;

    if (size < 1084) {
        dl.setBlock(dl.block + 1);
        if (dl.logicalTrack != logicalTrack) readDiskTrack(dl.logicalTrack, false, retryCount);
        aSec = (Sector *)&getTrack()[dl.sector].sector;
        memcpy(&modheader[size], &aSec->data[0], 1084 - size);
    }

    Serial << "Header Dump:\r\n";

    for (int i=0; i<1084; i++) {
        Serial << byte2char(modheader[i]);
        if (i % 64 == 0 && i > 0) Serial << "\r\n";
    }

    char* modname = &modheader[0];
    Serial << "\r\nMod Name: '" << modname << "'\r\n";

    Serial << "Sample Names:\r\n";

    offset = 20;
    for (int i=0; i<31; i++) {
        char* samplename = &modheader[offset];
        uint16_t sample_size = 0;
        byte *bss = (byte*)&sample_size;
        bss[0] = modheader[offset + 23];
        bss[1] = modheader[offset + 22];
        Serial << "Sample #" << i + 1 << ": '" << samplename << "' Size: " << sample_size * 2 << "\r\n";
        offset += 30;
    }
}

bool XCopyDisk::search(XCopyDisk* obj, String text, uint8_t retryCount, SearchProcessor processor) {
    _cancelOperation = false;

    // check if disk is present in floppy
    if (!diskChange()) {
        Serial << "No disk inserted" << "\r\n";
        _audio->playBong(false);
        return false;
    }

    _esp->resetDisk();
    _esp->setTab("diskview");
    _esp->clearHighlightedBlocks();
    _graphics->drawDisk();
    _graphics->drawText(0, 0, ST7735_WHITE, "ASCII Search", true);
    _graphics->getTFT()->drawFastHLine(0, 85, _graphics->getTFT()->width(), ST7735_GREEN);

    Serial << "\r\nSearch blocks:\r\nSearching ...\r\n";

    for (int trackNum = 0; trackNum < 160; trackNum++) {
        if (_cancelOperation) {
            OperationCancelled(trackNum);
            Log << "Canceled.\r\n";
            return false;
        }

        // read track
        readDiskTrack(trackNum, false, retryCount);
        
        char track[3] = "";
        sprintf(track, "%02d", trackNum / 2);

        for (int sec = 0; sec < 11; sec++) {
            struct Sector *aSec = (Sector *)&getTrack()[sec].sector;

            int offset = searchMemory(text, aSec->data, 512);
            if (offset != -1) {
                DiskLocation dl;
                dl.setBlock(trackNum, sec);
                SearchResult sr = processor(this, text, dl, offset, retryCount);                
                dl.setBlock(sr.block);
                _esp->highlightBlock(dl.track, dl.side, dl.sector, sr.size == 0 ? 1 : ceil(sr.size / 512.0f), true);
                // Serial << "Block: " << sr.block << " Size: " << sr.size << " Offset: " << sr.offset << "\r\n";
                Serial << "Searching ...\r\n";
            };
        }

        // draw flux
        analyseHist(true);
        drawFlux(trackNum, 6, 85, false);
    }

    Serial << "Done.\r\n";
    _audio->playBoing(false);
    return true;
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