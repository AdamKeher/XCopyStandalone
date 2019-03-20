#include "XCopyADFLib.h"
#include "../adflib/adflib.h"
#include "../adflib/adf_nativ.h"

void XCopyADFLib::begin(uint8_t sdCSPin)
{
    _sdCSPin = sdCSPin;
    adfEnvInitDefault();
}

Device *XCopyADFLib::mount(char *filename)
{
    _filename = String(filename);
    Serial << "XCopyADFLib::mount::1\r\n";

    _dev = adfMountDev(filename, true);

    Serial << "XCopyADFLib::mount::2\r\n";
    return _dev;
}

Volume *XCopyADFLib::openVolume(Device *device)
{
    _vol = adfMount(device, 0, true);
    return _vol;
}

void XCopyADFLib::unmount()
{
    // Serial << "XCopyADFLib::unmount";
    adfUnMount(_vol);
    adfUnMountDev(_dev);
}

void XCopyADFLib::printDevice(Device *device)
{
    Serial << "ADF:\r\n";
    Serial << "\t     Filename: " << _filename << "\r\n";
    Serial << "\t  Device Type: ";
    switch (device->devType)
    {
    case DEVTYPE_FLOPDD:
        Serial << "Double Density Floppy\r\n";
        break;
    case DEVTYPE_FLOPHD:
        Serial << "High Density Floppy\r\n";
        break;
    case DEVTYPE_HARDDISK:
        Serial << "Harddisk\r\n";
        break;
    case DEVTYPE_HARDFILE:
        Serial << "Hardfile\r\n";
        break;
    default:
        break;
    }
    Serial << "\t    Cylinders: " << device->cylinders << "\r\n";
    Serial << "\t        Heads: " << device->heads << "\r\n";
    Serial << "\t      Sectors: " << device->sectors << "\r\n";
    Serial << "\t         Size: " << device->size << "\r\n";
    Serial << "\t       Native: " << (device->isNativeDev ? "true" : "false") << "\r\n";
    Serial << "\t     ReadOnly: " << (device->readOnly ? "true" : "false") << "\r\n";
    Serial << "\t      Volumes: " << device->nVol << "\r\n";
}

void XCopyADFLib::printVolume(Volume *volume)
{
    Serial << "Volume:\r\n";
    Serial << "\t         Name: " << volume->volName << "\r\n";
    Serial << "\t      dosType: ";
    Serial.printf("%s", isFFS(volume->dosType) ? "FFS" : "OFS");
    if (isINTL(volume->dosType))
        Serial << " INTL";
    if (isDIRCACHE(volume->dosType))
        Serial << " DIRCACHE";
    Serial << "\r\n";
    Serial << "\t    blockSize: " << volume->blockSize << "\r\n";
    Serial << "\tdatablockSize: " << volume->datablockSize << "\r\n";
    Serial << "\t     bootCode: " << volume->bootCode << "\r\n";
    Serial << "\t   freeBlocks: " << adfCountFreeBlocks(volume) << "/" << (volume->lastBlock - volume->firstBlock + 1) << "\r\n";
    Serial << "\t       filled: " << 100.0 - (adfCountFreeBlocks(volume) * 100.0) / (volume->lastBlock - volume->firstBlock + 1) << "%\r\n";

    Serial << "Directory:\r\n";
    Serial << "    Size Date         Time     @Bloc   Name / Comment\r\n";
    Serial << "-------- ----------   -------  ------  ------------------------------------\r\n";

    struct List *list, *cell;
    cell = list = adfGetDirEnt(volume, volume->curDirPtr);
    while (cell)
    {
        printEntry(volume, (Entry *)cell->content, "", true, true);
        cell = cell->next;
    }
    adfFreeDirList(list);
}

void XCopyADFLib::adfDump()
{
    if (_dev)
    {
        printDevice(_dev);
        _vol = adfMount(_dev, 0, true);
        if (!_vol)
        {
            Serial << "Error: Unable to mount ADF volume\r\n";
            return;
        }
        printVolume(_vol);
     }
    else
        Serial << "Error: Unable to mount ADF device\r\n";
}

void XCopyADFLib::printEntry(struct Volume *vol, struct Entry *entry, char *path, bool sect, bool comment)
{
    /* do not print the links entries, ADFlib do not support them yet properly */
    if (entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT)
        return;

    if (entry->type == ST_DIR)
        Serial.printf("         ");
    else
        Serial.printf("%7d  ", entry->size);

    Serial.printf("%4d/%02d/%02d  %2d:%02d:%02d ", entry->year, entry->month, entry->days,
                  entry->hour, entry->mins, entry->secs);
    if (sect)
        Serial.printf(" %06d ", entry->sector);

    if (strlen(path) > 0)
        Serial.printf(" %s/", path);
    else
        Serial.printf(" ");
    if (entry->type == ST_DIR)
        Serial.printf("%s/", entry->name);
    else
        Serial.printf("%s", entry->name);
    if (comment && entry->comment != NULL && strlen(entry->comment) > 0)
        Serial.printf(", %s", entry->comment);
    Serial.printf("\r\n");
}
