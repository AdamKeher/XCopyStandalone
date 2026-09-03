/*
 *  adf_dev_driver_ramdisk.c - ramdisk device driver
 *
 *  Copyright (C) 2023      Stuart Caie
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

#include <stdlib.h>
#include <string.h>

#include "adf_dev_driver_ramdisk.h"
#include "adf_dev_type.h"
#include "adf_env.h"
#include "adf_limits.h"

static struct AdfDevice * ramdiskCreate( const char * const  name,
                                         const uint32_t      cylinders,
                                         const uint32_t      heads,
                                         const uint32_t      sectors )
{
    struct AdfDevice * const  dev = ( struct AdfDevice * )
        malloc( sizeof ( struct AdfDevice ) );
    if ( dev == NULL ) {
        adfEnv.eFct( "%s: malloc error", __func__ );
        return NULL;
    }

    dev->readOnly  = false; // ( mode != ADF_ACCESS_MODE_READWRITE );
    dev->geometry.cylinders = cylinders;
    dev->geometry.heads     = heads;
    dev->geometry.sectors   = sectors;
    dev->geometry.blockSize = ADF_DEV_BLOCK_SIZE;
    dev->sizeBlocks         = cylinders * heads * sectors;

    dev->drvData = malloc( (size_t) dev->sizeBlocks * dev->sizeBlocks );
    if ( dev->drvData == NULL ) {
        adfEnv.eFct( "%s: malloc data error", __func__ );
        free( dev );
        return NULL;
    }

    //dev->devType   = adfDevType( dev );
    dev->type  = adfDevGetTypeByGeometry( &dev->geometry );
    dev->class = ( dev->type != ADF_DEVTYPE_UNKNOWN ) ?
        adfDevTypeGetClass( dev->type ) :
        adfDevGetClassBySizeBlocks( dev->sizeBlocks );

    dev->nVol      = 0;
    dev->volList   = NULL;
    dev->mounted   = false;
    dev->name      = strdup( name );
    dev->drv       = &adfDeviceDriverRamdisk;

    return dev;
}


static ADF_RETCODE ramdiskRelease( struct AdfDevice * const  dev )
{
    free( dev->drvData );
    free( dev->name );
    free( dev );
    return ADF_RC_OK;
}


static ADF_RETCODE ramdiskReadSectors( const struct AdfDevice * const  dev,
                                       const uint32_t                  block,
                                       const uint32_t                  lenBlocks,
                                       uint8_t * const                 buf )
{
    if ( block + lenBlocks > dev->sizeBlocks )
        return ADF_RC_ERROR;

    memcpy( buf, &( (uint8_t *) dev->drvData )[ block * dev->geometry.blockSize ],
            dev->geometry.blockSize * lenBlocks  );
    return ADF_RC_OK;
}


static ADF_RETCODE ramdiskWriteSectors( const struct AdfDevice * const  dev,
                                        const uint32_t                  block,
                                        const uint32_t                  lenBlocks,
                                        const uint8_t * const           buf )
{
    if ( block + lenBlocks > dev->sizeBlocks )
        return ADF_RC_ERROR;

    memcpy( &( (uint8_t *) dev->drvData )[ block * dev->geometry.blockSize ], buf,
            dev->geometry.blockSize * lenBlocks );
    return ADF_RC_OK;
}


static bool ramdiskIsDevNative( void )
{
    return false;
}


const struct AdfDeviceDriver adfDeviceDriverRamdisk = {
    .name         = "ramdisk",
    .data         = NULL,
    .createDev    = ramdiskCreate,
    .openDev      = NULL,
    .closeDev     = ramdiskRelease,
    .readSectors  = ramdiskReadSectors,
    .writeSectors = ramdiskWriteSectors,
    .isNative     = ramdiskIsDevNative,
    .isDevice     = NULL
};
