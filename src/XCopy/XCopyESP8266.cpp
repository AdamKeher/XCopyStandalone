#include "XCopyESP8266.h"

XCopyESP8266::XCopyESP8266(uint32_t baudrate, int espResetPin, int espProgPin)
{
    // _serial = Serial1;
    Serial1.begin(baudrate);

    _espResetPin = espResetPin;
    _espProgPin = espProgPin;
    digitalWrite(_espResetPin, HIGH);
    digitalWrite(_espProgPin, HIGH);
}

void XCopyESP8266::reset()
{
    digitalWrite(_espResetPin, LOW);
    delay(20);
    digitalWrite(_espResetPin, HIGH);
}

void XCopyESP8266::progMode()
{
    digitalWrite(_espResetPin, LOW);
    digitalWrite(_espProgPin, LOW);
    delay(20);
    digitalWrite(_espResetPin, HIGH);
    delay(20);
    digitalWrite(_espProgPin, HIGH);
}

String XCopyESP8266::sendCommand(String command, bool strip, int timeout)
{
    Serial1.flush();
    Serial1.clear();
    Serial1.print(command + "\r\n");

    if (timeout == -1)
        return "";

    char OK_EOC[5] = "OK\r\n";
    char ER_EOC[5] = "ER\r\n";
    const int bufferSize = 512;
    char buffer[bufferSize];
    int i = 0;
    int len = strlen(OK_EOC);
    uint32_t start = millis();

    while (millis() - start < (uint32_t)timeout)
    {
        if (Serial1.available())
        {
            // An unresponsive ESP -- or one running at a different baud rate -- produces
            // continuous framing garbage that never contains an end-of-command marker.
            // Slide the window rather than running off the end of this stack buffer,
            // keeping enough tail that an EOC straddling the boundary still matches.
            if (i >= bufferSize - 1)
            {
                memmove(buffer, buffer + i - (len - 1), len - 1);
                i = len - 1;
            }

            buffer[i++] = Serial1.read();
            if (i >= len)
            {
                if (strncmp(buffer + i - len, OK_EOC, len) == 0 || strncmp(buffer + i - len, ER_EOC, len) == 0)
                {
                    break;
                }
            }
        }
    }
    buffer[i] = 0;
    String response = buffer;

    if (strip)
    {
        if (response.startsWith(command + "\r\n")) { response = response.substring(command.length() + 2); }
        response.replace("\r\nOK\r\n", "");
        response.replace("\r\nER\r\n", "");
    }

    return response;
}

void XCopyESP8266::sendWebSocket(String command)
{
    sendCommand("broadcast " + command + "\r\n", false, -1);
}

bool XCopyESP8266::connect(String ssid, String password, uint32_t timeout)
{
    String response = sendCommand("connect " + ssid + " " + password + "\r", false, timeout);
    if (response.endsWith(OK_EOC))
        return true;
    else
        return false;
}

bool XCopyESP8266::begin()
{
    String result = sendCommand("ping\r\n", false, 200);
    if (result.indexOf("pong") != -1)
        return true;
    else
        return false;
}

void XCopyESP8266::setEcho(bool status)
{
    sendCommand("echo " + String(status ? "on" : "off") + "\r\n");
}

String XCopyESP8266::Version()
{
    return sendCommand("version\r\n");
}

void XCopyESP8266::Update()
{
    while (Serial1.available())
    {
        char inChar = (char)Serial1.read();

        if (inChar == 0x0a)
        {
            if (_command.startsWith(_marker))
            {
                _command = _command.substring(_marker.length());
                _command.replace("\r", "");
                _callback(_caller, _command);
            }
            _command = "";
        }
        else
        {
            // Same guard as sendCommand(): line noise carries no line endings to flush the
            // accumulator, and an unbounded String will exhaust the heap.
            if (_command.length() < 512)
                _command += inChar;
            else
                _command = "";
        }
    }
}

void XCopyESP8266::setCallBack(void* caller, OnWebCommand function)
{
    _caller = caller;
    _callback = function;
}

time_t XCopyESP8266::getTime() {
    String result = sendCommand("gettime\r\n", true, 5000);
    result.replace("gettime\r\n", "");
    result.replace("\r\n", "");
    return strtol(result.c_str(), nullptr, 10);
}

bool XCopyESP8266::updateWebSdCardFiles(String directory) {
    sendWebSocket(F("clearSdFiles"));

    XCopySDCard *_sdcard = new XCopySDCard();


    if (!_sdcard->cardDetect()) {
        // send error via websocket
        delete _sdcard;
        return false;
    }

    if (!_sdcard->begin()) {
        // send error via websocket
        delete _sdcard;
        return false;
    }

    if (!_sdcard->open(directory)) {
        // send error via websocket
        delete _sdcard;
        return false;
    }

    while (_sdcard->next()) {
        // Bound once, and grown once: this was six getfile() calls (three String
        // deep copies each) plus a temporary per append.
        const XCopyFile &file = _sdcard->getfile();

        String newline;
        newline.reserve(file.date.length() + file.time.length() +
                        file.filename.length() + 24);
        newline += file.date;
        newline += "&";
        newline += file.time;
        newline += "&";
        newline += file.size;
        newline += "&";
        newline += file.filename;
        newline += "&";
        newline += file.isDirectory ? "1" : "0";
        newline += "&";
        newline += file.isADF ? "1" : "0";

        // send command
        sendWebSocket(F("addSdFile,") + newline + F("\r"));

        // slow down to allow transfer to web
        delay(6);
    }

    sendWebSocket(F("drawSdFiles"));

    delete _sdcard;

    return true;
}