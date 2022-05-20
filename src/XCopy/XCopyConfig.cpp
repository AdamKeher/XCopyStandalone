#include "XCopyConfig.h"

XCopyConfig::XCopyConfig(bool readConfig)
{
    if (readConfig)
        this->readConfig();
}

void XCopyConfig::createConfig()
{
    StaticJsonDocument<512> jsonDocument;
    JsonObject root = jsonDocument.as<JsonObject>();
    root["verify"] = "TRUE";
    root["retryCount"] = 5;
    root["ssid"] = "";
    root["password"] = "";

    _config = "";
    serializeJson(root, _config);

    parseConfig();
}

void XCopyConfig::setRetryCount(int value)
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["retryCount"] = value;

    _config = "";
    serializeJson(root, _config);
    _retryCount = value;
}

void XCopyConfig::setVerify(bool value)
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["verify"] = value ? "TRUE" : "FALSE";

    _config = "";
    serializeJson(root, _config);
    _verify = value;
}

void XCopyConfig::setVolume(float value)
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["volume"] = value;

    _config = "";
    serializeJson(root, _config);
    _volume = value;
}

void XCopyConfig::setSSID(String value)
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["ssid"] = value;

    _config = "";
    serializeJson(root, _config);
    _ssid = value;
}

void XCopyConfig::setPassword(String value)
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["password"] = value;

    _config = "";
    serializeJson(root, _config);
    _password = value;
}

void XCopyConfig::setDiskDelay(uint16_t delayMs) {
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["diskDelay"] = delayMs;

    _config = "";
    serializeJson(root, _config);
    _diskDelay = delayMs;
}

void XCopyConfig::setTimeZone(int timeZone) {
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["timeZone"] = timeZone;

    _config = "";
    serializeJson(root, _config);
    _timeZone = timeZone;
}


void XCopyConfig::parseConfig()
{
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());

    _verify = root["verify"] == "TRUE" ? true : false;
    _retryCount = root["retryCount"];
    _volume = root["volume"].as<float>();
    _ssid = root["ssid"].as<const char*>();
    _password = root["password"].as<const char*>();
    _diskDelay = root["diskDelay"];
    _timeZone = root["timeZone"];
}

bool XCopyConfig::readConfig()
{
    SerialFlashFile configfile = SerialFlash.open(configFilename);

    if (configfile)
    {
        _config = "";
        unsigned long n = configfile.size();
        char buffer[256];

        while (n > 0)
        {
            unsigned long rd = n;
            if (rd > sizeof(buffer) - 1)
                rd = sizeof(buffer) - 1;
            memset(buffer, 0, sizeof(buffer));
            configfile.read(buffer, rd);
            _config = _config + String(buffer);
            n = n - rd;
        }

        configfile.close();
        parseConfig();
        return true;
    }
    else
    {
        configfile.close();
        return false;
    }
}

bool XCopyConfig::writeConfig()
{
    SerialFlash.remove(configFilename);

    char buffer[_config.length()];
    memset(buffer, 0, sizeof(buffer));
    _config.toCharArray(buffer, sizeof(buffer) + 1);

    if (SerialFlash.create(configFilename, sizeof(buffer)))
    {
        SerialFlashFile configfile = SerialFlash.open(configFilename);

        if (configfile)
        {
            configfile.write(buffer, sizeof(buffer));
            configfile.close();
        }
        else
        {
            configfile.close();
            return false;
        }
    }
    else
        return false;

    parseConfig();
    return true;
}