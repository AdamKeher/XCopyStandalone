#include "XCopyESP8266.h"

XCopyESP8266::XCopyESP8266(HardwareSerial serial, uint32_t baudrate)
{
    _serial = serial;
    _serial.begin(baudrate);
}

String XCopyESP8266::sendCommand(String command, uint32_t delayms)
{
    // TODO: add timeout and instant exit after serial received
    _serial.print(command);
    String response = "";
    // uint32_t start = millis();
    delay(delayms);
    while (_serial.available())
    {
        response.append((char)_serial.read());
    }

    return response;
}

bool XCopyESP8266::connect(String ssid, String password, uint32_t timeout)
{
    String response = sendCommand("connect " + ssid + " " + password + "\r\n", timeout);
    // Serial << "DEBUG::CONNECT::(" << ssid << "," << password << "::" << response << ")\r\n";
    if (response.toLowerCase().indexOf("connected") != -1)
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

String XCopyESP8266::Version()
{
    String response = sendCommand("version\r\n");
    if (response != "")
        return response;
    else
        return "Unknown";
}