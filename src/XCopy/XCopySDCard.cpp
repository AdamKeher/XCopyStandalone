#include "XCopySDCard.h"

bool XCopySDCard::begin() {
    // Initialize at the highest speed supported by the board that is
    // not over 50 MHz. Try a lower speed if SPI errors occur.
    if (!_sd.begin(PIN_SDCS, SD_SCK_MHZ(50))) {
        _error = F("SDCard failed to initialise");
        return false;
    }
    return true;
}
bool XCopySDCard::cardDetect() {
    if (!digitalRead(PIN_CARDDETECT) == 0) {
        _error = F("No SDCard detected");
        return false;
    }
    return true;
}

bool XCopySDCard::open(String directory) {
    if (directory == "") { directory = "/"; }
    directory = directory.replace("'", "");
    directory = directory.replace("\"", "");

    _directory = directory;

    if (!_root.open(_directory.c_str())) {
        _error = F("SDCard failed to open directory");
        return false;
    }
    return true;
}

bool XCopySDCard::next() {
    if (_file.openNext(&_root, O_RDONLY) == false) {
        return false;
    }

    char sdate[11];
    char stime[9];
    dir_t dir;

    _file.dirEntry(&dir);

    // date & size
    uint16_t date = dir.lastWriteDate;
    uint16_t time = dir.lastWriteTime;
    sprintf(sdate, "%04d-%02d-%02d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date));
    sprintf(stime, "%02d:%02d:%02d", FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time));
    _xfile.date = String(sdate);
    _xfile.time = String(stime);

    // filesize
    _xfile.size = dir.fileSize;

    // filename
    char lfnBuffer[255];
    _file.getName(lfnBuffer, 255);
    _xfile.filename = String(lfnBuffer);

    // bools
    _xfile.isDirectory = _file.isDir();
    _xfile.isADF = _xfile.filename.toLowerCase().endsWith(".adf");

    _file.close();

    if (_root.getError()) {
        Serial << "openNext failed";
        return false;
    }

    return true;
}