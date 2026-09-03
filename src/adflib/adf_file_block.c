/*
 *  adf_file_block.c - Volume block-level code for files
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

#include "adf_file_block.h"

#include "adf_bitm.h"
#include "adf_byteorder.h"
#include "adf_env.h"
#include "adf_file_util.h"
#include "adf_raw.h"
#include "adf_util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


/*
 * adfGetFileBlocks
 *
 */
ADF_RETCODE adfGetFileBlocks( struct AdfVolume * const                 vol,
                              const struct AdfFileHeaderBlock * const  entry,
                              struct AdfFileBlocks * const             fileBlocks )
{
    ADF_RETCODE status = ADF_RC_OK;

    memset( fileBlocks, 0, sizeof(struct AdfFileBlocks) );
    fileBlocks->header          = entry->headerKey;
    //fileBlocks->data.sectors    = NULL;  -> memset
    //fileBlocks->extens.sectors  = NULL;  -> memset
    fileBlocks->data              = adfVectorSectorsCreate( 0 );
    fileBlocks->extens            = adfVectorSectorsCreate( 0 );

    uint32_t dataItemsNum, extensItemsNum;
#ifndef NDEBUG
    uint32_t sizeInBlocks =
#endif
        adfFileRealSize( entry->byteSize, vol->datablockSize,
                         &dataItemsNum, &extensItemsNum );
    assert( sizeInBlocks == dataItemsNum + extensItemsNum + 1 );  // +1 for file header block

    const int32_t dblocksInHeader = min( dataItemsNum,
                                         (unsigned) ADF_MAX_DATABLK );
    if ( dblocksInHeader != entry->highSeq ) {
        adfEnv.eFct( "%s: inconsistent data in the File Header block "
                     "0x%x (%d, file name: %s), "
                     "highSeq value should be the number of data blocks in the file header, "
                     "while:  calculated (expected) %d != file header highSeq %d",
                     __func__, entry->headerKey, entry->headerKey, entry->fileName,
                     dblocksInHeader, entry->highSeq );
        return ADF_RC_ERROR;
    }

    /* add data blocks from file header block */
    fileBlocks->data = adfVectorSectorsCreate( dataItemsNum );
    if ( fileBlocks->data.sectors == NULL ) {
        adfEnv.eFct( "%s: malloc", __func__ );
        return ADF_RC_MALLOC;
    }

    uint32_t nDataBlocks = 0,
             nExtBlocks  = 0;
    for ( int i = 0; i < entry->highSeq ; i++ ) {
        fileBlocks->data.sectors[ nDataBlocks++ ] =
            entry->dataBlocks[ ADF_MAX_DATABLK - 1 - i ];
    }

    if ( extensItemsNum > 0 ) {
        /* add file extension blocks and data blocks indexed in them */

        fileBlocks->extens = adfVectorSectorsCreate( extensItemsNum );
        if ( fileBlocks->extens.sectors == NULL ) {
            adfEnv.eFct( "%s: malloc", __func__ );
            status = ADF_RC_MALLOC;
            goto adfGetFileBlocks_error;
        }

        ADF_SECTNUM nSect = entry->extension;
        struct AdfFileExtBlock extBlock;
        while( nSect != 0 ) {
            fileBlocks->extens.sectors[ nExtBlocks++ ] = nSect;
            ADF_RETCODE rc = adfReadFileExtBlock( vol, nSect, &extBlock );
            if ( rc != ADF_RC_OK ) {
                status = rc;
                goto adfGetFileBlocks_error;
            }
            for ( int i = 0 ; i < extBlock.highSeq ; i++ ) {
                fileBlocks->data.sectors[ nDataBlocks++ ] =
                    extBlock.dataBlocks[ ADF_MAX_DATABLK - 1 - i ];
            }
            nSect = extBlock.extension;
        }
    }

    if ( nDataBlocks != fileBlocks->data.nItems ) {
        adfEnv.eFct( "%s: invalid number of data blocks: "
                     "expected %d != retrieved %d", __func__,
                     fileBlocks->data.nItems, nDataBlocks );
        status = ADF_RC_ERROR;
        goto adfGetFileBlocks_error;
    }

    if ( nExtBlocks != fileBlocks->extens.nItems ) {
        adfEnv.eFct( "%s: invalid number of ext. blocks: "
                     "expected %d != retrieved %d", __func__,
                     fileBlocks->extens.nItems, nExtBlocks );
        status = ADF_RC_ERROR;
        goto adfGetFileBlocks_error;
    }

    return ADF_RC_OK;

adfGetFileBlocks_error:
    fileBlocks->extens.destroy( &fileBlocks->extens );
    fileBlocks->data.destroy( &fileBlocks->data );
    return status;
}

/*
 * adfFreeFileBlocks
 *
 */
ADF_RETCODE adfFreeFileBlocks( struct AdfVolume * const           vol,
                               struct AdfFileHeaderBlock * const  entry )
{
    struct AdfFileBlocks fileBlocks;
    ADF_RETCODE rc = adfGetFileBlocks( vol, entry, &fileBlocks );
    if ( rc != ADF_RC_OK )
        return rc;

    for ( unsigned i = 0 ; i < fileBlocks.data.nItems ; i++ ) {
        adfSetBlockFree( vol, fileBlocks.data.sectors[ i ] );
    }
    for ( unsigned i = 0; i < fileBlocks.extens.nItems ; i++ ) {
        adfSetBlockFree( vol, fileBlocks.extens.sectors[ i ] );
    }

    fileBlocks.data.destroy( &fileBlocks.data );
    fileBlocks.extens.destroy( &fileBlocks.extens );
		
    return rc;
}


/*
 * adfWriteFileHdrBlock
 *
 */
ADF_RETCODE adfWriteFileHdrBlock( struct AdfVolume * const           vol,
                                  const ADF_SECTNUM                  nSect,
                                  struct AdfFileHeaderBlock * const  fhdr )
{
    uint8_t  buf[ 512] ;
    uint32_t newSum;
/*printf("adfWriteFileHdrBlock %ld\n",nSect);*/
    fhdr->type     = ADF_T_HEADER;
    fhdr->dataSize = 0;
    fhdr->secType  = ADF_ST_FILE;

    memcpy( buf, fhdr, sizeof(struct AdfFileHeaderBlock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_FILE );
#endif
    newSum = adfNormalSum( buf, 20, sizeof(struct AdfFileHeaderBlock) );
    swapUint32ToPtr( buf + 20, newSum );
/*    *(uint32_t*)(buf+20) = swapUint32fromPtr((uint8_t*)&newSum);*/

    return adfVolWriteBlock( vol, (uint32_t) nSect, buf );
}



/*
 * adfReadDataBlock
 *
 */
ADF_RETCODE adfReadDataBlock( struct AdfVolume * const  vol,
                              const ADF_SECTNUM         nSect,
                              void * const              data )
{
    if ( nSect < 1 ) {
        // ie. block 0 is volume's bootblock - cannot be a data block
        adfEnv.eFct( "%s: error, '%d' cannot be a data block", __func__, nSect );
        return ADF_RC_ERROR;
    }

    uint8_t buf[ 512 ];

    ADF_RETCODE rc = adfVolReadBlock( vol, (uint32_t) nSect, buf );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error reading block %d, volume '%s'",
                     __func__, nSect, vol->volName );
        return rc;
    }

    memcpy( data, buf, 512 );

    if ( adfVolIsOFS( vol ) ) {
#ifdef LITT_ENDIAN
        adfSwapEndian( data, ADF_SWBL_DATA );
#endif
        struct AdfOFSDataBlock * const dBlock = (struct AdfOFSDataBlock *) data;
/*printf("adfReadDataBlock %ld\n",nSect);*/

        const uint32_t checksumCalculated =
            adfNormalSum( buf, 20, sizeof(struct AdfOFSDataBlock) );
        if ( dBlock->checkSum != checksumCalculated ) {
            const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
                ", block %d, volume '%s'";
            if ( adfEnv.ignoreChecksumErrors ) {
                adfEnv.wFct( msg, __func__, dBlock->checkSum, checksumCalculated,
                             nSect, vol->volName );
            } else {
                adfEnv.eFct( msg, __func__, dBlock->checkSum, checksumCalculated,
                             nSect, vol->volName );
                return ADF_RC_BLOCKSUM;
            }
        }

        if ( dBlock->type != ADF_T_DATA )
            adfEnv.wFct( "%s: id ADF_T_DATA not found, block %d, volume '%s'",
                         __func__, nSect, vol->volName );

        if ( dBlock->dataSize > 488 )
            adfEnv.wFct( "%s: dataSize (0x%x / %u) incorrect, block %d, volume '%s'",
                         __func__, dBlock->dataSize, dBlock->dataSize, nSect, vol->volName );

        if ( ! adfVolIsSectNumValid( vol, dBlock->headerKey ) )
            adfEnv.wFct( "%s: headerKey (0x%x / %u) out of range, block %d, volume '%s'",
                         __func__, dBlock->headerKey, dBlock->headerKey, nSect, vol->volName );

        if ( ! adfVolIsSectNumValid( vol, dBlock->nextData ) )
            adfEnv.wFct( "%s: nextData out of range, block %d, volume '%s'",
                         __func__, nSect, vol->volName );
    }

    return rc;
}


/*
 * adfWriteDataBlock
 *
 */
ADF_RETCODE adfWriteDataBlock( struct AdfVolume * const  vol,
                               const ADF_SECTNUM         nSect,
                               void * const              data )
{
    if ( nSect < 1 ) {
        // ie. block 0 is volume's bootblock - cannot be a data block
        adfEnv.eFct( "%s: error, '%d' cannot be a data block", __func__, nSect );
        return ADF_RC_ERROR;
    }

    ADF_RETCODE rc;
    if ( adfVolIsOFS( vol ) ) {
        struct AdfOFSDataBlock * const dataB = (struct AdfOFSDataBlock *) data;
        dataB->type = ADF_T_DATA;

        uint8_t buf[ 512 ];
        memcpy( buf, data, 512 );
#ifdef LITT_ENDIAN
        adfSwapEndian( buf, ADF_SWBL_DATA );
#endif
        const uint32_t newSum = adfNormalSum( buf, 20, 512 );
        swapUint32ToPtr( buf + 20, newSum );
/*        *(int32_t*)(buf+20) = swapUint32fromPtr((uint8_t*)&newSum);*/
        rc = adfVolWriteBlock( vol, (uint32_t) nSect, buf );
    } else {
        rc = adfVolWriteBlock( vol, (uint32_t) nSect, data );
    }
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error writing block %d, volume '%s'",
                     __func__, nSect, vol->volName );
    }
/*printf("adfWriteDataBlock %ld\n",nSect);*/

    return rc;
}


/*
 * adfReadFileExtBlock
 *
 */
ADF_RETCODE adfReadFileExtBlock( struct AdfVolume * const        vol,
                                 const ADF_SECTNUM               nSect,
                                 struct AdfFileExtBlock * const  fext )
{
    uint8_t buf[ sizeof(struct AdfFileExtBlock) ];
    ADF_RETCODE rc = adfVolReadBlock( vol, (uint32_t) nSect, buf );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error reading block %d, volume '%s'",
                     __func__, nSect, vol->volName );
        return rc;
    }
/*printf("read fext=%d\n",nSect);*/
    memcpy( fext, buf, sizeof(struct AdfFileExtBlock) );
#ifdef LITT_ENDIAN
    adfSwapEndian( (uint8_t *) fext, ADF_SWBL_FEXT );
#endif

    const uint32_t checksumCalculated =
        adfNormalSum( buf, 20, sizeof(struct AdfFileExtBlock) );
    if ( fext->checkSum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, volume '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, fext->checkSum, checksumCalculated,
                         nSect, vol->volName );
        } else {
            adfEnv.eFct( msg, __func__, fext->checkSum, checksumCalculated,
                         nSect, vol->volName );
            return ADF_RC_BLOCKSUM;
        }
    }

    if ( fext->type != ADF_T_LIST )
        adfEnv.wFct( "%s: type ADF_T_LIST not found", __func__ );

    if ( fext->secType != ADF_ST_FILE )
        adfEnv.wFct( "%s: sectype ADF_ST_FILE not found", __func__ );

    if ( fext->headerKey != nSect )
        adfEnv.wFct( "%s: headerKey!=nSect", __func__ );

    if ( fext->highSeq < 0 ||
         fext->highSeq > ADF_MAX_DATABLK )
    {
        adfEnv.wFct("%s: highSeq out of range", __func__ );
    }

    if ( ! adfVolIsSectNumValid( vol, fext->parent ) )
        adfEnv.wFct( "%s: parent out of range", __func__ );

    if ( fext->extension != 0 &&
         ! adfVolIsSectNumValid( vol, fext->extension ) )
    {
        adfEnv.wFct( "%s: extension out of range", __func__ );
    }

    return rc;
}


/*
 * adfWriteFileExtBlock
 *
 */
ADF_RETCODE adfWriteFileExtBlock( struct AdfVolume * const        vol,
                                  const ADF_SECTNUM               nSect,
                                  struct AdfFileExtBlock * const  fext )
{
    uint8_t buf[ 512 ];

    fext->type      = ADF_T_LIST;
    fext->secType   = ADF_ST_FILE;
    fext->dataSize  = 0L;
    fext->firstData = 0L;

    memcpy( buf, fext, 512 );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_FEXT );
#endif
    const uint32_t newSum = adfNormalSum( buf, 20, 512 );
    swapUint32ToPtr( buf + 20, newSum );
/*    *(int32_t*)(buf+20) = swapUint32fromPtr((uint8_t*)&newSum);*/

    ADF_RETCODE rc = adfVolWriteBlock( vol, (uint32_t) nSect, buf );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct( "%s: error wriding block %d, volume '%s'",
                     __func__, nSect, vol->volName );
    }

    return rc;
}
