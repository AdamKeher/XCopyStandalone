#include "XCopyAdfView.h"
#include "XCopyLog.h"
#include "XCopyAdfWalk.h"

extern "C"
{
#include "../adflib/adf_blk.h"
#include "../adflib/adf_bitm.h"
#include "../adflib/adf_dev_type.h"
}

void XCopyAdfView::printDevice(const struct AdfDevice *dev)
{
    if (dev == NULL)
        return;

    Log << F("Device:\r\n");
    Log << F("\t         Name: ") << (dev->name ? dev->name : "?") << F("\r\n");
    Log << F("\t         Type: ") << adfDevTypeGetDescription(dev->type) << F("\r\n");

    Log << F("\t        Class: ");
    switch (dev->devClass)
    {
    case ADF_DEVCLASS_FLOP:     Log << F("Floppy\r\n");    break;
    case ADF_DEVCLASS_HARDDISK: Log << F("Hard disk\r\n"); break;
    case ADF_DEVCLASS_HARDFILE: Log << F("Hardfile\r\n");  break;
    default:                    Log << F("Unknown\r\n");   break;
    }

    Log.printf("\t     Geometry: %u cylinders, %u heads, %u sectors\r\n",
               dev->geometry.cylinders, dev->geometry.heads, dev->geometry.sectors);
    Log.printf("\t         Size: %lu blocks, %lu bytes\r\n",
               (unsigned long)dev->sizeBlocks,
               (unsigned long)dev->sizeBlocks * dev->geometry.blockSize);
    Log << F("\t       Driver: ") << (dev->drv ? dev->drv->name : "?");
    Log << (dev->drv && dev->drv->isNative() ? F(" (native)\r\n") : F("\r\n"));
    Log << F("\t     ReadOnly: ") << (dev->readOnly ? F("true\r\n") : F("false\r\n"));
    Log.printf("\t      Volumes: %d\r\n", dev->nVol);
}

void XCopyAdfView::printVolume(struct AdfVolume *vol)
{
    if (vol == NULL)
        return;

    Log << F("Volume:\r\n");
    Log << F("\t         Name: ") << (vol->volName ? vol->volName : "?") << F("\r\n");

    Log << F("\t   Filesystem: ") << adfVolGetFsStr(vol);
    if (adfVolHasINTL(vol))
        Log << F(" INTL");
    if (adfVolHasDIRCACHE(vol))
        Log << F(" DIRCACHE");
    Log << F("\r\n");

    Log.printf("\t    blockSize: %u\r\n", vol->blockSize);
    Log.printf("\tdatablockSize: %u\r\n", vol->datablockSize);
    Log << F("\t     bootCode: ") << (vol->bootCode ? F("true\r\n") : F("false\r\n"));

    /*
       Free space, computed once.

       adfCountFreeBlocks() walks the whole bitmap, and the version this replaces
       called it three times in as many lines - once for the count and twice more
       inside the percentage.
    */
    const uint32_t total = (uint32_t)(vol->lastBlock - vol->firstBlock + 1);
    const uint32_t free = adfCountFreeBlocks(vol);
    Log.printf("\t   freeBlocks: %lu/%lu\r\n",
               (unsigned long)free, (unsigned long)total);
    if (total > 0)
        Log.printf("\t       filled: %lu%%\r\n",
                   (unsigned long)(((total - free) * 100 + total / 2) / total));

    Log << F("\t     ReadOnly: ") << (vol->readOnly ? F("true\r\n") : F("false\r\n"));
}

void XCopyAdfView::printListHeader()
{
    Log << F("    Size Date         Time     @Bloc   Name / Comment\r\n");
    Log << F("-------- ----------   -------  ------  ------------------------------------\r\n");
}

void XCopyAdfView::printEntry(const struct AdfEntry *entry)
{
    if (entry == NULL)
        return;

    /*
       Links are skipped, as they were before. ADFlib reads the entry but does not
       resolve it, so the row would carry a name and a size belonging to somewhere
       else on the disk.
    */
    if (entry->type == ADF_ST_LFILE || entry->type == ADF_ST_LDIR ||
        entry->type == ADF_ST_LSOFT)
        return;

    const bool isDir = (entry->type == ADF_ST_DIR);

    /*
       On the stack, not static. This is a leaf call and 48 bytes of frame is
       nothing against 6,252 of stack, whereas a static would cost the same bytes
       out of the malloc arena for the whole run - and the arena is the scarce one.
    */
    char row[48];

    if (isDir)
        snprintf(row, sizeof(row), "%8s ", "");
    else
        snprintf(row, sizeof(row), "%8lu ", (unsigned long)entry->size);
    Log << row;

    snprintf(row, sizeof(row), "%4d/%02d/%02d  %2d:%02d:%02d  %06ld  ",
             entry->year, entry->month, entry->days,
             entry->hour, entry->mins, entry->secs,
             (long)entry->sector);
    Log << row;

    Log << (entry->name ? entry->name : "?");
    if (isDir)
        Log << F("/");

    if (entry->comment != NULL && entry->comment[0] != '\0')
        Log << F(", ") << entry->comment;

    Log << F("\r\n");
}

namespace
{
    struct Listing
    {
        uint16_t listed;
    };

    void onEntry(void *context, const struct AdfEntry *entry)
    {
        XCopyAdfView::printEntry(entry);
        ((Listing *)context)->listed++;
    }

    void onFault(void *context, ADF_SECTNUM sector, AdfWalkFault fault)
    {
        (void)context;
        Log << F("adf: warning: entry at block ") << sector
            << (fault == ADF_WALK_SELF_LINK
                    ? F(" links to itself; chain truncated\r\n")
                    : F(" links back into the hash table; chain truncated\r\n"));
        Log << F("adf: warning: repair this volume before writing to it\r\n");
    }
}

uint16_t XCopyAdfView::printDirectory(struct AdfVolume *vol, ADF_SECTNUM dirSector)
{
    /*
       The walk's two block buffers, 1KB, on the stack.

       On the stack rather than static because a listing is transient and a static
       would cost the same bytes out of the malloc arena permanently; and rather
       than borrowed from XCopyScratch because a listing has to work while a disk
       operation holds that buffer. A console command runs from the main loop with
       a shallow stack under it, which is where the room comes from.
    */
    struct AdfEntryBlock scratch[2];

    Listing listing = {0};
    adfWalkDir(vol, dirSector, scratch, onEntry, onFault, &listing);
    return listing.listed;
}
