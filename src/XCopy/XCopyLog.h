#ifndef XCOPYLOG_H
#define XCOPYLOG_H

#include <Arduino.h>
#include "XCopyESP8266.h"

class XCopyLog
{
    public:
    XCopyLog() { _esp = nullptr; }
    XCopyLog(XCopyESP8266 *esp) { _esp = esp; }

    XCopyLog& operator<<(String text) {
        Serial << text;
        text = text.replace("\r", "\033[^M");
        text = text.replace("\n", "\033[^J");
        _esp->print("broadcast log," + text + "\r\n");
        delay(6);
        return *this;
    }

    void setESP(XCopyESP8266 *esp) { _esp = esp; }

    int printf(const char *format, ...) {
        va_list args;
        va_start(args, format);
        char output[255] = "";
        vsprintf(output, format, args);
        va_end(args);
        Serial << output;
        String strOutput = String(output);
        strOutput = strOutput.replace("\r", "\033[^M");
        strOutput = strOutput.replace("\n", "\033[^J");
        _esp->log(strOutput);
        return 0;
    }

    private:
    XCopyESP8266 *_esp;
};

extern XCopyLog Log;

#endif // XCOPYLOG_H