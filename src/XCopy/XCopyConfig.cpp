#include "XCopyConfig.h"

XCopyConfig::XCopyConfig(bool readConfig)
{
    if (readConfig)
        this->readConfig();
}

void XCopyConfig::createConfig()
{
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    root["verify"] = "TRUE";
    root["retryCount"] = 5;

    _config = "";
    root.printTo(_config);

    parseConfig();
}

void XCopyConfig::setRetryCount(int value)
{
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(_config);
    root["retryCount"] = value;

    _config = "";
    root.printTo(_config);

    _retryCount = value;
}

void XCopyConfig::setVerify(bool value)
{
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(_config);
    root["verify"] = value ? "TRUE" : "FALSE";

    _config = "";
    root.printTo(_config);

    _verify = value;
}

void XCopyConfig::setVolume(float value)
{
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(_config);
    root["volume"] = value;

    _config = "";
    root.printTo(_config);

    _volume = value;
}


void XCopyConfig::parseConfig()
{
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(_config);
    _verify = root["verify"] == "TRUE" ? true : false;
    _retryCount = root["retryCount"];
    _volume = root["volume"];
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