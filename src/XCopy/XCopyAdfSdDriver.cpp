#include "XCopyAdfSdDriver.h"
#include "XCopySdFat.h"

#include <string.h>
#include <stdlib.h>
#include <new>

extern "C"
{
#include "../adflib/adf_dev.h"
#include "../adflib/adf_dev_type.h"
#include "../adflib/adf_env.h"
#include "../adflib/adf_err.h"
#include "../adflib/adf_limits.h"
#include "../adflib/adf_blk.h"
}

/*
   The open file, per device.

   FatFile rather than SdFat's File: File is the Arduino compatibility wrapper and
   drags Stream in behind it, and nothing here wants read()/write() semantics that
   return -1 for "no data yet". FatFile is opened against the one shared SdFat
   instance - see XCopySdFat.h for why there is exactly one - and holds its own
   directory entry and cluster chain, so two of these can be open on two images at
   the same time without either knowing about the other.

   It is deliberately not closed and reopened per sector, which is what
   adf_nativ.cpp did: a directory walk is dozens of block reads and each one was
   costing a fresh path lookup down the FAT.
*/
struct SdDevData
{
    FatFile file;
};

// C linkage on the callbacks: the vtable they are assigned to is a C struct, and
// language linkage is part of a function pointer's type.
extern "C"
{

    static SdDevData *dataOf(const struct AdfDevice *const dev)
    {
        return (SdDevData *)dev->drvData;
    }

    static struct AdfDevice *sdDevOpen(const char *const name,
                                       const AdfAccessMode mode)
    {
        struct AdfDevice *const dev =
            (struct AdfDevice *)malloc(sizeof(struct AdfDevice));
        if (dev == NULL)
        {
            adfEnv.eFct("%s: out of memory", __func__);
            return NULL;
        }

        dev->drvData = malloc(sizeof(SdDevData));
        if (dev->drvData == NULL)
        {
            adfEnv.eFct("%s: out of memory", __func__);
            free(dev);
            return NULL;
        }

        // FatFile is not a POD - it has a constructor that clears its state - and
        // this memory came from malloc, so it has to be constructed in place.
        SdDevData *const data = new (dev->drvData) SdDevData();

        dev->readOnly = (mode != ADF_ACCESS_MODE_READWRITE);

        if (!data->file.open(name, dev->readOnly ? O_RDONLY : O_RDWR))
        {
            // Falling back to read only rather than failing: an image on a
            // write protected card, or one the operator only wants to look at,
            // should still open. The caller can see dev->readOnly.
            if (dev->readOnly || !data->file.open(name, O_RDONLY))
            {
                adfEnv.eFct("%s: cannot open '%s'", __func__, name);
                data->~SdDevData();
                free(dev->drvData);
                free(dev);
                return NULL;
            }
            dev->readOnly = true;
        }

        const uint32_t size = data->file.fileSize();
        dev->sizeBlocks = size / ADF_DEV_BLOCK_SIZE;

        if (dev->sizeBlocks == 0)
        {
            adfEnv.eFct("%s: '%s' is smaller than one block", __func__, name);
            data->file.close();
            data->~SdDevData();
            free(dev->drvData);
            free(dev);
            return NULL;
        }

        /*
           Geometry, type and class are deliberately not worked out here.

           adfDevOpenWithDrv_() does it for any driver whose isNative() is false:
           it matches the block count against every floppy layout ADFlib knows,
           falls back to a hardfile shape when nothing matches, and then reads the
           Rigid Disk Block to tell a hardfile from a hard disk. Doing it here as
           well would only be a second opinion that gets overwritten - and getting
           it wrong is exactly what the old adf_nativ.cpp did, by declaring every
           image 80/2/11 DD whatever it held.
        */
        dev->geometry.blockSize = ADF_DEV_BLOCK_SIZE;
        dev->geometry.cylinders = 0;
        dev->geometry.heads = 0;
        dev->geometry.sectors = 0;
        dev->type = ADF_DEVTYPE_UNKNOWN;
        dev->devClass = ADF_DEVCLASS_UNKNOWN;

        dev->name = strdup(name);
        dev->nVol = 0;
        dev->volList = NULL;
        dev->mounted = false;
        dev->drv = &xcopyAdfSdDriver;

        // Cleared rather than left as malloc found it: adfDevClose() free()s
        // rdb.block, and every caller that reaches us through adfDevOpen() or
        // adfDevCreate() sets these afterwards anyway. It is two stores against a
        // free() of a wild pointer if one ever does not.
        dev->rdb.status = ADF_DEV_RDB_STATUS_UNKNOWN;
        dev->rdb.block = NULL;

        return dev;
    }

    /*
       Create a new image of the given geometry.

       Written as one seek to the last block and one block written, rather than a
       loop over every block: SdFat fills the gap with zeroes on its own, and the
       loop would be 1,760 block writes to produce a file that is entirely zeroes
       except for the boot block a later adfVolCreate() puts there.
    */
    static struct AdfDevice *sdDevCreate(const char *const name,
                                         const uint32_t cylinders,
                                         const uint32_t heads,
                                         const uint32_t sectors)
    {
        const uint32_t blocks = cylinders * heads * sectors;
        if (blocks == 0)
        {
            adfEnv.eFct("%s: empty geometry", __func__);
            return NULL;
        }

        {
            FatFile file;
            if (!file.open(name, O_RDWR | O_CREAT | O_TRUNC))
            {
                adfEnv.eFct("%s: cannot create '%s'", __func__, name);
                return NULL;
            }

            uint8_t zeroes[ADF_LOGICAL_BLOCK_SIZE];
            memset(zeroes, 0, sizeof(zeroes));

            const bool written =
                file.seekSet((blocks - 1) * (uint32_t)ADF_LOGICAL_BLOCK_SIZE) &&
                file.write(zeroes, sizeof(zeroes)) == (int)sizeof(zeroes) &&
                file.sync();

            file.close();

            if (!written)
            {
                adfEnv.eFct("%s: cannot size '%s' to %lu blocks", __func__,
                            name, (unsigned long)blocks);
                return NULL;
            }
        }

        struct AdfDevice *const dev = sdDevOpen(name, ADF_ACCESS_MODE_READWRITE);
        if (dev == NULL)
            return NULL;

        // The caller asked for a geometry; honour it even when the size does not
        // match a layout adfDevGetTypeBySizeBlocks() recognises.
        dev->geometry.cylinders = cylinders;
        dev->geometry.heads = heads;
        dev->geometry.sectors = sectors;
        dev->geometry.blockSize = ADF_DEV_BLOCK_SIZE;
        dev->type = adfDevGetTypeByGeometry(&dev->geometry);
        dev->devClass = (dev->type != ADF_DEVTYPE_UNKNOWN)
                            ? adfDevTypeGetClass(dev->type)
                            : adfDevGetClassBySizeBlocks(dev->sizeBlocks);

        return dev;
    }

    static ADF_RETCODE sdDevClose(struct AdfDevice *const dev)
    {
        if (dev->mounted)
            adfDevUnMount(dev);

        SdDevData *const data = dataOf(dev);
        data->file.sync();
        data->file.close();
        data->~SdDevData();

        free(dev->drvData);
        free(dev->name);
        free(dev);

        return ADF_RC_OK;
    }

    static ADF_RETCODE sdDevRead(const struct AdfDevice *const dev,
                                 const uint32_t block,
                                 const uint32_t lenBlocks,
                                 uint8_t *const buf)
    {
        if (block + lenBlocks > dev->sizeBlocks)
        {
            adfEnv.eFct("%s: block %lu is past the end of the image", __func__,
                        (unsigned long)(block + lenBlocks - 1));
            return ADF_RC_ERROR;
        }

        FatFile &file = dataOf(dev)->file;
        const uint32_t bytes = lenBlocks * dev->geometry.blockSize;

        if (!file.seekSet(block * (uint32_t)dev->geometry.blockSize))
        {
            adfEnv.eFct("%s: seek to block %lu failed", __func__,
                        (unsigned long)block);
            return ADF_RC_ERROR;
        }

        // read() returns -1 on error and a short count at end of file. The old
        // driver returned RC_OK unconditionally, so a short read left the caller
        // parsing whatever happened to be in its buffer.
        const int got = file.read(buf, bytes);
        if (got != (int)bytes)
        {
            adfEnv.eFct("%s: short read at block %lu", __func__,
                        (unsigned long)block);
            return ADF_RC_ERROR;
        }

        return ADF_RC_OK;
    }

    static ADF_RETCODE sdDevWrite(const struct AdfDevice *const dev,
                                  const uint32_t block,
                                  const uint32_t lenBlocks,
                                  const uint8_t *const buf)
    {
        if (dev->readOnly)
        {
            adfEnv.eFct("%s: '%s' is mounted read only", __func__, dev->name);
            return ADF_RC_ERROR;
        }

        if (block + lenBlocks > dev->sizeBlocks)
        {
            adfEnv.eFct("%s: block %lu is past the end of the image", __func__,
                        (unsigned long)(block + lenBlocks - 1));
            return ADF_RC_ERROR;
        }

        FatFile &file = dataOf(dev)->file;
        const uint32_t bytes = lenBlocks * dev->geometry.blockSize;

        if (!file.seekSet(block * (uint32_t)dev->geometry.blockSize))
        {
            adfEnv.eFct("%s: seek to block %lu failed", __func__,
                        (unsigned long)block);
            return ADF_RC_ERROR;
        }

        if (file.write(buf, bytes) != (int)bytes)
        {
            adfEnv.eFct("%s: short write at block %lu", __func__,
                        (unsigned long)block);
            return ADF_RC_ERROR;
        }

        /*
           Flushed per write, not per close.

           The card can be pulled at any moment and the operator gets no warning
           when it happens. A dirty FAT costs the whole image, not the one block
           that was in flight, so the cost of the sync - a few milliseconds on a
           block that was going to be written anyway - buys back the difference.
        */
        if (!file.sync())
        {
            adfEnv.eFct("%s: sync failed at block %lu", __func__,
                        (unsigned long)block);
            return ADF_RC_ERROR;
        }

        return ADF_RC_OK;
    }

    /*
       Not a native device: the geometry here is inferred from a file's length, not
       read off a drive. The live floppy driver is the one that answers true.
    */
    static bool sdDevIsNative(void)
    {
        return false;
    }

    /*
       Claims any name that is not another driver's.

       adfGetDeviceDriverByDevName() asks each driver in turn and falls back to
       "dump" if none claims the name - and dump is not compiled into this firmware,
       so something has to say yes or plain adfDevOpen() would always fail. There is
       only one other driver on this board and it owns names beginning "DF", which
       is a prefix no SD path starts with.
    */
    static bool sdDevIsDevice(const char *const name)
    {
        if (name == NULL)
            return false;

        const bool isFloppy = (name[0] == 'D' || name[0] == 'd') &&
                              (name[1] == 'F' || name[1] == 'f');
        return !isFloppy;
    }

} // extern "C"

const struct AdfDeviceDriver xcopyAdfSdDriver = {
    "sd",         // name
    NULL,         // data
    sdDevCreate,  // createDev
    sdDevOpen,    // openDev
    sdDevClose,   // closeDev
    sdDevRead,    // readSectors
    sdDevWrite,   // writeSectors
    sdDevIsNative,
    sdDevIsDevice};
