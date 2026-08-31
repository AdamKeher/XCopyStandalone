#include "XCopyConfig.h"

XCopyConfig::XCopyConfig(bool readConfig)
{
    if (readConfig)
        this->readConfig();
}

void XCopyConfig::createConfig()
{
    StaticJsonDocument<512> jsonDocument;
    // to<JsonObject>(), not as<JsonObject>(): as() on an empty document returns a
    // null object and every assignment below was silently discarded, leaving
    // _config as the string "null".
    JsonObject root = jsonDocument.to<JsonObject>();
    root["verify"] = "TRUE";
    root["retryCount"] = 5;
    root["ssid"] = "";
    root["password"] = "";
    // parseConfig() reads these too; without them a fresh config came up muted,
    // with no disk delay. Values match the shipped config.json.
    root["volume"] = 0.8;
    root["diskDelay"] = 200;
    root["timeZone"] = 0;
    root["scpRevolutions"] = 3;
    root["scpEndCylinder"] = 79;

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

void XCopyConfig::setScpRevolutions(uint8_t revolutions) {
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["scpRevolutions"] = revolutions;

    _config = "";
    serializeJson(root, _config);
    _scpRevolutions = revolutions;
}

void XCopyConfig::setScpEndCylinder(uint8_t cylinder) {
    StaticJsonDocument<512> root;
    deserializeJson(root, _config.c_str());
    root["scpEndCylinder"] = cylinder;

    _config = "";
    serializeJson(root, _config);
    _scpEndCylinder = cylinder;
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
    // Defaulted rather than read bare: a config written before SCP existed has
    // neither key, and root["missing"] is 0 - which would mean zero revolutions.
    _scpRevolutions = root["scpRevolutions"] | 3;
    _scpEndCylinder = root["scpEndCylinder"] | 79;
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

    // toCharArray() writes up to size bytes including the terminator, so the buffer
    // needs room for it. Passing sizeof(buffer) + 1 wrote one byte past the end.
    size_t length = _config.length();
    char buffer[length + 1];
    memset(buffer, 0, sizeof(buffer));
    _config.toCharArray(buffer, sizeof(buffer));

    // The terminator is not stored: readConfig() reads exactly size() bytes into a
    // zeroed buffer, and existing config files were written this way.
    if (SerialFlash.create(configFilename, length))
    {
        SerialFlashFile configfile = SerialFlash.open(configFilename);

        if (configfile)
        {
            configfile.write(buffer, length);
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