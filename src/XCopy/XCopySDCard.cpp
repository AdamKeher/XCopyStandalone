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

bool XCopySDCard::printDirectory(String directory, bool color) {    
    SdFat sd;
    SdFile root;
    SdFile file;

    if (directory == "") { directory = "/"; }
    String displayName = directory;
    displayName = displayName.replace("'", "");
    displayName = displayName.replace("\"", "");
    directory = displayName;
    directory = directory.replace("\"", "");

    if (!displayName.startsWith("/")) { 
        displayName = "/" + displayName;
    }

    Log << "Directory: '" << displayName << "'\r\n";

    if (!root.open(directory.c_str())) {
        return false;
    }

    while (file.openNext(&root, O_RDONLY)) {
        char line[512];

        // date & size
        dir_t dir;
        file.dirEntry(&dir);
        uint16_t date = dir.lastWriteDate;
        uint16_t time = dir.lastWriteTime;
        sprintf(line, "%04d-%02d-%02d %02d:%02d:%02d %11d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date), FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time), dir.fileSize);

        // filename
        char lfnBuffer[220];
        char filename[255];
        file.getName(lfnBuffer, 220);
        if (file.isDir()) {
            // color directory
            sprintf(lfnBuffer, "%s/", lfnBuffer);
            if (color) {
                sprintf(filename, "%s%s%s",  XCopyConsole::high_yellow().c_str(), lfnBuffer, XCopyConsole::reset().c_str());
            }
        } else {
            // color adf files
            if (color & String(lfnBuffer).toLowerCase().endsWith(".adf")) {
                sprintf(filename, "%s%s%s", XCopyConsole::high_green().c_str(), lfnBuffer, XCopyConsole::reset().c_str());
            } else {
                sprintf(filename, lfnBuffer);
            }
        }

        // final line
        sprintf(line, "%s %s\r\n", line, filename);
        Log << line;

        file.close();
    }
    
    if (root.getError()) {
        Log << "openNext failed";
        return false;
    }

    return true;
}

// GenericList<XCopyFile> *XCopySDCard::getFiles(String directory) {    
void XCopySDCard::getFiles(String directory) {    
    GenericList<XCopyFile> list;

    SdFat sd;
    SdFile root;
    SdFile file;

    if (directory == "") { directory = "/"; }
    directory = directory.replace("'", "");
    directory = directory.replace("\"", "");

    directory = "/";

    if (!root.open(directory.c_str())) {
        return;
    }

    char sdate[11];
    char stime[9];
    dir_t dir;
    int count = 0;

    while (file.openNext(&root, O_RDONLY)) {
        Serial << ++count << "\r\n";

        if (list.size() > 30) break;

        XCopyFile *xFile = new XCopyFile();

        file.dirEntry(&dir);

        // date & size
        uint16_t date = dir.lastWriteDate;
        uint16_t time = dir.lastWriteTime;
        sprintf(sdate, "%04d-%02d-%02d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date));
        sprintf(stime, "%02d:%02d:%02d", FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time));
        xFile->date = String(sdate);
        xFile->time = String(stime);

        // filesize
        xFile->size = dir.fileSize;

        // filename
        char lfnBuffer[255];
        file.getName(lfnBuffer, 255);
        xFile->filename = String(lfnBuffer);
        xFile->isDirectory = file.isDir() ? true : false;
        xFile->isADF = xFile->filename.toLowerCase().endsWith(".adf") ? true : false;

        list.add(xFile);

        file.close();
    }

    if (root.getError()) {
        Log << "openNext failed";
    }

    root.close();

    Serial << "List Size: " << list.size() << "\r\n";

    // Node<XCopyFile> *p = list->head;
    // while (p) {
    //     Serial << p->data->filename << "," << p->data->size << "," << p->data->isDirectory << "," << p->data->isADF << "\r\n";
    //     p = p->next;
    // }

    // delete list;

    // return list;
}
