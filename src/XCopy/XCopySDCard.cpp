#include "XCopySDCard.h"

bool XCopySDCard::begin() {
    if (!xcopySdBegin()) {
        _error = F("SDCard failed to initialise");
        return false;
    }
    return true;
}
bool XCopySDCard::cardDetect() {
    // The pin is pulled up and driven low by the card switch. This read as
    // "(!digitalRead(...)) == 0", which is the same test written backwards.
    if (digitalRead(PIN_CARDDETECT) != 0) {
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

    // filename. 255 is required: getName() refuses a buffer under 13 bytes and a
    // FAT long name can reach 255, so a smaller one would silently truncate.
    char lfnBuffer[255];
    if (!_file.getName(lfnBuffer, sizeof(lfnBuffer))) {
        // On failure the buffer may be partly written and unterminated.
        lfnBuffer[0] = 0;
    }
    _xfile.filename = String(lfnBuffer);

    // bools
    _xfile.isDirectory = _file.isDir();
    // A case insensitive endsWith(".adf") over the existing buffer. The old
    // filename.toLowerCase() allocated nothing but modified in place -- Teensy
    // returns String& -- so it rewrote the stored name and ls listed every file
    // in lower case.
    int nameLen = _xfile.filename.length();
    _xfile.isADF = nameLen >= 4 &&
                   strcasecmp(_xfile.filename.c_str() + nameLen - 4, ".adf") == 0;
    _xfile.isSCP = nameLen >= 4 &&
                   strcasecmp(_xfile.filename.c_str() + nameLen - 4, ".scp") == 0;

    _file.close();

    if (_root.getError()) {
        Serial << "openNext failed";
        return false;
    }

    return true;
}