/*
 *  adf_dev_flop.c - device code / floppy
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

#include "adf_dev_flop.h"

#include "adf_byteorder.h"
#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "adf_vol.h"

#include <stdlib.h>
#include <string.h>


/*
 * adfMountFlop
 *
 * normaly not used directly, called directly by adfDevMount()
 *
 * use dev->devType to choose between DD and HD
 * fills the volume list with one volume
 */
ADF_RETCODE adfMountFlop( struct AdfDevice * const  dev )
{
    struct AdfVolume * const vol = (struct AdfVolume *)
        malloc( sizeof(struct AdfVolume) );
    if ( vol == NULL ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }

    vol->firstBlock = 0;
    vol->lastBlock  = (int32_t) ( dev->geometry.cylinders *
                                  dev->geometry.heads *
                                  dev->geometry.sectors - 1 );
    vol->blockSize  = 512;
    vol->dev        = dev;
    vol->volName    = NULL;
    vol->mounted    = false;

    /* set filesystem info (read from bootblock) */
    struct AdfBootBlock boot;
    ADF_RETCODE rc =
        adfDevReadBlock(   // reading as raw(!), to avoid errors (can have an unknown fs)
        dev, (uint32_t) vol->firstBlock, 512, (uint8_t *) &boot );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error reading BootBlock, device %s, volume %d",
                     __func__, dev->name, 0 );
        free( vol );
        return rc;
    }
    memcpy( vol->fs.id, boot.dosType, 3 );
    vol->fs.id[ 3 ] = '\0';
    vol->fs.type = (uint8_t) boot.dosType[ 3 ];

    if ( adfVolIsDosFS( vol ) ) {
#ifdef LITT_ENDIAN
        // boot was read as raw, must change byte order
        adfSwapEndian( (uint8_t *) &boot, ADF_SWBL_BOOT );
#endif
        vol->datablockSize = adfVolIsOFS( vol ) ? 488 : 512;

        // read root block (to get the volume's name)
        struct AdfRootBlock root;
        if ( boot.rootBlock > 1 ) {  // make sure it is not pointing to bootBlock(!)
            // try from sector set in boot block
            vol->rootBlock = boot.rootBlock;
            vol->mounted = true;    // must be set to read the root block
            rc = adfReadRootBlock( vol, (uint32_t) vol->rootBlock, &root );
            vol->mounted = false;
        }
        if ( boot.rootBlock <= 1  ||  rc != ADF_RC_OK ) {
            // try from calculated position
            vol->rootBlock = adfVolCalcRootBlk( vol );
            vol->mounted = true;    // must be set to read the root block
            rc = adfReadRootBlock( vol, (uint32_t) vol->rootBlock, &root );
            vol->mounted = false;
        }
        if ( rc != ADF_RC_OK ) {
            free( vol );
            return rc;
        }

        vol->volName = strndup( root.diskName,
                                min( root.nameLen,
                                     (unsigned) ADF_MAX_NAME_LEN ) );

        if ( boot.rootBlock != vol->rootBlock ) {
            adfEnv.wFct( "%s: rootBlock sector set in bootblock %d, "
                         "different than calculated %d, volume '%s'",
                         __func__, boot.rootBlock, vol->rootBlock, vol->volName );
        }
    } else { // if ( adfVolIsPFS ( vol ) ) {
        vol->datablockSize = 0; //512;
        vol->volName       = NULL;
        vol->rootBlock     = -1;
    }

    dev->volList = (struct AdfVolume **) malloc( sizeof(struct AdfVolume *) );
    if ( ! dev->volList ) {
        free( vol->volName );
        free( vol );
        adfEnv.eFct( "%s: malloc, device '%s'", __func__, dev->name );
        return ADF_RC_MALLOC;
    }
    dev->volList[ 0 ] = vol;
    dev->nVol         = 1;

/*printf("root=%d\n",vol->rootBlock);	    */
    return ADF_RC_OK;
}


/*
 * adfCreateFlop
 *
 * create a filesystem on a floppy device
 * fills dev->volList[]
 */
ADF_RETCODE adfCreateFlop( struct AdfDevice * const  dev,
                           const char * const        volName,
                           const uint8_t             volType )
{
    if ( dev == NULL ) {
        adfEnv.eFct( "%s: dev == NULL", __func__ );
        return ADF_RC_NULLPTR;
    }
    if ( volName == NULL ) {
        adfEnv.eFct( "%s: volName == NULL", __func__ );
        return ADF_RC_NULLPTR;
    }
    dev->volList = (struct AdfVolume **) malloc( sizeof(struct AdfVolume *) );
    if ( ! dev->volList ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }
    dev->volList[ 0 ] = adfVolCreate( dev, 0L, 80L, volName, volType );
    if ( dev->volList[ 0 ] == NULL ) {
        free( dev->volList );
        return ADF_RC_ERROR;
    }
    dev->nVol    = 1;

    // must be already set in dev open
    //dev->devType = ( dev->sectors == 11 ? ADF_DEVTYPE_FLOPDD :
    //                                      ADF_DEVTYPE_FLOPHD );
    dev->mounted = true;

    return ADF_RC_OK;
}
