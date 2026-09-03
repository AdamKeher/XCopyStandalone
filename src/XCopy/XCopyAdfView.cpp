#include "XCopyAdfView.h"
#include "XCopyLog.h"

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

bool XCopyAdfView::eachEntry(struct AdfVolume *vol,
                             ADF_SECTNUM dirSector,
                             EntryVisit visit,
                             void *context)
{
    if (vol == NULL || visit == NULL)
        return false;

    /*
       Two block buffers on the stack, 1KB together.

       They are here rather than static because a listing is transient and static
       would cost the same bytes permanently - and rather than on the heap because
       the heap is the scarce one. Console commands run from the main loop with a
       shallow stack under them, which is where the room for this comes from.
    */
    struct AdfEntryBlock parent;
    struct AdfEntryBlock entryBlk;

    if (adfReadEntryBlock(vol, dirSector, &parent) != ADF_RC_OK)
        return false;

    for (int i = 0; i < ADF_HT_SIZE; i++)
    {
        ADF_SECTNUM sector = parent.hashTable[i];

        while (sector != 0)
        {
            struct AdfEntry entry;
            if (adfEntryRead(vol, sector, &entry, &entryBlk) != ADF_RC_OK)
                return false;

            visit(context, &entry);

            // adfEntryRead() strdup()s the name and the comment into the entry.
            // One entry is live at a time, so this is ~90 bytes of heap held for
            // the length of one visit rather than 6KB held for the whole listing.
            adfFreeEntry(&entry);

            if (entryBlk.nextSameHash == 0)
                break;

            /*
               ADFlib upstream issue #99, carried over from adfGetRDirEntLimited().

               Some disks - it looks deliberate, as a way of stopping people looking
               at the contents - link an entry's nextSameHash back to itself or to
               another entry already in the parent's hash table. AmigaOS locks up on
               those and so would this loop. Both cases end the chain and say so,
               rather than abandoning the whole listing: everything already printed
               is real, and the rest of the hash table is usually fine.
            */
            if (sector == entryBlk.nextSameHash)
            {
                Log << F("adf: warning: entry at block ") << sector
                    << F(" links to itself; chain truncated\r\n");
                break;
            }

            bool loops = false;
            for (int j = 0; j < ADF_HT_SIZE; j++)
            {
                if (parent.hashTable[j] == entryBlk.nextSameHash)
                {
                    Log << F("adf: warning: entry at block ") << sector
                        << F(" links back into the hash table; chain truncated\r\n");
                    loops = true;
                    break;
                }
            }
            if (loops)
                break;

            sector = entryBlk.nextSameHash;
        }
    }

    return true;
}

namespace
{
    void countAndPrint(void *context, const struct AdfEntry *entry)
    {
        XCopyAdfView::printEntry(entry);
        (*(uint16_t *)context)++;
    }
}

uint16_t XCopyAdfView::printDirectory(struct AdfVolume *vol, ADF_SECTNUM dirSector)
{
    uint16_t listed = 0;
    eachEntry(vol, dirSector, countAndPrint, &listed);
    return listed;
}
