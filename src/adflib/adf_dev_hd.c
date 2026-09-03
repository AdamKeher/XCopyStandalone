/*
 *  adf_dev_hd.c - device code / harddisk
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

#include "adf_dev_hd.h"

#include "adf_byteorder.h"
#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "adf_vol.h"

#include <stdlib.h>
#include <string.h>


static void adfFreeTmpVolList( struct AdfList * const  root );


/*
 * adfCreateHd
 *
 * create a filesystem one an harddisk device (partitions==volumes, and the header)
 *
 * fills dev->volList[]
 *
 */
ADF_RETCODE adfCreateHd( struct AdfDevice * const                   dev,
                         const unsigned                             n,
                         const struct AdfPartition * const * const  partList )
{
/*struct AdfVolume *vol;*/

    if ( dev == NULL || partList == NULL ) {
        adfEnv.eFct( "%s: illegal parameter(s)", __func__ );
        return ADF_RC_ERROR;
    }

    dev->class = ADF_DEVCLASS_HARDDISK;

    dev->volList = (struct AdfVolume **) malloc(
        sizeof(struct AdfVolume *) * n );
    if ( ! dev->volList ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }
    for ( unsigned i = 0; i < n; i++ ) {
        dev->volList[ i ] = adfVolCreate( dev,
                                          (uint32_t) partList[ i ]->startCyl,
                                          (uint32_t) partList[ i ]->lenCyl,
                                          partList[ i ]->volName,
                                          partList[ i ]->volType );
        if ( dev->volList[ i ] == NULL ) {
            for ( unsigned j = 0; j < i; j++ ) {
                free ( dev->volList[ j ] );
/* pas fini */
            }
            free( dev->volList );
            adfEnv.eFct( "%s: adfVolCreate() failed", __func__ );
        }
    }
    dev->nVol = (int) n;
/*
vol=dev->volList[0];
printf("0first=%ld last=%ld root=%ld\n",vol->firstBlock,
 vol->lastBlock, vol->rootBlock);
*/
    dev->mounted = true;

    return adfCreateHdHeader( dev, (int) n, partList );
}


/*
 * adfCreateHdHeader
 *
 * create PARTIALLY the sectors of the header of one harddisk : can not be mounted
 * back on a real Amiga ! It's because some device dependant values can't be guessed...
 *
 * do not use dev->volList[], but partList for partitions information : start and len are cylinders,
 *  not blocks
 * do not fill dev->volList[]
 * called by adfCreateHd()
 */
ADF_RETCODE adfCreateHdHeader( struct AdfDevice * const                   dev,
                               const int                                  n,
                               const struct AdfPartition * const * const  partList )
{
    (void)               n;
    struct AdfRDSKblock  rdsk;
    struct AdfPARTblock  part;

    /* RDSK */ 
 
    memset( (uint8_t *) &rdsk, 0, sizeof(struct AdfRDSKblock) );

    rdsk.rdbBlockLo = 0;
    rdsk.rdbBlockHi = ( dev->geometry.sectors *
                        dev->geometry.heads * 2 ) - 1;
    rdsk.loCylinder = 2;
    rdsk.hiCylinder = dev->geometry.cylinders - 1;
    rdsk.cylBlocks  = dev->geometry.sectors *
                      dev->geometry.heads;

    rdsk.cylinders = dev->geometry.cylinders;
    rdsk.sectors   = dev->geometry.sectors;
    rdsk.heads     = dev->geometry.heads;
	
    rdsk.badBlockList   = -1;
    rdsk.partitionList  = 1;
    rdsk.fileSysHdrList = 1 + dev->nVol;

    ADF_RETCODE rc = adfWriteRDSKblock( dev, &rdsk );
    if ( rc != ADF_RC_OK )
        return rc;

    /* PART */

    ADF_SECTNUM j = 1;
    for ( int i = 0; i < dev->nVol; i++ ) {
        memset( &part, 0, sizeof(struct AdfPARTblock) );

        if ( i < dev->nVol - 1 )
            part.next = j + 1;
        else
            part.next = -1;

        const unsigned len = min( (unsigned) ADF_MAX_NAME_LEN,
                                  (unsigned) strlen( partList[ i ]->volName ) );
        part.nameLen = (char) len;
        strncpy( part.name, partList[ i ]->volName, len );

        part.surfaces       = (int32_t) dev->geometry.heads;
        part.blocksPerTrack = (int32_t) dev->geometry.sectors;
        part.lowCyl         = partList[ i ]->startCyl;
        part.highCyl        = partList[ i ]->startCyl +
                              partList[ i ]->lenCyl - 1;
        memcpy( part.dosType, "DOS", 3 );

        part.dosType[ 3 ] = partList[ i ]->volType & 0x01;

        rc = adfWritePARTblock( dev, j, &part );
        if ( rc != ADF_RC_OK )
            return rc;
        j++;
    }

    /* FSHD */
    struct AdfFSHDblock fshd;
    memcpy( fshd.dosType, "DOS", 3 );
    fshd.dosType[ 3 ] = (char) partList[ 0 ]->volType;
    fshd.next         = -1;
    fshd.segListBlock = j + 1;
    rc = adfWriteFSHDblock( dev, j, &fshd );
    if ( rc != ADF_RC_OK )
        return rc;
    j++;
	
    /* LSEG */
    struct AdfLSEGblock lseg;
    lseg.next = -1;

    return adfWriteLSEGblock( dev, j, &lseg );
}


/*
 * adfMountHd
 *
 * normal not used directly : called by adfDevMount()
 *
 * fills geometry fields and volumes list (dev->nVol and dev->volList[])
 */
ADF_RETCODE adfMountHd( struct AdfDevice * const  dev )
{
    struct AdfRDSKblock  rdsk;
    struct AdfPARTblock  part;

    ADF_RETCODE rc = adfReadRDSKblock( dev, &rdsk );
    if ( rc != ADF_RC_OK )
        return rc;

    /* PART blocks */
    struct AdfList
        *vList    = NULL,
        *listRoot = NULL;
    int32_t next  = rdsk.partitionList;
    dev->nVol     = 0;
    while ( next != -1 ) {
        rc = adfReadPARTblock( dev, next, &part );
        if ( rc != ADF_RC_OK ) {
            adfFreeTmpVolList( listRoot );
            adfEnv.eFct( "%s: read PART, block %d, device '%s'",
                         __func__, next, dev->name );
            return rc;
        }

        struct AdfVolume * const vol =
            (struct AdfVolume *) malloc( sizeof(struct AdfVolume) );
        if ( vol == NULL ) {
            adfFreeTmpVolList( listRoot );
            adfEnv.eFct( "%s: malloc", __func__ );
            return ADF_RC_MALLOC;
        }
        vol->dev     = dev;
        vol->volName = NULL;
        dev->nVol++;

        vol->firstBlock = (int32_t) rdsk.cylBlocks * part.lowCyl;
        vol->lastBlock  = ( part.highCyl + 1 ) * (int32_t) rdsk.cylBlocks - 1;
        vol->blockSize  = part.blockSize * 4;

        /* set filesystem info (read from bootblock) */
        struct AdfBootBlock boot;
        rc = adfDevReadBlock( dev, (uint32_t) vol->firstBlock, 512, (uint8_t *) &boot );
        if ( rc != ADF_RC_OK ) {
            adfEnv.eFct( "%s: error reading BootBlock, device %s, volume %d",
                         __func__, dev->name, dev->nVol - 1 );
            adfFreeTmpVolList( listRoot );
            free( vol );
            return rc;
        }
        memcpy( vol->fs.id, boot.dosType, 3 );
        vol->fs.id[ 3 ]    = '\0';
        vol->fs.type       = (uint8_t) boot.dosType[ 3 ];
        vol->datablockSize = adfVolIsOFS( vol ) ? 488 : 512;

        /* set volume name (from partition info) */
        const unsigned len = (unsigned) min( 31, part.nameLen );
        vol->volName = (char *) malloc( len + 1 );
        if ( vol->volName == NULL ) { 
            adfFreeTmpVolList( listRoot );
            free( vol );
            adfEnv.eFct( "%s: malloc", __func__ );
            return ADF_RC_MALLOC;
        }
        memcpy( vol->volName, part.name, len );
        vol->volName[ len ] = '\0';

        vol->mounted = false;

        /* stores temporaly the volumes in a linked list */
        if ( listRoot == NULL )
            vList = listRoot = adfListNewCell( NULL, (void *) vol );
        else
            vList = adfListNewCell( vList, (void *) vol );

        if ( vList == NULL ) {
            adfFreeTmpVolList( listRoot );
            adfEnv.eFct( "%s: adfListNewCell() malloc", __func__ );
            return ADF_RC_MALLOC;
        }

        vol->rootBlock = ( adfVolIsDosFS( vol ) ? adfVolCalcRootBlk( vol ) : -1 );

        next = part.next;
    }

    /* stores the list in an array */
    dev->volList = (struct AdfVolume **) malloc(
        sizeof(struct AdfVolume *) * (unsigned) dev->nVol );
    if ( dev->volList == NULL ) {
        adfFreeTmpVolList( listRoot );
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }
    vList = listRoot;
    for( int i = 0; i < dev->nVol; i++ ) {
        dev->volList[ i ] = (struct AdfVolume *) vList->content;
        vList = vList->next;
    }
    adfListFree( listRoot );

    /* The code below seems to only check if the FSHD and LSEG blocks can be
       read. These blocks are not required to access partitions/volumes:
       http://lclevy.free.fr/adflib/adf_info.html#p64 */

    struct AdfFSHDblock fshd;
    fshd.segListBlock = -1;

    next = rdsk.fileSysHdrList;
    while ( next != -1 ) {
        rc = adfReadFSHDblock( dev, next, &fshd );
        if ( rc != ADF_RC_OK ) {
            /*
            for ( i = 0 ; i < dev->nVol ; i++ )
                free ( dev->volList[i] );
            free(dev->volList); */
            adfEnv.wFct( "%s: adfReadFSHDblock error, device %s, sector %d",
                         __func__, dev->name, next );
            //return rc;
            break;
        }
        next = fshd.next;
    }

    struct AdfLSEGblock lseg;
    next = fshd.segListBlock;
    while ( next != -1 ) {
        rc = adfReadLSEGblock( dev, next, &lseg );
        if ( rc != ADF_RC_OK ) {
            /*for ( i = 0 ; i < dev->nVol ; i++ )
                free ( dev->volList[i] );
            free(dev->volList); */
            adfEnv.wFct( "%s: adfReadLSEGblock error, device %s, sector %s",
                         __func__, dev->name, next );
            //return rc;
            break;
        }
        next = lseg.next;
    }

    return ADF_RC_OK;
}


/*
 * ReadRDSKblock
 *
 */
ADF_RETCODE adfReadRDSKblock( const struct AdfDevice * const  dev,
                              struct AdfRDSKblock * const     blk )
{
    uint8_t buf[ 256 ];

    ADF_RETCODE rc = adfDevReadBlock( dev, 0, 256, buf );
    if ( rc != ADF_RC_OK )
       return rc;

    memcpy( blk, buf, 256 );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian( (uint8_t *) blk, ADF_SWBL_RDSK );
#endif

    if ( strncmp( blk->id, "RDSK", 4 ) != 0 ) {
        adfEnv.eFct( "%s: RDSK id not found", __func__ );
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
        adfEnv.wFct( "%s: size != 64", __func__ );			/* BV */

    const uint32_t checksumCalculated = adfNormalSum( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, blk->checksum, checksumCalculated,
                         0, dev->name );
        } else {
            adfEnv.eFct( msg, __func__, blk->checksum, checksumCalculated,
                         0, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }
	
    if ( blk->blockSize != 512 )
        adfEnv.wFct("%s: blockSize != 512", __func__ );		/* BV */

    if ( blk->cylBlocks != blk->sectors * blk->heads )
        adfEnv.wFct( "%s: cylBlocks != sectors * heads", __func__ );

    return rc;
}


/*
 * adfWriteRDSKblock
 *
 */
ADF_RETCODE adfWriteRDSKblock( const struct AdfDevice * const  dev,
                               struct AdfRDSKblock * const     rdsk )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];

    if ( dev->readOnly ) {
        adfEnv.wFct( "%s: can't write block, read only device", __func__ );
        return ADF_RC_ERROR;
    }

    memset( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy( rdsk->id, "RDSK", 4 );
    rdsk->size         = sizeof(struct AdfRDSKblock) / sizeof(int32_t);
    rdsk->blockSize    = ADF_LOGICAL_BLOCK_SIZE;
    rdsk->badBlockList = -1;

    memcpy( rdsk->diskVendor, "ADFlib  ", 8 );
    memcpy( rdsk->diskProduct, "harddisk.adf    ", 16 );
    memcpy( rdsk->diskRevision, "v1.0", 4 );

    memcpy( buf, rdsk, sizeof(struct AdfRDSKblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_RDSK );
#endif

    const uint32_t newSum = adfNormalSum( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swapUint32ToPtr( buf + 8, newSum );

    return adfDevWriteBlock( dev, 0, ADF_LOGICAL_BLOCK_SIZE, buf );
}


/*
 * ReadPARTblock
 *
 */
ADF_RETCODE adfReadPARTblock( const struct AdfDevice * const  dev,
                              const int32_t                   nSect,
                              struct AdfPARTblock * const     blk )
{
    uint8_t buf[ sizeof(struct AdfPARTblock) ];

    ADF_RETCODE rc = adfDevReadBlock( dev, (uint32_t) nSect,
                                      sizeof(struct AdfPARTblock), buf );
    if ( rc != ADF_RC_OK )
       return rc;

    memcpy( blk, buf, sizeof(struct AdfPARTblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian( (uint8_t *) blk, ADF_SWBL_PART);
#endif

    if ( strncmp( blk->id, "PART", 4 ) != 0 ) {
        adfEnv.eFct( "%s: PART id not found", __func__ );
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
        adfEnv.wFct( "%s: size != 64", __func__ );

    if ( blk->blockSize != 128 ) {
        adfEnv.eFct( "%s: blockSize!=512, not supported (yet)", __func__ );
        return ADF_RC_ERROR;
    }

    const uint32_t checksumCalculated = adfNormalSum( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
        } else {
            adfEnv.eFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }

    return rc;
}


/*
 * adfWritePARTblock
 *
 */
ADF_RETCODE adfWritePARTblock ( const struct AdfDevice * const  dev,
                                const int32_t                   nSect,
                                struct AdfPARTblock * const     part )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];
	
    if ( dev->readOnly ) {
        adfEnv.wFct( "%s: can't write block, read only device", __func__ );
        return ADF_RC_ERROR;
    }

    memset( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy( part->id, "PART", 4 );
    part->size            = sizeof(struct AdfPARTblock) / sizeof(int32_t);
    part->blockSize       = ADF_LOGICAL_BLOCK_SIZE;
    part->vectorSize      = 16;
    part->blockSize       = 128;
    part->sectorsPerBlock = 1;
    part->dosReserved     = 2;

    memcpy( buf, part, sizeof(struct AdfPARTblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_PART );
#endif

    const uint32_t newSum = adfNormalSum( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swapUint32ToPtr( buf + 8, newSum );
/*    *(int32_t*)(buf+8) = swapUint32fromPtr((uint8_t*)&newSum);*/

    return adfDevWriteBlock( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}

/*
 * ReadFSHDblock
 *
 */
ADF_RETCODE adfReadFSHDblock( const struct AdfDevice * const  dev,
                              const int32_t                   nSect,
                              struct AdfFSHDblock * const     blk )
{
    uint8_t buf[ sizeof(struct AdfFSHDblock) ];

    ADF_RETCODE rc = adfDevReadBlock( dev, (uint32_t) nSect,
                                      sizeof(struct AdfFSHDblock), buf );
    if ( rc != ADF_RC_OK )
        return rc;
		
    memcpy( blk, buf, sizeof(struct AdfFSHDblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian( (uint8_t *) blk, ADF_SWBL_FSHD );
#endif

    if ( strncmp( blk->id, "FSHD", 4 ) != 0 ) {
        adfEnv.eFct( "%s: FSHD id not found", __func__ );
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
        adfEnv.wFct( "%s: size != 64", __func__ );

    const uint32_t checksumCalculated = adfNormalSum( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
        } else {
            adfEnv.eFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }
    return rc;
}


/*
 *  adfWriteFSHDblock
 *
 */
ADF_RETCODE adfWriteFSHDblock( const struct AdfDevice * const  dev,
                               const int32_t                   nSect,
                               struct AdfFSHDblock * const     fshd )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];

    if ( dev->readOnly ) {
        adfEnv.wFct( "%s: can't write block, read only device", __func__ );
        return ADF_RC_ERROR;
    }

    memset( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy( fshd->id, "FSHD", 4 );
    fshd->size = sizeof(struct AdfFSHDblock) / sizeof(int32_t);

    memcpy( buf, fshd, sizeof(struct AdfFSHDblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_FSHD );
#endif

    uint32_t newSum = adfNormalSum( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swapUint32ToPtr( buf + 8, newSum );
/*    *(int32_t*)(buf+8) = swapUint32fromPtr((uint8_t*)&newSum);*/

    return adfDevWriteBlock( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}


/*
 * ReadLSEGblock
 *
 */
ADF_RETCODE adfReadLSEGblock( const struct AdfDevice * const  dev,
                              const int32_t                   nSect,
                              struct AdfLSEGblock * const     blk )
{
    uint8_t buf[ sizeof(struct AdfLSEGblock) ];

    ADF_RETCODE rc = adfDevReadBlock( dev, (uint32_t) nSect,
                                      sizeof(struct AdfLSEGblock), buf );
    if ( rc != ADF_RC_OK )
        return rc;
		
    memcpy( blk, buf, sizeof(struct AdfLSEGblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian( (uint8_t *) blk, ADF_SWBL_LSEG );
#endif

    if ( strncmp( blk->id, "LSEG", 4 ) !=0 ) {
        adfEnv.eFct( "%s: LSEG id not found", __func__ );
        return ADF_RC_ERROR;
    }

    const uint32_t checksumCalculated = adfNormalSum( buf, 8, sizeof(struct AdfLSEGblock) );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
        } else {
            adfEnv.eFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }

    if ( blk->next != -1 && blk->size != 128 )
        adfEnv.wFct( "%s: size != 128", __func__ );

    return ADF_RC_OK;
}


/*
 * adfWriteLSEGblock
 *
 */
ADF_RETCODE adfWriteLSEGblock( const struct AdfDevice * const  dev,
                               const int32_t                   nSect,
                               struct AdfLSEGblock * const     lseg )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];

    if ( dev->readOnly ) {
        adfEnv.wFct( "%s: can't write block, read only device", __func__ );
        return ADF_RC_ERROR;
    }

    memset( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy( lseg->id, "LSEG", 4 );
    lseg->size = sizeof(struct AdfLSEGblock) / sizeof(int32_t);

    memcpy( buf, lseg, sizeof(struct AdfLSEGblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_LSEG );
#endif

    uint32_t newSum = adfNormalSum( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swapUint32ToPtr( buf + 8, newSum );
/*    *(int32_t*)(buf+8) = swapUint32fromPtr((uint8_t*)&newSum);*/

    return adfDevWriteBlock( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}


/*
 * adfReadBADBblock
 *
 */
ADF_RETCODE adfReadBADBblock( const struct AdfDevice * const  dev,
                              const int32_t                   nSect,
                              struct AdfBADBblock * const     blk )
{
    uint8_t buf[ sizeof(struct AdfBADBblock) ];

    ADF_RETCODE rc = adfDevReadBlock( dev, (uint32_t) nSect,
                                      sizeof(struct AdfBADBblock), buf );
    if ( rc != ADF_RC_OK )
        return rc;

    memcpy( blk, buf, sizeof(struct AdfBADBblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( (uint8_t *) blk, ADF_SWBL_BADB );
#endif

    if ( strncmp( blk->id, "BADB", 4 ) != 0 ) {
        adfEnv.eFct( "%s: BADB id not found", __func__ );
        return ADF_RC_ERROR;
    }

    const uint32_t checksumCalculated = adfNormalSum( buf, 8, sizeof(struct AdfBADBblock) );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
        } else {
            adfEnv.eFct( msg, __func__, blk->checksum, checksumCalculated,
                         nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }

    // copied from adfReadLSEGblock - to check/update for BADB
    //if ( blk->next != -1 && blk->size != 128 )
    //    adfEnv.wFct( "%s: size != 128", __func__ );

    return ADF_RC_OK;
}


/*
 * adfWriteBADBblock
 *
 */
ADF_RETCODE adfWriteBADBblock( const struct AdfDevice * const  dev,
                               const int32_t                   nSect,
                               struct AdfBADBblock * const     blk )
{
    (void) dev, (void) nSect, (void) blk;
    adfEnv.eFct( "%s: not implemented", __func__ );
    return ADF_RC_ERROR;
}

/*##########################################################################*/

/*
 * adfFreeTmpVolList
 *
 */
static void adfFreeTmpVolList( struct AdfList * const  root )
{
    struct AdfList *cell;
    struct AdfVolume *vol;

    cell = root;
    while ( cell != NULL ) {
        vol = (struct AdfVolume *) cell->content;
        if ( vol->volName != NULL )
            free( vol->volName );
        cell = cell->next;
    }
    adfListFree( root );
}
