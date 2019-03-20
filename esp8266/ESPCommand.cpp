#include "ESPCommand.h"

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

    if (cmd == "version" || cmd == "ver")
    {
        Serial << "Version: " << ESPVersion << "\r\n";
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
        Serial << "| connect <ssid> <pwd> | connect to wireless network                          |\r\n";
        Serial << "| status               | show status                                          |\r\n";
        Serial << "`----------------------'------------------------------------------------------'\r\n";
    }

    if (cmd == "clear" || cmd == "cls")
    {
        Serial << "\033[2J\033[H";
    }

    if (cmd == "ping")
    {
        Serial << "pong\r\n";
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
    }

    if (cmd == "connect")
    {
        String ssid = param.substring(0, param.indexOf(" "));
        String password = param.substring(param.indexOf(" ") + 1);

        if (ssid == "" || password == "" || param.indexOf(" ") == -1)
        {
            Serial << "Error: must supply ssid and password.\r\n";
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
        {
            Serial << "Connected to: " << ssid << "\r\n";
            Serial << "IP address: " << WiFi.localIP() << "\r\n";
        }
        else
            Serial << "Error connecting to: " << ssid << "\r\n";
    }
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
            Serial << "\033[1D \033[1D";
            return;
        }

        if (inChar == 0x0d || inChar == 0x0a)
        {
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
            Serial << inChar;
        }
    }
}
