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