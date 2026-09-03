/*
 *  adf_vol.c - logical disk/volume code
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

#include "adf_vol.h"

#include "adf_bitm.h"
#include "adf_cache.h"
#include "adf_dev.h"
#include "adf_dir.h"
#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * adfVolCheckFilesystem
 *
 * Returns ADF_RC_OK if no errors found.
 */
static ADF_RETCODE adfVolCheckFilesystem( struct AdfVolume * const  vol );



/*
 * adfVolCreate
 *
 *
 */
struct AdfVolume * adfVolCreate( struct AdfDevice * const  dev,
                                 const uint32_t            start,
                                 const uint32_t            len,
                                 const char * const        volName,
                                 const uint8_t             volType )
{
/*    struct AdfDirCacheBlock dirc;*/
    ADF_SECTNUM blkList[2];

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 0 );

    struct AdfVolume * const vol = (struct AdfVolume *) malloc( sizeof(struct AdfVolume) );
    if ( ! vol ) {
        adfEnv.eFct( "%s: malloc vol", __func__ );
        return NULL;
    }

    vol->dev        = dev;
    vol->firstBlock = (int32_t)( dev->geometry.heads *
                                 dev->geometry.sectors * start );
    vol->lastBlock  = vol->firstBlock + (int32_t)( dev->geometry.heads *
                                                   dev->geometry.sectors * len ) - 1;
    vol->blockSize  = 512;
    vol->rootBlock  = adfVolCalcRootBlk( vol );

/*printf("first=%ld last=%ld root=%ld\n",vol->firstBlock,
 vol->lastBlock, vol->rootBlock);
*/
    vol->curDirPtr = vol->rootBlock;
    vol->readOnly  = dev->readOnly;
    vol->mounted   = true;
    vol->volName   = strndup( volName,
                              min( strlen( volName ),
                                   (unsigned) ADF_MAX_NAME_LEN ) );
    if ( vol->volName == NULL ) {
        adfEnv.eFct( "%s: malloc volName", __func__ );
        free( vol );
        return NULL;
    }

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 25 );

    strncpy( vol->fs.id, "DOS", 3 );
    vol->fs.id[ 3 ] = '\0';
    vol->fs.type    = volType;

    struct AdfBootBlock boot;
    memset( &boot, 0, 1024 );
    //strncpy ( boot.dosType, "DOS", 3 ); /// done in adfWriteBootBlock
    boot.dosType[ 3 ] = (char) volType;
    boot.rootBlock    = vol->rootBlock;
/*printf("first=%d last=%d\n", vol->firstBlock, vol->lastBlock);
printf("name=%s root=%d\n", vol->volName, vol->rootBlock);
*/
    if ( adfWriteBootBlock( vol, &boot ) != ADF_RC_OK )
        goto volcreate_error;

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 20 );

    if ( adfCreateBitmap( vol ) != ADF_RC_OK )
        goto volcreate_error;

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 40 );


/*for(i=0; i<127; i++)
printf("%3d %x, ",i,vol->bitmapTable[0]->map[i]);
*/
    if ( adfDosFsHasDIRCACHE( volType ) )
        adfGetFreeBlocks( vol, 2, blkList );
    else
        adfGetFreeBlocks( vol, 1, blkList );


/*printf("[0]=%d [1]=%d\n",blkList[0],blkList[1]);*/

    struct AdfRootBlock root;
    memset( &root, 0, ADF_LOGICAL_BLOCK_SIZE );

    root.nameLen = (uint8_t) strlen( vol->volName );
    memcpy( root.diskName, volName, root.nameLen );
    adfTime2AmigaTime( adfGiveCurrentTime(), &root.coDays, &root.coMins,
                       &root.coTicks );

    /* dircache block */
    if ( adfDosFsHasDIRCACHE( volType ) ) {
        root.extension = 0L;
        root.secType   = ADF_ST_ROOT; /* needed by adfCreateEmptyCache() */
        adfCreateEmptyCache( vol, (struct AdfEntryBlock *) &root, blkList[ 1 ] );
    }

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 60 );

    if ( adfWriteRootBlock( vol, (uint32_t) blkList[ 0 ], &root ) != ADF_RC_OK )
        goto volcreate_error;

   /* fills root->bmPages[] and writes filled bitmapExtBlocks */
    if ( adfWriteNewBitmap( vol ) != ADF_RC_OK )
        goto volcreate_error;

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 80 );

    if ( adfUpdateBitmap( vol ) != ADF_RC_OK )
        goto volcreate_error;

    if ( adfEnv.useProgressBar )
        adfEnv.progressBar( 100 );
/*printf("free blocks %ld\n",adfCountFreeBlocks(vol));*/

    /* will be managed by adfMount() later */
    adfFreeBitmap( vol );

    vol->mounted = false;

    return vol;

volcreate_error:
    free( vol->volName );
    free( vol );
    return NULL;
}


static struct AdfVolume * adfVolMount_( struct AdfDevice * const  dev,
                                        const int                 nPart,
                                        const AdfAccessMode       mode );

struct AdfVolume * adfVolMount( struct AdfDevice * const  dev,
                                const int                 nPart,
                                const AdfAccessMode       mode )
{
    // mount read-only
    struct AdfVolume * vol = adfVolMount_( dev, nPart, ADF_ACCESS_MODE_READONLY );
    if ( mode == ADF_ACCESS_MODE_READONLY )
        return vol;

    // if read-write requested - must check filesystem consistency first
    // (to prevent possible damages caused by writing a dirty filesystem)
    ADF_RETCODE rc = adfVolCheckFilesystem( vol );
    if ( rc != ADF_RC_OK ) {
        adfVolUnMount( vol );
        return NULL;
    }

    rc = adfVolRemount( vol, mode );
    if ( rc != ADF_RC_OK ) {
        adfVolUnMount( vol );
        return NULL;
    }

    return vol;
}


/*
 * adfVolMount
 *
 * 
 */
static struct AdfVolume * adfVolMount_( struct AdfDevice * const  dev,
                                        const int                 nPart,
                                        const AdfAccessMode       mode )
{
    if ( dev == NULL ) {
        adfEnv.eFct( "%s: invalid device (NULL)", __func__ );
        return NULL;
    }

    if ( dev->nVol < 1 ) {
        adfEnv.eFct( "%s: volume list empty (none or not loaded), "
                     "(requested: device %s, volume %d)",
                     __func__, dev->name, nPart );
        return NULL;
    }

    if ( nPart < 0  ||
         nPart >= dev->nVol )
    {
        adfEnv.eFct( "%s: invalid partition %d", __func__, nPart );
        return NULL;
    }

    if ( mode != ADF_ACCESS_MODE_READONLY  &&
         mode != ADF_ACCESS_MODE_READWRITE )
    {
        adfEnv.eFct( "%s: invalid mode %d", __func__, mode );
        return NULL;
    }

    struct AdfVolume * const vol = dev->volList[ nPart ];

    if ( ! adfVolIsDosFS( vol ) ) {
        if ( adfVolIsPFS( vol ) ) {
            adfEnv.eFct( "%s: a PFS volume, not supported (device %s, volume %d)",
                         __func__, dev->name, nPart );
            return NULL;
        }
        adfEnv.eFct( "%s: filesystem not supported (device %s, volume %d)",
                     __func__, dev->name, nPart );
        return NULL;
    }

    vol->mounted = true;

/*printf("first=%ld last=%ld root=%ld\n",vol->firstBlock,
 vol->lastBlock, vol->rootBlock);
*/
    vol->datablockSize = adfVolIsOFS( vol ) ? 488 : 512;

    if ( dev->readOnly /*|| isDIRCACHE(vol->fs.type)*/)
        vol->readOnly = true;
    else
        vol->readOnly = ( mode != ADF_ACCESS_MODE_READWRITE );
	   	
    struct AdfRootBlock root;
    if ( adfReadRootBlock( vol, (uint32_t) vol->rootBlock, &root ) != ADF_RC_OK ) {
        adfEnv.eFct( "%s: invalid RootBlock, sector %u", __func__, vol->rootBlock );
        vol->mounted = false;
        return NULL;
    }

    ADF_RETCODE rc = adfBitmapAllocate( vol );
    if ( rc != ADF_RC_OK ) {
            adfEnv.eFct( "%s: adfBitmapAllocate() returned error %d, "
                         "mounting volume %s failed", __func__, rc, vol->volName );
            adfVolUnMount( vol );
            return NULL;
    }

    rc = adfReadBitmap( vol, &root );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: adfReadBitmap() returned error %d, "
                     "mounting volume %s failed", __func__, rc, vol->volName );
        adfVolUnMount( vol );
        return NULL;
    }

    /*
    if ( root.bmFlag != ADF_BM_VALID ) {
        if ( vol->readOnly == true ) {
            rc = adfReconstructBitmap ( vol, &root );
            if ( rc != ADF_RC_OK ) {
                adfEnv.eFct ( "adfMount : adfReconstructBitmap() returned error %d, "
                              "mounting volume %s failed", rc, vol->volName );
                adfVolUnMount ( vol );
                return NULL;
            }
        } else {
            adfEnv.eFct ( "adfMount : block allocation bitmap marked invalid in root block, "
                          "mounting the volume %s read-write not possible", vol->volName );
            adfVolUnMount ( vol );
            return NULL;
        }
    }
    */
    if ( root.bmFlag != ADF_BM_VALID )
        adfEnv.wFct( "%s: invalid bitmap on volume '%s'", __func__, vol->volName );

    vol->curDirPtr = vol->rootBlock;

/*printf("blockSize=%d\n",vol->blockSize);*/

    return vol;
}

/*
 * adfVolRemount
 *
 *
 */
ADF_RETCODE adfVolRemount( struct AdfVolume *   vol,
                           const AdfAccessMode  mode )
{
    if ( vol == NULL )
        return ADF_RC_ERROR;

    if ( ! vol->mounted )
        return ADF_RC_ERROR;

    if ( mode == ADF_ACCESS_MODE_READWRITE ) {
        if ( vol->dev->readOnly ) {
            adfEnv.eFct( "%s: device read-only, cannot mount "
                         "volume '%s' read-write", __func__, vol->volName );
            return ADF_RC_ERROR;
        }

        // for read-write - must check filesystem consistency to prevent damage
        //if ( adfVolCheckFilesystem( vol ) != ADF_RC_OK ) {
        //    adfEnv.eFct( "%s: volume '%s': filesystem contains errors, "
        //                 "cannot mount read-write, repair it first",
        //                 __func__, vol->volName );
        //    return ADF_RC_ERROR;
        //}

        vol->readOnly = false;
    } else if ( mode == ADF_ACCESS_MODE_READONLY ) {
        vol->readOnly = true;
    } else {
        adfEnv.eFct( "%s: cannot remount volume %s, invalid mode %d",
                     __func__, vol->volName, mode );
        return ADF_RC_ERROR;
    }
    return ADF_RC_OK;
}

/*
 * adfVolUnMount
 *
 * free bitmap structures
 * free current dir
 */
void adfVolUnMount( struct AdfVolume * const  vol )
{
    if ( ! vol ) {
        adfEnv.eFct( "%s: vol is null", __func__ );
        return;
    }

    adfFreeBitmap( vol );

    vol->mounted = false;
}


/*
 * adfVolCheckFilesystem
 *
 */
static ADF_RETCODE adfVolCheckFilesystem( struct AdfVolume * const  vol )
{
    unsigned nErrors = adfDirCheck( vol, vol->rootBlock, true )
        + ( adfVolBitmapIsMarkedValid( vol ) ? 0 : 1 );

    return ( nErrors == 0 ? ADF_RC_OK : ADF_RC_ERROR );
}


/*
 * adfVolInstallBootBlock
 *
 */
ADF_RETCODE adfVolInstallBootBlock( struct AdfVolume * const  vol,
                                    const uint8_t * const     code )
{
    if ( vol->dev->class != ADF_DEVCLASS_FLOP )
        //&& vol->dev->ype != ADF_DEVTYPE_HDF )   also HDF????
    {
        return ADF_RC_ERROR;
    }

    struct AdfBootBlock boot;
    ADF_RETCODE rc = adfReadBootBlock( vol, &boot );
    if ( rc != ADF_RC_OK )
        return rc;

    boot.rootBlock = 880;
    for ( int i = 0; i < 1024 - 12; i++ )         /* bootcode */
        boot.data[ i ] = code[ i + 12 ];

    rc = adfWriteBootBlock( vol, &boot );
    if ( rc != ADF_RC_OK )
        return rc;

    vol->bootCode = true;

    return ADF_RC_OK;
}

/*
 * adfVolIsSectNumValid
 *
 */
bool adfVolIsSectNumValid( const struct AdfVolume * const  vol,
                           const ADF_SECTNUM               nSect )
{
    return ( nSect >= 0 &&
             nSect <= vol->lastBlock - vol->firstBlock );
}

/*
 * adfVolReadBlock
 *
 * read logical block
 */
ADF_RETCODE adfVolReadBlock( const struct AdfVolume * const  vol,
                             const uint32_t                  nSect,
                             uint8_t * const                 buf )
{
    if ( ! vol->mounted ) {
        adfEnv.eFct( "%s: volume not mounted", __func__ );
        return ADF_RC_ERROR;
    }

    /* translate logical sect to physical sect */
    unsigned pSect = nSect + (unsigned) vol->firstBlock;

    if ( adfEnv.useRWAccess )
        adfEnv.rwhAccess( (ADF_SECTNUM) pSect, (ADF_SECTNUM) nSect, false );

/*  char strBuf[80];
    printf("psect=%ld nsect=%ld\n",pSect,nSect);
    sprintf(strBuf,"ReadBlock : accessing logical block #%ld", nSect);
    adfEnv.vFct(strBuf);
*/
    if ( pSect < (unsigned) vol->firstBlock ||
         pSect > (unsigned) vol->lastBlock )
    {
        adfEnv.wFct( "%s: nSect %u out of range", __func__, nSect );
        return ADF_RC_BLOCKOUTOFRANGE;
    }

    ADF_RETCODE rc = adfDevReadBlock( vol->dev, pSect, 512, buf );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error reading block %d, volume '%s'",
                     __func__, nSect, vol->volName );
    }
    return rc;
}

/*
 * adfVolWriteBlock
 *
 */
ADF_RETCODE adfVolWriteBlock( const struct AdfVolume * const  vol,
                              const uint32_t                  nSect,
                              const uint8_t * const           buf )
{
    if ( ! vol->mounted ) {
        adfEnv.eFct( "%s: volume not mounted", __func__ );
        return ADF_RC_ERROR;
    }

    if ( vol->readOnly ) {
        adfEnv.wFct( "%s: can't write block, read only volume", __func__ );
        return ADF_RC_ERROR;
    }

    unsigned pSect = nSect + (unsigned) vol->firstBlock;
/*printf("write nsect=%ld psect=%ld\n",nSect,pSect);*/

    if ( adfEnv.useRWAccess )
        adfEnv.rwhAccess( (ADF_SECTNUM) pSect, (ADF_SECTNUM) nSect, true );
 
    if ( pSect < (unsigned) vol->firstBlock ||
         pSect > (unsigned) vol->lastBlock )
    {
        adfEnv.wFct( "%s: nSect %u out of range", __func__, nSect );
        return ADF_RC_BLOCKOUTOFRANGE;
    }

    ADF_RETCODE rc = adfDevWriteBlock( vol->dev, pSect, 512, buf );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error writing block %d, volume '%s'",
                     __func__, nSect, vol->volName );
    }
    return rc;
}

/*
 * adfVolGetFsStr
 *
 */
const char * adfVolGetFsStr( const struct AdfVolume * const  vol )
{
    return ( adfVolIsOFS( vol ) ? "OFS" :
             adfVolIsFFS( vol ) ? "FFS" :
             adfVolIsPFS( vol ) ? "PFS" : "???" );
}


/*
 * adfVolGetInfo
 *
 */
char * adfVolGetInfo( struct AdfVolume * const  vol )
{
    struct AdfRootBlock root;
    if ( adfReadRootBlock( vol, (uint32_t) vol->rootBlock, &root ) != ADF_RC_OK )
        return NULL;

    char diskName[ 35 ];
    memset( diskName, 0, 35 );
    memcpy( diskName, root.diskName, root.nameLen );

    const unsigned
        sizeBlocks = (unsigned)( vol->lastBlock -
                                 vol->firstBlock + 1 ),
        freeBlocks = adfCountFreeBlocks( vol );
    int cDays, cMonth, cYear,
        aDays, aMonth, aYear,
        mDays, mMonth, mYear;
    adfDays2Date( root.coDays, &cYear, &cMonth, &cDays );
    adfDays2Date( root.days,   &aYear, &aMonth, &aDays );
    adfDays2Date( root.cDays,  &mYear, &mMonth, &mDays );

    const int volInfoSize = snprintf(
        NULL, 0,
        "\nADF volume info:\n  Name:\t\t%-30s\n"
        "  Type:\t\t%s\n"
        "  Filesystem:\t%s %s %s\n"
        "  Size:\t\t%u blocks (%4.2f MiB)\n"
        "  Free space:\t%u blocks (%4.2f MiB)\n"
        "  R/W:\t\t%s\n"
        "  Created:\t%d/%02d/%02d %d:%02d:%02d\n"
        "  Last access:\t%d/%02d/%02d %d:%02d:%02d"
        "\n\t\t%d/%02d/%02d %d:%02d:%02d\n",
        vol->volName, // diskName ?
        vol->dev->class == ADF_DEVCLASS_FLOP     ? "Floppy disk" :
        vol->dev->class == ADF_DEVCLASS_HARDDISK ? "Hard disk partition" :
        vol->dev->class == ADF_DEVCLASS_HARDFILE ? "HardFile" : "Unknown!",
        adfVolIsFFS( vol ) ? "FFS" : "OFS",
        adfVolHasINTL( vol ) ? "INTL " : "",
        adfVolHasDIRCACHE( vol ) ? "DIRCACHE " : "",
        sizeBlocks, sizeBlocks * 512.0 / ( 1024.0 * 1024.0 ),
        freeBlocks, freeBlocks * 512.0 / ( 1024.0 * 1024.0 ),
        vol->readOnly ? "Read only" : "Read/Write",
        cDays, cMonth, cYear,
        root.coMins / 60,
        root.coMins % 60,
        root.coTicks / 50,
        aDays, aMonth, aYear,
        root.mins / 60,
        root.mins % 60,
        root.ticks / 50,
        mDays, mMonth, mYear,
        root.cMins / 60,
        root.cMins % 60,
        root.cTicks / 50 ) + 1;

    if ( volInfoSize < 0 ) {
        adfEnv.eFct( "%s: snprintf returned an error", __func__);
        return NULL;
    }

    char * const volInfo = malloc( (size_t) volInfoSize );
    if ( volInfo == NULL )
        return NULL;

    snprintf(
        volInfo, (size_t) volInfoSize,
        "\nADF volume info:\n  Name:\t\t%-30s\n"
        "  Type:\t\t%s\n"
        "  Filesystem:\t%s %s %s\n"
        "  Size:\t\t%u blocks (%4.2f MiB)\n"
        "  Free space:\t%u blocks (%4.2f MiB)\n"
        "  R/W:\t\t%s\n"
        "  Created:\t%d/%02d/%02d %d:%02d:%02d\n"
        "  Last access:\t%d/%02d/%02d %d:%02d:%02d"
        "\n\t\t%d/%02d/%02d %d:%02d:%02d\n",
        vol->volName, // diskName ?
        vol->dev->class == ADF_DEVCLASS_FLOP     ? "Floppy disk" :
        vol->dev->class == ADF_DEVCLASS_HARDDISK ? "Hard disk partition" :
        vol->dev->class == ADF_DEVCLASS_HARDFILE ? "HardFile" : "Unknown!",
        adfVolIsFFS( vol ) ? "FFS" : "OFS",
        adfVolHasINTL( vol ) ? "INTL " : "",
        adfVolHasDIRCACHE( vol ) ? "DIRCACHE " : "",
        sizeBlocks, sizeBlocks * 512.0 / ( 1024.0 * 1024.0 ),
        freeBlocks, freeBlocks * 512.0 / ( 1024.0 * 1024.0 ),
        vol->readOnly ? "Read only" : "Read/Write",
        cDays, cMonth, cYear,
        root.coMins / 60,
        root.coMins % 60,
        root.coTicks / 50,
        aDays, aMonth, aYear,
        root.mins / 60,
        root.mins % 60,
        root.ticks / 50,
        mDays, mMonth, mYear,
        root.cMins / 60,
        root.cMins % 60,
        root.cTicks / 50 );

    return volInfo;
}
