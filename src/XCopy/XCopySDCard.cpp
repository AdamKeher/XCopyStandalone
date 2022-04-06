#include "XCopySDCard.h"

bool XCopySDCard::begin() {
    // Initialize at the highest speed supported by the board that is
    // not over 50 MHz. Try a lower speed if SPI errors occur.
    if (!_sd.begin(PIN_SDCS, SD_SCK_MHZ(50))) {
        return false;
    }
    return true;
}
bool XCopySDCard::cardDetect() {
    return digitalRead(PIN_CARDDETECT) == 0 ? true : false;
}

// TODO: These functions have a maxItems parameter as they is not currently enough memory
// change them to some sort of getNext() type arrangement
GenericList<XCopyFile> *XCopySDCard::getXFiles(String directory, int maxItems) {    
    GenericList<XCopyFile> *list = new GenericList<XCopyFile>;

    SdFat sd;
    SdFile root;
    SdFile file;

    if (directory == "") { directory = "/"; }
    directory = directory.replace("'", "");
    directory = directory.replace("\"", "");

    if (!root.open(directory.c_str())) {
        return list;
    }

    char sdate[11];
    char stime[9];
    dir_t dir;

    while (file.openNext(&root, O_RDONLY)) {
        if (list->size() > maxItems) break;

        XCopyFile *newfile = new XCopyFile();
        file.dirEntry(&dir);

        // date & size
        uint16_t date = dir.lastWriteDate;
        uint16_t time = dir.lastWriteTime;
        sprintf(sdate, "%04d-%02d-%02d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date));
        sprintf(stime, "%02d:%02d:%02d", FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time));
        newfile->date = String(sdate);
        newfile->time = String(stime);

        // filesize
        newfile->size = dir.fileSize;

        // filename
        char lfnBuffer[255];
        file.getName(lfnBuffer, 255);
        newfile->filename = String(lfnBuffer);

        // bools
        newfile->isDirectory = file.isDir();
        newfile->isADF = newfile->filename.toLowerCase().endsWith(".adf");

        list->add(newfile);
        file.close();
    }

    if (root.getError()) {
        Serial << "openNext failed";
    }

    root.close();

    return list;
}

GenericList<String> *XCopySDCard::getFiles(String directory, int maxItems) {    
    GenericList<String> *list = new GenericList<String>;

    SdFat sd;
    SdFile root;
    SdFile file;

    if (directory == "") { directory = "/"; }
    directory = directory.replace("'", "");
    directory = directory.replace("\"", "");

    directory = "/";

    if (!root.open(directory.c_str())) {
        return list;
    }

    char sdate[11];
    char stime[9];
    dir_t dir;

    while (file.openNext(&root, O_RDONLY)) {
        String *newline = new String("");

        if (list->size() > maxItems) break;

        file.dirEntry(&dir);

        // date & size
        uint16_t date = dir.lastWriteDate;
        uint16_t time = dir.lastWriteTime;
        sprintf(sdate, "%04d-%02d-%02d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date));
        sprintf(stime, "%02d:%02d:%02d", FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time));
        newline->append(String(sdate));
        newline->append("," + String(stime));

        // filesize
        newline->append("," + String(dir.fileSize));

        // filename
        char lfnBuffer[255];
        file.getName(lfnBuffer, 255);
        newline->append("," + String(lfnBuffer));
        newline->append("," + String(file.isDir()));
        newline->append("," + String(String(lfnBuffer).toLowerCase().endsWith(".adf")));
        list->add(newline);

        file.close();
    }

    if (root.getError()) {
        Serial << "openNext failed";
    }

    root.close();

    return list;
}