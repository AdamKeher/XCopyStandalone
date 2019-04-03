#include "XCopyESP8266.h"

XCopyESP8266::XCopyESP8266(HardwareSerial serial, uint32_t baudrate, int espResetPin, int espProgPin)
{
    _serial = serial;
    _serial.begin(baudrate);

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
    _serial.flush();
    _serial.clear();
    _serial.print(command);

    if (timeout == -1)
        return "";

    char OK_EOC[5] = "OK\r\n";
    char ER_EOC[5] = "ER\r\n";
    char buffer[512];
    int i = 0;
    int len = strlen(OK_EOC);
    bool found = false;
    uint32_t start = millis();

    while (millis() < start + timeout)
    {
        if (_serial.available())
        {
            buffer[i++] = _serial.read();
            if (i >= len)
            {
                if (strncmp(buffer + i - len, OK_EOC, len) == 0 || strncmp(buffer + i - len, ER_EOC, len) == 0)
                {
                    found = true;
                    break;
                }
            }
        }
    }
    buffer[i] = 0;
    String response = buffer;

    if (strip)
    {
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
    String result = sendCommand("ping\r\n", 200);
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
    while (_serial.available())
    {
        char inChar = (char)_serial.read();

        if (inChar == 0x0a)
        {
            if (_command.startsWith(_marker))
            {
                _command = _command.substring(_marker.length());
                _command.replace("\r", "");
                this->_espCallBack(_command);
            }
            _command = "";
        }
        else
        {
            _command += inChar;
        }
    }
}

void XCopyESP8266::setCallBack(espCallbackFunction function)
{
    _espCallBack = function;
}