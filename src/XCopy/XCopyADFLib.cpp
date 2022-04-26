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
    // Serial << "XCopyADFLib::mount::1\r\n";

    _dev = adfMountDev(filename, true);

    // Serial << "XCopyADFLib::mount::2\r\n";
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

String XCopyADFLib::printDevice(Device *device)
{
    String result = "";
    result.append("ADF:\r\n");
    result.append("\t     Filename: " + _filename + "\r\n");
    result.append("\t  Device Type: ");
    switch (device->devType)
    {
    case DEVTYPE_FLOPDD:
        result.append("Double Density Floppy\r\n");
        break;
    case DEVTYPE_FLOPHD:
        result.append("High Density Floppy\r\n");
        break;
    case DEVTYPE_HARDDISK:
        result.append("Harddisk\r\n");
        break;
    case DEVTYPE_HARDFILE:
        result.append("Hardfile\r\n");
        break;
    default:
        break;
    }
    result.append("\t    Cylinders: " + String(device->cylinders) + "\r\n");
    result.append("\t        Heads: " + String(device->heads) + "\r\n");
    result.append("\t      Sectors: " + String(device->sectors) + "\r\n");
    result.append("\t         Size: " + String(device->size) + "\r\n");
    result.append("\t       Native: " + String(device->isNativeDev ? "true" : "false") + "\r\n");
    result.append("\t     ReadOnly: " + String(device->readOnly ? "true" : "false") + "\r\n");
    result.append("\t      Volumes: " + String(device->nVol) + "\r\n");

    return result;
}

String XCopyADFLib::printVolume(Volume *volume) {
    String result = "";    
    result.append("Volume:\r\n");
    result.append("\t         Name: " + String(volume->volName) + "\r\n");
    result.append("\t      dosType: ");
    result.append(isFFS(volume->dosType) ? "FFS" : "OFS");
    if (isINTL(volume->dosType))
        result.append(" INTL");
    if (isDIRCACHE(volume->dosType))
        result.append(" DIRCACHE");
    result.append("\r\n");
    result.append("\t    blockSize: " + String(volume->blockSize) + "\r\n");
    result.append("\tdatablockSize: " + String(volume->datablockSize) + "\r\n");
    result.append("\t     bootCode: " + String(volume->bootCode) + "\r\n");
    result.append("\t   freeBlocks: " + String(adfCountFreeBlocks(volume)) + "/" + String((volume->lastBlock - volume->firstBlock + 1)) + "\r\n");
    result.append("\t       filled: " + String(100.0 - (adfCountFreeBlocks(volume) * 100.0) / (volume->lastBlock - volume->firstBlock + 1)) + "%\r\n");
    result.append("Directory:\r\n");
    result.append("    Size Date         Time     @Bloc   Name / Comment\r\n");
    result.append("-------- ----------   -------  ------  ------------------------------------\r\n");

    return result;
}

String XCopyADFLib::printDirectory(Volume *volume) {
    String result = "";    
    struct List *list, *cell;
    cell = list = adfGetDirEnt(volume, volume->curDirPtr);
    while (cell) {
        result.append(printEntry(volume, (Entry *)cell->content, "", true, true));
        // Entry *entry = (Entry *)cell->content;
        // if (entry->type == ST_DIR) {
        //     int32_t curDirPtr = volume->curDirPtr;
        //     volume->curDirPtr = entry->sector;
        //     printDirectory(volume);
        //     volume->curDirPtr = curDirPtr;
        // }
        cell = cell->next;        
    }
    adfFreeDirList(list);

    return result;
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

String XCopyADFLib::printEntry(struct Volume *vol, struct Entry *entry, char *path, bool sect, bool comment) {
    String result = "";
    char cresult[255] = "";

    /* do not print the links entries, ADFlib do not support them yet properly */
    if (entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT) {
        return "";
    }

    sprintf(cresult, entry->type == ST_DIR ? "         " : "%7d  ", entry->size);
    result.append(cresult);

    sprintf(cresult, "%4d/%02d/%02d  %2d:%02d:%02d ", entry->year, entry->month, entry->days, entry->hour, entry->mins, entry->secs);
    result.append(cresult);

    if (sect) {
        sprintf(cresult, " %06d ", entry->sector);
        result.append(cresult);
    }

    sprintf(cresult, strlen(path) > 0 ? " %s/" : " ", path);
    result.append(cresult);

    sprintf(cresult, entry->type == ST_DIR ? "%s/" : "%s", entry->name);
    result.append(cresult);

    if (comment && entry->comment != NULL && strlen(entry->comment) > 0) {
        sprintf(cresult, ", %s", entry->comment);
        result.append(cresult);
    }

    result.append("\r\n");

    return result;
}
