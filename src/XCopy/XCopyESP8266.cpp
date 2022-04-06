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
    Serial1.print(command);

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
        if (Serial1.available())
        {
            buffer[i++] = Serial1.read();
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
            _command += inChar;
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
    time_t time = 0;
    return strtol(result.c_str(), nullptr, 10);
}

bool XCopyESP8266::updateWebSdCardFiles(String directory) {
    XCopySDCard *_sdcard = new XCopySDCard();
    
    if (!_sdcard->cardDetect()) {
        // Log << F("No SDCard detected\r\n");
        delete _sdcard;
        return false;
    }

    if (!_sdcard->begin()) {
        // Log << F("SDCard failed to initialise\r\n");
        delete _sdcard;
        return false;
    }
    
    SdFat sd;
    SdFile root;
    SdFile file;

    if (directory == "") { directory = "/"; }
    directory = directory.replace("'", "");
    directory = directory.replace("\"", "");

    if (!root.open(directory.c_str())) {
        delete _sdcard;
        return false;
    }

    sendWebSocket(F("clearSdFiles"));

    char sdate[11];
    char stime[9];
    dir_t dir;

    while (file.openNext(&root, O_RDONLY)) {
        String newline;

        file.dirEntry(&dir);

        // date & size
        uint16_t date = dir.lastWriteDate;
        uint16_t time = dir.lastWriteTime;
        sprintf(sdate, "%04d-%02d-%02d", FAT_YEAR(date), FAT_MONTH(date), FAT_DAY(date));
        sprintf(stime, "%02d:%02d:%02d", FAT_HOUR(time), FAT_MINUTE(time), FAT_SECOND(time));
        newline.append(String(sdate));
        newline.append("&" + String(stime));

        // filesize
        newline.append("&" + String(dir.fileSize));

        // filename
        char lfnBuffer[255];
        file.getName(lfnBuffer, 255);
        newline.append("&" + String(lfnBuffer));
        newline.append("&" + String(file.isDir()));
        newline.append("&" + String(String(lfnBuffer).toLowerCase().endsWith(".adf")));

        // send command
        String command = "addSdFile,";
        command.append(newline).append("\r");
        sendWebSocket(command);

        file.close();
    }

    if (root.getError()) {
        Serial << "openNext failed";
    }

    sendWebSocket(F("drawSdFiles"));

    root.close();
    delete _sdcard;
}

// bool XCopyESP8266::updateWebSdCardFiles(String directory) {
//         XCopySDCard *_sdcard = new XCopySDCard();
        
//         if (!_sdcard->cardDetect()) {
//             // Log << F("No SDCard detected\r\n");
//             return false;
//         }

//         if (!_sdcard->begin()) {
//             // Log << F("SDCard failed to initialise\r\n");
//             return false;
//         }

//         GenericList<String> *list = _sdcard->getFiles(directory, 40);

//         sendWebSocket(F("clearSdFiles"));

//         Node<String> *p = list->head;
//         while (p) {
//             // use & as delimiter so there isnt a conflict with the web code also 
//             // using ',' as a command and param delimiter
//             String command = "addSdFile,";
//             String data = String(p->data->c_str());
//             Serial << data << "\r\n";
//             data = data.replace(",", "&");
//             command.append(data).append("\r");
//             sendWebSocket(command);
//             p = p->next;
//         }
//         delete p;

//         sendWebSocket(F("drawSdFiles"));

//         Serial << "END\r\n\r\n";

//         delete list;
//         delete _sdcard;

//         return;
// }