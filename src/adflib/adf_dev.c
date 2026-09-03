/*
 *  adf_dev.c - device code
 *
 *  Copyright (C) 1997-2022 Laurent Clevy
 *                2023-2026 Tomasz Wolak
 *
 *  This file is part of ADFLib.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "adf_dev.h"

#include "adf_dev_drivers.h"
#include "adf_dev_flop.h"
#include "adf_dev_hd.h"
#include "adf_dev_hdfile.h"
#include "adf_env.h"
#include "adf_limits.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static struct AdfDevice * adfDevOpenWithDrv_(
    const struct AdfDeviceDriver * const  driver,
    const char * const                    name,
    const AdfAccessMode                   mode );

static ADF_RETCODE adfDevSetCalculatedGeometry_( struct AdfDevice * const  dev );
static ADF_RETCODE adfDevReadRdb( struct AdfDevice * const dev );


/*****************************************************************************
 *
 * Public functions
 *
 *****************************************************************************/

/*
 * adfDevCreate
 *
 */
struct AdfDevice * adfDevCreate(  const char * const  driverName,
                                  const char * const  name,
                                  const uint32_t      cylinders,
                                  const uint32_t      heads,
                                  const uint32_t      sectors )
{
    uint32_t sizeBlocks = cylinders * heads * sectors;
    if ( sizeBlocks > ADF_DEV_SIZE_MAX_BLOCKS ) {
        adfEnv.eFct( " %s: size %u blocks is bigger than max. %u blocks",
                     sizeBlocks, ADF_DEV_SIZE_MAX_BLOCKS );
        return NULL;
    }

    const struct AdfDeviceDriver * const  driver = adfGetDeviceDriverByName( driverName );
    if ( driver == NULL || driver->createDev == NULL )
        return NULL;

    struct AdfDevice * const dev = driver->createDev( name, cylinders, heads, sectors );
    if ( dev == NULL )
        return NULL;

    dev->rdb.status = ADF_DEV_RDB_STATUS_NOTFOUND;  // better: ADF_DEV_RDB_STATUS_UNCHECKED ?
    dev->rdb.block  = NULL;

    return dev;
}


/*
 * adfDevOpen
 *
 */
struct AdfDevice * adfDevOpen( const char * const   name,
                               const AdfAccessMode  mode )
{
    return adfDevOpenWithDrv_( adfGetDeviceDriverByDevName( name ), name, mode );
}

/*
 * adfDevOpenWithDriver
 *
 */
struct AdfDevice * adfDevOpenWithDriver( const char * const   driverName,
                                         const char * const   name,
                                         const AdfAccessMode  mode )
{
    return adfDevOpenWithDrv_( adfGetDeviceDriverByName( driverName ), name, mode );
}


/*
 * adfDevClose
 *
 * Closes/releases an opened device.
 */
void adfDevClose( struct AdfDevice * const  dev )
{
    if ( dev == NULL )
        return;

    free( dev->rdb.block );
    dev->rdb.block = NULL;

    if ( dev->mounted )
        adfDevUnMount( dev );

    dev->drv->closeDev( dev );
}


/*
 * adfDevMount
 *
 * mount a dump file (.adf) or a real device (uses adf_nativ.c and .h)
 *
 * adfInitDevice() must fill dev->size !
 */
ADF_RETCODE adfDevMount( struct AdfDevice * const  dev )
{
    if ( dev == NULL )
        return ADF_RC_ERROR;

    ADF_RETCODE rc;

    switch( dev->class ) {

    case ADF_DEVCLASS_FLOP:
        rc = adfMountFlop ( dev );
        break;

    case ADF_DEVCLASS_HARDDISK:
        rc = adfMountHd( dev );
        break;

    case ADF_DEVCLASS_HARDFILE:
        rc = adfMountHdFile( dev );
        break;

    default:
        adfEnv.eFct("%s: unknown device type", __func__ );
        return ADF_RC_ERROR;								/* BV */
    }

    if ( rc != ADF_RC_OK )
        return rc;

    dev->mounted = true;
    return ADF_RC_OK;
}

/*
 * adfDevUnMount
 *
 */
void adfDevUnMount( struct AdfDevice * const  dev )
{
    if ( ! dev->mounted )
        return;

    // free volume list
    //if ( dev->volList ) {
    if ( dev->nVol > 0 ) {
        for ( int i = 0 ; i < dev->nVol ; i++ ) {
            free ( dev->volList[i]->volName );
            free ( dev->volList[i] );
        }
        free ( dev->volList );
        dev->nVol = 0;
    }

    dev->volList = NULL;
    dev->mounted = false;
}

/*
 * adfDevReadBlock
 *
 */
ADF_RETCODE adfDevReadBlock( const struct AdfDevice * const  dev,
                             uint32_t                        pSect,
                             const uint32_t                  size,
                             uint8_t * const                 buf )
{
    const unsigned nFullBlocks = size / dev->geometry.blockSize;
    ADF_RETCODE rc = dev->drv->readSectors( dev, pSect, nFullBlocks, buf );
    if ( rc != ADF_RC_OK )
        return rc;

    const unsigned remainder = size % dev->geometry.blockSize;
    if ( remainder != 0 ) {
#ifdef _MSC_VER
        uint8_t* const blockBuf = _alloca(dev->geometry.blockSize);
#else
        uint8_t blockBuf[ dev->geometry.blockSize ];
#endif
        ADF_RETCODE rc = dev->drv->readSectors( dev, pSect + nFullBlocks, 1, blockBuf );
        if ( rc != ADF_RC_OK )
            return rc;
        memcpy( buf + size - remainder, blockBuf, remainder );
    }

    return ADF_RC_OK;
}

/*
 * adfDevWriteBlock
 *
 */
ADF_RETCODE adfDevWriteBlock( const struct AdfDevice * const  dev,
                              uint32_t                        pSect,
                              const uint32_t                  size,
                              const uint8_t * const           buf )
{
    const unsigned nFullBlocks = size / dev->geometry.blockSize;
    ADF_RETCODE rc = dev->drv->writeSectors( dev, pSect, nFullBlocks, buf );
    if ( rc != ADF_RC_OK )
        return rc;

    const unsigned remainder = size % dev->geometry.blockSize;
    if ( remainder != 0 ) {
#ifdef _MSC_VER
        uint8_t * const blockBuf = _alloca( dev->geometry.blockSize );
#else
        uint8_t blockBuf[dev->geometry.blockSize];
#endif
        memcpy( blockBuf, buf + size - remainder, remainder );
        memset( blockBuf + remainder, 0, dev->geometry.blockSize - remainder );
        ADF_RETCODE rc = dev->drv->writeSectors( dev, pSect + nFullBlocks, 1, blockBuf );
        if ( rc != ADF_RC_OK )
            return rc;
    }

    return ADF_RC_OK;
}


/*****************************************************************************
 *
 * Private / lower-level functions
 *
 *****************************************************************************/


static struct AdfDevice * adfDevOpenWithDrv_(
    const struct AdfDeviceDriver * const  driver,
    const char * const                    name,
    const AdfAccessMode                   mode )
{
    if ( driver == NULL || driver->openDev == NULL )
        return NULL;

    struct AdfDevice * const  dev = driver->openDev( name, mode );
    if ( dev == NULL ) {
        adfEnv.eFct( " %s: openDev failed, dev. name '%s'", __func__, name );
        return NULL;
    }

    // set class depending only on size (until more data available...)
    dev->class = adfDevGetClassBySizeBlocks( dev->sizeBlocks );

    // if no geometry from the device (not native) set something reasonable
    if ( ! dev->drv->isNative() ) {
        if ( adfDevSetCalculatedGeometry_( dev ) != ADF_RC_OK ) {
            adfEnv.eFct( " %s: setting calc. geometry failed, dev. name '%s'",
                         __func__, name );
            dev->drv->closeDev( dev );
            return NULL;
        }
    }

    // check if the geometry (read or calculated) is a valid one
    if ( ! adfDevIsGeometryValid( &dev->geometry, dev->sizeBlocks ) ) {
        if ( dev->drv->isNative()
             || dev->type != ADF_DEVTYPE_UNKNOWN )   // from the predefined list should be valid...
        {
            adfEnv.eFct( "%s: invalid geometry: cyliders %u, "
                         "heads: %u, sectors: %u, size (in blocks): %u, device: %s",
                         __func__, dev->geometry.cylinders, dev->geometry.heads,
                         dev->geometry.sectors, dev->sizeBlocks, dev->name );
            dev->drv->closeDev( dev );
            return NULL;
        } /*else {
            // only a warning (many exsting HDFs are like this...)
            const uint32_t sizeFromGeometry = dev->geometry.cylinders *
                                              dev->geometry.heads *
                                              dev->geometry.sectors * 512;
            adfEnv.wFct( "%s: size from geometry %u != device size %u\n"
                     "(%u bytes left over geometry)\n"
                     "This is most likely due to size of the device (%u) not divisible\n"
                     "by sector size (512): real size(%u) %% 512 = %u bytes left unused\n",
                     __func__, sizeFromGeometry, dev->size,
                     dev->size % 512,
                     dev->size, dev->size, dev->size % 512 );
            //assert( dev->size % 512 == dev->size - sizeFromGeometry );
            }*/
    }

    // check if the dev contains and RDB
    dev->rdb.block = NULL;             // was not initialized yet
    ADF_RETCODE rc = adfDevReadRdb( dev );
    if ( rc != ADF_RC_OK ) {
        // some critical error (mem alloc. or cannot read block 0)
        adfDevClose( dev );
        return NULL;
    }

    // classify device depending on having or not having RDB (HARDDISK vs. HARDFILE)
    if ( dev->rdb.status >= ADF_DEV_RDB_STATUS_EXIST ) {
        if ( dev->class == ADF_DEVCLASS_FLOP ) {
            // This should not happen, but... theoretically should work, too.
            // (still, at least warn...)
            adfEnv.wFct( " %s: '%s' is a floppy but Rigid Device Block (RDB) was found"
                         "(unusual, but let it be...)",
                         __func__, name );
        }

        dev->class = ADF_DEVCLASS_HARDDISK;
    } else {
        if ( dev->class == ADF_DEVCLASS_HARDDISK )
            dev->class = ADF_DEVCLASS_HARDFILE;
    }

    // if hard disk (has RDB) - update geometry from data stored in RDB
    if ( dev->class == ADF_DEVCLASS_HARDDISK ) {
        const struct AdfRDSKblock * const rdsk = dev->rdb.block;
        assert( rdsk != NULL );

        if ( dev->geometry.cylinders == rdsk->cylinders &&
             dev->geometry.heads     == rdsk->heads     &&
             dev->geometry.sectors   == rdsk->sectors )
        {
            dev->rdb.status = ADF_DEV_RDB_STATUS_SAMEGEOMETRY;
        } else {
            // not sure about some safety checks - but rather client code
            // should do that (using state of RDB block)
            /*if ( mode == ADF_ACCESS_MODE_READWRITE ) &&
                   dev->rdb.status == ADF_DEV_RDB_STATUS_CHECKSUMERROR )
            {
                // inconsistent geometry might be dangerous - do not allow to write (?)
                // (on the other hand - the lib might be used to repair it...)
                adfDevClose( dev );
                return NULL;
            }*/

            // use geometry from RDB as the disk structure layout should be
            // according to it (if not - something to repair, beyond the lib...)
            //
            // (to consider, how to make it possible to specify (or even enforce)
            // which geometry to use)
            const unsigned sizeBlocksFromRdb = rdsk->cylinders *
                                               rdsk->heads *
                                               rdsk->sectors;
            adfEnv.wFct(
                "%s: using geometry from Rigid Block, "
                "different than detected (or, if not a real disk, calculated):\n"
                "                detected                rdsk block\n"
                " cyliders:      %8u                  %8u\n"
                " heads:         %8u                  %8u\n"
                " sectors:       %8u                  %8u\n"
                " size (blocks): %8u                  %8u  %s",
                __func__,
                dev->geometry.cylinders, rdsk->cylinders,
                dev->geometry.heads,     rdsk->heads,
                dev->geometry.sectors,   rdsk->sectors,
                dev->sizeBlocks,         sizeBlocksFromRdb,
                dev->sizeBlocks != sizeBlocksFromRdb ? " DIFFERENT SIZE(!)" : "" );
            dev->geometry.cylinders = rdsk->cylinders;
            dev->geometry.heads     = rdsk->heads;
            dev->geometry.sectors   = rdsk->sectors;
        }
    }

    // update device and type after having final geometry set
    dev->type  = adfDevGetTypeByGeometry( &dev->geometry );  // do we have to?
    //dev->class = ( dev->type != ADF_DEVTYPE_UNKNOWN ) ?
    //    adfDevTypeGetClass( dev->type ) :
    //    adfDevGetClassBySizeBlocks( dev->sizeBlocks );

    // ensure the lib can handle the size
    if ( dev->sizeBlocks > ADF_DEV_SIZE_MAX_BLOCKS ) {
        adfEnv.eFct( " %s: size %u blocks is bigger than max. %u blocks",
                     __func__, dev->sizeBlocks, ADF_DEV_SIZE_MAX_BLOCKS );
        adfDevClose( dev );
        return NULL;
    }

    return dev;
}


static ADF_RETCODE adfDevSetCalculatedGeometry_( struct AdfDevice * const  dev )
{
    // set geometry (based on already set size)

    // first - check predefined types
    dev->type = adfDevGetTypeBySizeBlocks( dev->sizeBlocks );
    if ( dev->type != ADF_DEVTYPE_UNKNOWN ) {
        dev->geometry = adfDevTypeGetGeometry( dev->type );
        return ADF_RC_OK;
    }

    // if not found on the predefined list - guess something reasonable...
    if ( dev->class == ADF_DEVCLASS_HARDDISK ||
         dev->class == ADF_DEVCLASS_HARDFILE )
    {
        // partitions must be aligned with cylinders(tracks) - this gives most
        // flexibility
        dev->geometry.cylinders = dev->sizeBlocks;
        dev->geometry.heads     = 1;
        dev->geometry.sectors   = 1;
    } else {
        adfEnv.eFct( "%s: invalid dev class %d", __func__, dev->class );
        return ADF_RC_ERROR;
    }

    return ADF_RC_OK;
}


static ADF_RETCODE adfDevReadRdb( struct AdfDevice * const dev )
{
#ifdef _MSC_VER
    uint8_t* const block = _alloca( dev->geometry.blockSize );
#else
    uint8_t block[ dev->geometry.blockSize ];
#endif
    ADF_RETCODE rc = adfDevReadBlock( dev, 0, dev->geometry.blockSize, block );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: reading block 0 of %s failed", __func__, dev->name );
        dev->rdb.status = ADF_DEV_RDB_STATUS_UNREADABLE;
        return rc;
    }

    if ( strncmp( "RDSK", (char *) block, 4 ) != 0 ) {
        dev->rdb.status = ADF_DEV_RDB_STATUS_NOTFOUND;
        return ADF_RC_OK;
    }
    dev->rdb.status = ADF_DEV_RDB_STATUS_EXIST;
    if ( dev->rdb.block == NULL ) {
        dev->rdb.block = malloc( sizeof(struct AdfRDSKblock) );
        if ( dev->rdb.block == NULL )
            return ADF_RC_MALLOC;
    }

    rc = adfReadRDSKblock( dev, dev->rdb.block );
    if ( rc != ADF_RC_OK ) {
        if ( rc == ADF_RC_BLOCKSUM )
            dev->rdb.status = ADF_DEV_RDB_STATUS_CHECKSUMERROR;
        //else  ->  some other error (keep status "EXIST")
    } else {
        dev->rdb.status = ADF_DEV_RDB_STATUS_OK;
    }

    return ADF_RC_OK;
}


/*
 * adfDevGetInfo
 *
 */
#define DEVINFO_SIZE 1024

char * adfDevGetInfo( const struct AdfDevice * const  dev )
{
    const char * devTypeInfo = NULL;
    if ( dev->type != ADF_DEVTYPE_UNKNOWN ) {
        devTypeInfo = adfDevTypeGetDescription( dev->type );
    } else {
        switch ( dev->class ) {
            // floppies must be covered above

        case ADF_DEVCLASS_HARDDISK:
            devTypeInfo = "harddisk";
            break;
        case ADF_DEVCLASS_HARDFILE:
            devTypeInfo = "hardfile";
            break;
        default:
            devTypeInfo = "unknown device type!";
        }
    }

    char * const info = malloc( DEVINFO_SIZE + 1 );
    if ( info == NULL )
        return NULL;

    char *infoptr = info;
    infoptr += snprintf( infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
                         "\nADF device info:\n  Type:\t\t%s\n  Driver:\t%s\n",
                         devTypeInfo, dev->drv->name );

    infoptr += snprintf( infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
                         "  Geometry:\n"
            "    Cylinders\t%d\n"
            "    Heads\t%d\n"
            "    Sectors\t%d\n\n",
            dev->geometry.cylinders, dev->geometry.heads, dev->geometry.sectors );

    infoptr += snprintf(
        infoptr, (size_t) ( DEVINFO_SIZE - ( infoptr - info ) ),
        "  Volumes:\t%d%s\n", dev->nVol,
        dev->nVol > 0 ? "\n   idx  first bl.     last bl.    filesystem    name" : "" );

    for ( int i = 0 ; i < dev->nVol ; i++ ) {
        const struct AdfVolume * const vol = dev->volList[i];
        const char * const fstype = ( adfVolIsDosFS( vol ) ) ?
            ( adfVolIsOFS( vol ) ? "OFS" : "FFS" ) : "???";
        infoptr += snprintf(
            infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
            "    %2d  %9d    %9d    %s(%s)      \"%s\"", i,
                vol->firstBlock,
                vol->lastBlock,
                adfVolIsFsValid(vol) ? vol->fs.id : "???",
                fstype,
                vol->volName ? vol->volName : "" );
        if ( vol->mounted )
            infoptr += snprintf( infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
                                 "    mounted");
        infoptr += snprintf( infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
                             "\n");
    }
    infoptr += snprintf( infoptr, (size_t)( DEVINFO_SIZE - ( infoptr - info ) ),
                         "\n");
    assert( infoptr - info < DEVINFO_SIZE );
    return info;
}
