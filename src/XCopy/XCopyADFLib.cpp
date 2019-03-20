#include "XCopyADFLib.h"
#include "../adflib/adflib.h"
#include "../adflib/adf_nativ.h"

void XCopyADFLib::adfInfo(char* filename)
{
    adfEnvInitDefault();
    struct Device *dev;
    dev = adfMountDev(filename, true);
    
    if (dev)
    {
        Serial << "ADF:\r\n";
        Serial << "\t   Filename: " << filename << "\r\n";
        Serial << "\tDevice Type: ";
        switch(dev->devType)
        {
            case DEVTYPE_FLOPDD:
                Serial << "DEVTYPE_FLOPDD\r\n";
                break;
            case DEVTYPE_FLOPHD:
                Serial << "DEVTYPE_FLOPHD\r\n";
                break;
            case DEVTYPE_HARDDISK:
                Serial << "DEVTYPE_HARDDISK\r\n";
                break;
            case DEVTYPE_HARDFILE:
                Serial << "DEVTYPE_HARDFILE\r\n";
                break;
            default:
                break;
        }
        Serial << "\t  Cylinders: " << dev->cylinders << "\r\n";
        Serial << "\t      Heads: " << dev->heads << "\r\n";
        Serial << "\t    Sectors: " << dev->sectors << "\r\n";
        Serial << "\t       Size: " << dev->size << "\r\n";
        Serial << "\t     Native: " << (dev->isNativeDev ? "true" : "false") << "\r\n";
        Serial << "\t   ReadOnly: " << (dev->readOnly ? "true" : "false") << "\r\n";
        Serial << "\t    Volumes: " << dev->nVol << "\r\n";

        struct Volume *vol = adfMount(dev, 0, true);

        Serial << "Volume:\r\n";
        Serial << "\t         Name: " << vol->volName << "\r\n";
        Serial << "\t      dosType: ";
        Serial.printf("%s", isFFS(vol->dosType) ? "FFS" : "OFS");
        if (isINTL(vol->dosType))
            Serial << " INTL";
        if (isDIRCACHE(vol->dosType))
            Serial << " DIRCACHE";
        Serial << "\r\n";
        Serial << "\t    blockSize: " << vol->blockSize << "\r\n";
        Serial << "\tdatablockSize: " << vol->datablockSize << "\r\n";
        Serial << "\t     bootCode: " << vol->bootCode << "\r\n";
        Serial << "\t   freeBlocks: " << adfCountFreeBlocks(vol) << "/" << (vol->lastBlock - vol->firstBlock +1) << "\r\n";
        Serial << "\t       filled: " << 100.0-(adfCountFreeBlocks(vol)*100.0)/(vol->lastBlock - vol->firstBlock +1) << "%\r\n";

        Serial << "Directory:\r\n";
        struct List *list, *cell;
        cell = list = adfGetDirEnt(vol,vol->curDirPtr);
        while(cell) {
            printEnt(vol, (Entry *)cell->content, "", true, true);
            cell = cell->next;
        }
        adfFreeDirList(list);        
    }
    else
        Serial << "Error: Unable to mount ADF file\r\n";    
}

void XCopyADFLib::printEnt(struct Volume *vol, struct Entry *entry, char *path, bool sect, bool comment)
{
    /* do not print the links entries, ADFlib do not support them yet properly */
    if (entry->type==ST_LFILE || entry->type==ST_LDIR || entry->type==ST_LSOFT)
        return;

    if (entry->type==ST_DIR)
        Serial.printf("         ");
    else
        Serial.printf("%7d  ",entry->size);

	Serial.printf("%4d/%02d/%02d  %2d:%02d:%02d ",entry->year, entry->month, entry->days,
        entry->hour, entry->mins, entry->secs);
    if (sect)
        Serial.printf(" %06d ",entry->sector);

    if (strlen(path)>0)
        Serial.printf(" %s/",path);
    else
        Serial.printf(" ");
    if (entry->type==ST_DIR)
        Serial.printf("%s/",entry->name);
    else
        Serial.printf("%s",entry->name);
    if (comment && entry->comment!=NULL && strlen(entry->comment)>0)
        Serial.printf(", %s",entry->comment);
        Serial.printf("\r\n");

}
