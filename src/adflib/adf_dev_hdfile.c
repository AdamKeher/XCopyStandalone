/*
 *  adf_dev_hdfile.c - device code / hardfile (HDF)
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

#include "adf_dev_hdfile.h"

#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "adf_vol.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


/*
 * adfCreateHdFile
 *
 */
ADF_RETCODE adfCreateHdFile( struct AdfDevice * const  dev,
                             const char * const        volName,
                             const uint8_t             volType )
{
    if ( dev == NULL ) {
        adfEnv.eFct( "%s: dev == NULL", __func__ );
        return ADF_RC_NULLPTR;
    }
    dev->volList = (struct AdfVolume **) malloc( sizeof(struct AdfVolume *) );
    if ( dev->volList == NULL ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }

    dev->volList[ 0 ] = adfVolCreate( dev, 0L, dev->geometry.cylinders,
                                      volName, volType );
    if ( dev->volList[ 0 ] == NULL ) {
        free( dev->volList );
        return ADF_RC_ERROR;
    }

    dev->nVol    = 1;
    dev->class   = ADF_DEVCLASS_HARDFILE;
    dev->mounted = true;

    return ADF_RC_OK;
}


/*
 * adfMountHdFile
 *
 */
ADF_RETCODE adfMountHdFile( struct AdfDevice * const  dev )
{
    dev->nVol = 0;
    dev->volList = (struct AdfVolume **) malloc( sizeof(struct AdfVolume *) );
    if ( dev->volList == NULL ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }

    struct AdfVolume * const vol =
        (struct AdfVolume *) malloc( sizeof(struct AdfVolume) );
    if ( vol == NULL ) {
        free( dev->volList );
        dev->volList = NULL;
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }
    dev->volList[ 0 ] = vol;
    dev->nVol++;      /* fixed by Dan, ... and by Gary */

    vol->dev        = dev;
    vol->volName    = NULL;
    vol->mounted    = false;
    vol->blockSize  = 512;

    vol->firstBlock = 0;

    //const unsigned size = dev->size + 512 - ( dev->size % 512 );
/*printf("size=%ld\n",size);*/

    /* set filesystem info (read from bootblock) */
    struct AdfBootBlock boot;
    ADF_RETCODE rc = adfDevReadBlock(
        dev, (uint32_t) vol->firstBlock, 512, (uint8_t *) &boot );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error reading BootBlock, device %s, volume %d",
                     __func__, dev->name, 0 );
        free( dev->volList );
        dev->volList = NULL;
        return rc;
    }
    memcpy( vol->fs.id, boot.dosType, 3 );
    vol->fs.id[ 3 ]    = '\0';
    vol->fs.type       = (uint8_t) boot.dosType[3];
    vol->datablockSize = ( adfVolIsOFS( vol ) ? 488 : 512 );

    if ( adfVolIsDosFS( vol ) ) {
        vol->rootBlock = (int32_t) ( dev->sizeBlocks / 2 );
        //adfEnv.eFct( "%s: root block %d, root block calc %d",
        //             __func__, vol->rootBlock, adfVolCalcRootBlk( vol ) );
        //assert( vol->rootBlock == adfVolCalcRootBlk( vol ) );
/*printf("root=%ld\n",vol->rootBlock);*/
        uint8_t buf[ 512 ];
        bool found = false;
        do {
            rc = dev->drv->readSectors( dev, (uint32_t) vol->rootBlock, 1, buf );
            if ( rc != ADF_RC_OK ) {
                free( dev->volList );
                dev->volList = NULL;
                return rc;
            }
            found = ( swapUint32fromPtr( buf ) == ADF_T_HEADER &&
                      swapUint32fromPtr( buf + 508 ) == ADF_ST_ROOT );
            if ( ! found )
                (vol->rootBlock)--;
        } while ( vol->rootBlock > 1 && ! found );

        if ( vol->rootBlock == 1 ) {
            adfEnv.eFct( "%s: rootblock not found", __func__ );
            free( dev->volList );
            dev->volList = NULL;
            free( vol );
            dev->nVol = 0;
            return ADF_RC_ERROR;
        }

        // why like this???? are there cases where root is lower than in half
        // and then the remaining part of blocks at the end is not used?
        // is AmigaDos expecting the end to be made after position of rootblock?
        vol->lastBlock = vol->rootBlock * 2 - 1;
        // hdf is a single volume, should cover all device (unless it has size
        // not expressible in blocks - there is a remainder after the last block,
        // smaller than the block size)
        //vol->lastBlock = (int32_t) ( dev->geometry.cylinders *
        //                             dev->geometry.heads *
        //                             dev->geometry.sectors - 1 );

        struct AdfRootBlock root;
        vol->mounted = true;    // must be set to read the root block
        rc = adfReadRootBlock( vol, (uint32_t) vol->rootBlock, &root );
        vol->mounted = false;
        if ( rc != ADF_RC_OK ) {
            free( vol );
            free( dev->volList );
            dev->volList = NULL;
            dev->nVol    = 0;
            return rc;
        }

        vol->volName = strndup( root.diskName,
                                min( root.nameLen,
                                     (unsigned) ADF_MAX_NAME_LEN ) );
    } else { // if ( adfVolIsPFS ( vol ) ) {
        vol->datablockSize = 0; //512;
        vol->volName       = NULL;
        vol->rootBlock     = -1;
        vol->lastBlock     = (int32_t) ( dev->geometry.cylinders *
                                         dev->geometry.heads *
                                         dev->geometry.sectors - 1 );
    }

    return ADF_RC_OK;
}
