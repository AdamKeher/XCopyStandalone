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

    Serial << "Directory: '" << displayName << "'\r\n";

    if (!root.open(directory.c_str())) {
        return false;
    }

    while (file.openNext(&root, O_RDONLY)) {
        file.printModifyDateTime(&Serial);
        Serial.write(' ');
        file.printFileSize(&Serial);
        Serial.write(' ');
        if (file.isDir()) {
            // color directory.
            if (color) Serial << XCopyConsole::high_yellow();
            file.printName(&Serial);
            Serial << "/";
            if (color) Serial << XCopyConsole::reset();
        } else {
            // color adf files
            char lfnBuffer[256];
            file.getName(lfnBuffer, 255);
            if (String(lfnBuffer).toLowerCase().endsWith(".adf")) {
                if (color) Serial << XCopyConsole::high_green();
                Serial << lfnBuffer;
                if (color) Serial << XCopyConsole::reset();
            } else {
                Serial << lfnBuffer;
            }
        }
        Serial.println();
        file.close();
    }
    
    if (root.getError()) {
        Serial.println("openNext failed");
        return false;
    }

    root.close();

    return true;
}