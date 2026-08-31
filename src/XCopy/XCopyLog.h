#ifndef XCOPYLOG_H
#define XCOPYLOG_H

#include <Arduino.h>
#include "XCopyESP8266.h"

class XCopyLog
{
    public:
    XCopyLog() { _esp = nullptr; }
    XCopyLog(XCopyESP8266 *esp) { _esp = esp; }

    // const reference, not by value: this is called once per directory entry and
    // the by value parameter copied the caller's String on every call.
    XCopyLog& operator<<(const String &text) {
        Serial << text;
        // _esp is null until XCopy::begin() constructs it, and the default
        // constructor sets it to nullptr, so every use has to be guarded. The
        // copy below only happens when there is somewhere to send it.
        if (_esp != nullptr) {
            String payload = text;
            payload.replace("\r", "\033[^M");
            payload.replace("\n", "\033[^J");
            _esp->print("broadcast log," + payload + "\r\n");
            delay(6);
        }
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
        if (_esp != nullptr) _esp->log(strOutput);
        return 0;
    }

    private:
    XCopyESP8266 *_esp;
};

extern XCopyLog Log;

#endif // XCOPYLOG_H