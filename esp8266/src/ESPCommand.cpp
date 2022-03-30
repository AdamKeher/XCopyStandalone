#include "ESPCommand.h"

void ESPCommandLine::begin(WebSocketsServer *webSocket)
{
    _webSocket = webSocket;
}

void ESPCommandLine::doCommand(String command)
{
    command.replace((char)10, (char)0);
    command.replace((char)13, (char)0);

    String cmd = command;
    cmd.toLowerCase();
    String param = "";
    if (command.indexOf(" ") > 0)
    {
        cmd.remove(command.indexOf(" "), command.length());
        param = command.substring(command.indexOf(" ") + 1);
    }

    if (cmd == "help" || cmd == "?")
    {
        Serial << ".-----------------------------------------------------------------------------.\r\n";
        Serial << "| X-Copy Standalone ESP8266 Module                                            |\r\n";
        Serial << "|-----------------------------------------------------------------------------|\r\n";
        Serial << "| Command              | Description                                          |\r\n";
        Serial << "|----------------------+------------------------------------------------------|\r\n";
        Serial << "| help | ?             | this help                                            |\r\n";
        Serial << "| version | ver        | XCopy ESP8266 version number                         |\r\n";
        Serial << "| clear | cls          | clear screen                                         |\r\n";
        Serial << "|----------------------+------------------------------------------------------|\r\n";
        Serial << "| dir | ls             | list all SPIFFS files                                |\r\n";
        Serial << "| broadcast <msg>      | broadcast websocket message                          |\r\n";
        Serial << "|----------------------+------------------------------------------------------|\r\n";
        Serial << "| scan                 | list wifi networks                                   |\r\n";
        Serial << "| gettime              | get time from NTP server                             |\r\n";
        Serial << "| connect <ssid> <pwd> | connect to wireless network                          |\r\n";
        Serial << "| status               | show status                                          |\r\n";
        Serial << "| ip                   | show ip address                                      |\r\n";
        Serial << "| ssid                 | show ssid                                            |\r\n";
        Serial << "| echo <on|off>        | show ssid                                            |\r\n";
        Serial << "`----------------------'------------------------------------------------------'\r\n";

        return;
    }

    if (cmd == "clear" || cmd == "cls")
    {
        Serial << "\033[2J\033[H";

        return;
    }

    if (cmd == "ping")
    {
        Serial << "pong\r\n"
               << OK_EOC;

        return;
    }

    if (cmd == "echo")
    {
        if (param == "off")
            _localecho = false;
        else if (param == "on")
            _localecho = true;

        Serial << "Local Echo: " << (_localecho ? "ON" : "OFF") << "\r\n"
               << OK_EOC;

        return;
    }

    if (cmd == "dir" || cmd == "ls")
    {
        String directory = "";
        Dir dir = SPIFFS.openDir("/");
        while (dir.next())
        {
            directory += dir.fileName();
            directory += " / ";
            directory += dir.fileSize();
            directory += "\r\n";
        }
        Serial.print(directory);

        return;
    }

    if (cmd == "ip")
    {
        Serial << WiFi.localIP() << "\r\n"
               << OK_EOC;

        return;
    }

    if (cmd == "ssid")
    {
        Serial << WiFi.SSID() << "\r\n"
               << OK_EOC;

        return;
    }

    if (cmd == "version" || cmd == "ver")
    {
        Serial << ESPVersion << "\r\n"
               << OK_EOC;

        return;
    }

    if (cmd == "broadcast")
    {
        if (param == "")
        {
            Serial << "Error: you must supply broadcast message";
            return;
        }
        
        _webSocket->broadcastTXT(param);

        return;
    }

    if (cmd == "status")
    {
        Serial << "WiFi Status: ";
        switch (WiFi.status())
        {
        case WL_NO_SHIELD:
            Serial << "No sheild\r\n";
            break;
        case WL_IDLE_STATUS:
            Serial << "Idle\r\n";
            break;
        case WL_NO_SSID_AVAIL:
            Serial << "No SSID available\r\n";
            break;
        case WL_SCAN_COMPLETED:
            Serial << "Scan completed\r\n";
            break;
        case WL_CONNECTED:
            Serial << "Connected\r\n";
            break;
        case WL_CONNECT_FAILED:
            Serial << "Connect failed\r\n";
            break;
        case WL_CONNECTION_LOST:
            Serial << "Connection lost\r\n";
            break;
        case WL_DISCONNECTED:
            Serial << "Disconnected\r\n";
            break;
        default:
            Serial << "Unknown\r\n";
        }

        Serial << "-----\r\n";
        WiFi.printDiag(Serial);
        Serial << OK_EOC;

        return;
    }

    if (cmd == "scan")
    {
        Serial << "Starting Wifi Scan\r\n";
        int scanResult = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
        if (scanResult == 0) {
            Serial << "No networks found\r\n";
            return;
        } else if (scanResult > 0) {
            Serial.printf(PSTR("%d networks found:\r\n"), scanResult);
        }

        for (size_t i = 0; i < scanResult; i++) {
            String ssid;
            int32_t rssi;
            uint8_t encryptionType;
            uint8_t* bssid;
            int32_t channel;
            bool hidden;

            WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, bssid, channel, hidden);

            Serial.printf(PSTR("  %02d: [CH %02d] [%02X:%02X:%02X:%02X:%02X:%02X] %ddBm %c %c %s\r\n"),
                    i,
                    channel,
                    bssid[0], bssid[1], bssid[2],
                    bssid[3], bssid[4], bssid[5],
                    rssi,
                    (encryptionType == ENC_TYPE_NONE) ? ' ' : '*',
                    hidden ? 'H' : 'V',
                    ssid.c_str());
        }

        Serial << "scan complete.\r\n";

        return;
    }

    if (cmd == "connect")
    {
        String ssid = param.substring(0, param.indexOf(" "));
        String password = param.substring(param.indexOf(" ") + 1);

        if (ssid == "" || password == "" || param.indexOf(" ") == -1)
        {
            Serial << "Error: you must supply ssid and password.\r\n"
                   << ER_EOC;
            return;
        }

        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        // Wait for connection
        uint32_t timeout = 20000;
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED)
        {
            if (millis() - start > timeout)
            {
                Serial << "timeout\r\n";
                break;
            }
            delay(500);
            // Serial << ".";
        }

        if (WiFi.status() == WL_CONNECTED)
            Serial << "Connected to: " << ssid << "\r\nIP address: " << WiFi.localIP() << "\r\n"
                   << OK_EOC;
        else
            Serial << "Error connecting to: " << ssid << "\r\n"
                   << ER_EOC;

        return;
    }

    if (cmd == "gettime") {
        const long utcOffsetInSeconds = 0;        
        WiFiUDP ntpUDP;
        NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
        timeClient.begin();
        timeClient.update();      
        Serial << timeClient.getEpochTime() << "\r\nOK\r\n";
        timeClient.end();
        return;
    }

    if (cmd != "")
        Serial << "Unknown command: '" << cmd << "'\r\n";
}

void ESPCommandLine::printPrompt()
{
    Serial << ">> ";
}

void ESPCommandLine::Update()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();

        if (inChar == 0x08) // backspace
        {
            if (_command.length() == 0)
                return;

            _command = _command.substring(0, _command.length() - 1);

            if (_localecho)
                Serial << "\033[1D \033[1D";

            return;
        }

        if (inChar == 0x0d || inChar == 0x0a) // CR or LF
        {
            if (_localecho)
                Serial << "\r\n";
            if (_command != String(0x0d))
            {
                doCommand(_command);
                printPrompt();
            }
            _command = "";
        }
        else
        {
            _command += inChar;
            if (_localecho)
                Serial << inChar;
        }
    }
}
