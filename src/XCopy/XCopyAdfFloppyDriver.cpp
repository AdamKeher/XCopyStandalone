#include "XCopyAdfFloppyDriver.h"
#include "XCopyDisk.h"
#include "XCopyFloppy.h"

#include <string.h>
#include <stdlib.h>

extern "C"
{
#include "../adflib/adf_dev.h"
#include "../adflib/adf_dev_type.h"
#include "../adflib/adf_env.h"
#include "../adflib/adf_err.h"
#include "../adflib/adf_limits.h"
}

namespace
{
    /*
       One drive, so one set of state.

       Per device data would suggest two of these could exist, and two devices
       reading one head would interleave seeks and hand each other the other's
       track. openDev() refuses a second open instead.
    */
    XCopyDisk *_disk = nullptr;
    XCopyFloppy *_floppy = nullptr;
    unsigned char _retries = 5;
    bool _open = false;

    /*
       The track the decoded sector buffer currently holds, or -1.

       Without this a directory listing is eleven head passes for what is one
       track: ADFlib reads a block, then the next block, and the next, and each is
       a seek and a revolution. The buffer being cached is XCopyFloppy's _track[],
       which readDiskTrack() fills - so the cache is really just a note of what is
       already in there, and anything else that reads a track has to invalidate it.
    */
    int16_t _cachedTrack = -1;

    /* A DD Amiga floppy, which is what an ADF is. */
    const uint32_t kCylinders = 80;
    const uint32_t kHeads = 2;
    const uint32_t kSectors = 11;
    const uint32_t kBlocks = kCylinders * kHeads * kSectors;

    //! Bring @p logicalTrack into the decoded track buffer. False if it will not read.
    bool loadTrack(uint8_t logicalTrack)
    {
        if (_cachedTrack == (int16_t)logicalTrack)
            return true;

        // A failed read leaves _track[] holding whatever the last good pass put
        // there, so the cache is cleared before the attempt, not after it.
        _cachedTrack = -1;

        // silent: no TFT, no track map. A file system read is not a disk copy and
        // painting the copy progress display through one would be a lie.
        if (_disk->readDiskTrack(logicalTrack, false, _retries, true) < 0)
        {
            adfEnv.eFct("DF0: track %u would not read", (unsigned)logicalTrack);
            return false;
        }

        _cachedTrack = (int16_t)logicalTrack;
        return true;
    }
}

extern "C"
{

    static struct AdfDevice *floppyOpen(const char *const name,
                                        const AdfAccessMode mode)
    {
        if (_disk == nullptr || _floppy == nullptr)
        {
            adfEnv.eFct("%s: no drive attached", __func__);
            return NULL;
        }

        if (_open)
        {
            adfEnv.eFct("%s: the drive is already open", __func__);
            return NULL;
        }

        if (_floppy->diskChange() != 1)
        {
            adfEnv.eFct("%s: no disk in the drive", __func__);
            return NULL;
        }

        struct AdfDevice *const dev =
            (struct AdfDevice *)malloc(sizeof(struct AdfDevice));
        if (dev == NULL)
        {
            adfEnv.eFct("%s: out of memory", __func__);
            return NULL;
        }

        /*
           Read only whatever was asked for, until there is a write path.

           Reporting read-write and then failing every write would let ADFlib mount
           the volume read-write, load the bitmap for allocation and get several
           blocks into a file before finding out. Saying so at the door is the
           difference between a refusal and a half written disk.
        */
        (void)mode;
        dev->readOnly = true;

        // Double density, which every Amiga ADF is. readTrack() reads whatever
        // setMode() last selected, and a drive left in HD from an SCP capture
        // would decode 22 sectors out of an 11 sector disk and find none of them.
        _floppy->setAutoDensity(false);
        _floppy->setMode(DD);

        dev->geometry.cylinders = kCylinders;
        dev->geometry.heads = kHeads;
        dev->geometry.sectors = kSectors;
        dev->geometry.blockSize = ADF_DEV_BLOCK_SIZE;
        dev->sizeBlocks = kBlocks;
        dev->type = ADF_DEVTYPE_FDD;
        dev->devClass = ADF_DEVCLASS_FLOP;

        dev->name = strdup(name);
        dev->drvData = NULL;
        dev->nVol = 0;
        dev->volList = NULL;
        dev->mounted = false;
        dev->drv = &xcopyAdfFloppyDriver;
        dev->rdb.status = ADF_DEV_RDB_STATUS_UNKNOWN;
        dev->rdb.block = NULL;

        _cachedTrack = -1;
        _open = true;

        return dev;
    }

    static ADF_RETCODE floppyClose(struct AdfDevice *const dev)
    {
        if (dev->mounted)
            adfDevUnMount(dev);

        free(dev->name);
        free(dev);

        _cachedTrack = -1;
        _open = false;

        return ADF_RC_OK;
    }

    static ADF_RETCODE floppyRead(const struct AdfDevice *const dev,
                                  const uint32_t block,
                                  const uint32_t lenBlocks,
                                  uint8_t *const buf)
    {
        if (block + lenBlocks > dev->sizeBlocks)
        {
            adfEnv.eFct("%s: block %lu is past the end of the disk", __func__,
                        (unsigned long)(block + lenBlocks - 1));
            return ADF_RC_ERROR;
        }

        for (uint32_t i = 0; i < lenBlocks; i++)
        {
            const uint32_t n = block + i;

            // The same arithmetic DiskLocation::setBlock() does, and the reason
            // this driver is short: the ADF block order and the order the tracks
            // come off the disk are the same order.
            const uint8_t logicalTrack = (uint8_t)(n / kSectors);
            const uint8_t sector = (uint8_t)(n % kSectors);

            if (!loadTrack(logicalTrack))
                return ADF_RC_ERROR;

            const struct Sector *decoded =
                (const struct Sector *)&_floppy->getTrack()[sector].sector;

            memcpy(buf + (i * ADF_DEV_BLOCK_SIZE), decoded->data,
                   ADF_DEV_BLOCK_SIZE);
        }

        return ADF_RC_OK;
    }

    static ADF_RETCODE floppyWrite(const struct AdfDevice *const dev,
                                   const uint32_t block,
                                   const uint32_t lenBlocks,
                                   const uint8_t *const buf)
    {
        (void)dev;
        (void)block;
        (void)lenBlocks;
        (void)buf;

        // Not "not implemented": writing one sector means reading the whole track,
        // replacing 512 bytes and writing all eleven back, and getting that wrong
        // costs the operator their disk rather than their patience.
        adfEnv.eFct("DF0: is read only");
        return ADF_RC_ERROR;
    }

    /*
       True, and it matters.

       adfDevOpen() takes the geometry a native driver reports rather than
       calculating one from the device's length - which is right here, because
       80/2/11 is a fact about the drive and not a guess about a file.
    */
    static bool floppyIsNative(void)
    {
        return true;
    }

    static bool floppyIsDevice(const char *const name)
    {
        return name != NULL &&
               (name[0] == 'D' || name[0] == 'd') &&
               (name[1] == 'F' || name[1] == 'f');
    }

} // extern "C"

const struct AdfDeviceDriver xcopyAdfFloppyDriver = {
    "floppy", // name
    NULL,     // data
    NULL,     // createDev: formatting a floppy is adfToDisk's job, not this one's
    floppyOpen,
    floppyClose,
    floppyRead,
    floppyWrite,
    floppyIsNative,
    floppyIsDevice};

void xcopyAdfFloppyAttach(XCopyDisk *disk, XCopyFloppy *floppy, unsigned char retries)
{
    _disk = disk;
    _floppy = floppy;
    _retries = retries > 0 ? retries : 1;
}

void xcopyAdfFloppyInvalidate()
{
    _cachedTrack = -1;
}

bool xcopyAdfFloppyDiskGone()
{
    if (_floppy == nullptr)
        return false;

    /*
       The line, not diskChange(): that one steps the head to re-arm the drive,
       which is a side effect nothing should have just from asking a question
       before every command.

       /DSKCHG is driven low when the disk has been removed or swapped and stays
       low until a step re-arms it, so a low line means what is mounted is no
       longer what is in the drive.
    */
    return !_floppy->readDiskChangeLine();
}
