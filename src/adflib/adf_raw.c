/*
 *  adf_raw.c - logical disk/volume code
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

#include "adf_raw.h"

#include "adf_byteorder.h"
//#include "adf_debug.h"
#include "adf_env.h"
#include "adf_util.h"

#include <assert.h>
#include <string.h>

#define SW_LONG  4
#define SW_SHORT 2
#define SW_CHAR  1
#define TOT_LEN  0

#define MAX_SWTYPE 11


struct SwapEl {
    int type, len;
};

static const struct SwapEl swapTable[ MAX_SWTYPE + 1 ][ 7 ] = {
    /* first bytes of boot */
    { { SW_CHAR,    4 },
      { SW_LONG,    2 },
      { SW_CHAR, 1012 },
      { TOT_LEN, 1024 }     // total len. (control)
    },

    /* root */
    { { SW_LONG, 108 },
      { SW_CHAR,  40 },
      { SW_LONG,  10 },
      { TOT_LEN, 512 }
    },

    /* data */
    { { SW_LONG,   6 },
      { SW_CHAR, 488 },
      { TOT_LEN, 512 }
    },

    /* file, dir, entry */
    { { SW_LONG,  82 },
      { SW_CHAR,  92 },
      { SW_LONG,   3 },
      { SW_CHAR,  36 },
      { SW_LONG,  11 },
      { TOT_LEN, 512 }
    },

    /* cache */
    { { SW_LONG, 6 },
      { TOT_LEN, 24 }
    },

    /* bitmap, fext */
    { { SW_LONG, 128 },
      { TOT_LEN, 512 }
    },

    /* link */
    { { SW_LONG,   6 },
      { SW_CHAR,  64 },
      { SW_LONG,  86 },
      { SW_CHAR,  32 },
      { SW_LONG,  12 },
      { TOT_LEN, 512 }
    },

    /* RDSK */
    { { SW_CHAR,   4 },
      { SW_LONG,  39 },
      { SW_CHAR,  56 },
      { SW_LONG,  10 },
      { TOT_LEN, 256 }
    },

    /* BADB */
    { { SW_CHAR,   4 },
      { SW_LONG, 127 },
      { TOT_LEN, 512 }
    },

    /* PART */
    { { SW_CHAR,   4 },
      { SW_LONG,   8 },
      { SW_CHAR,  32 },
      { SW_LONG,  31 },
      { SW_CHAR,   4 },
      { SW_LONG,  15 },
      { TOT_LEN, 256 }
    },

    /* FSHD */
    { { SW_CHAR,   4 },
      { SW_LONG,   7 },
      { SW_CHAR,   4 },
      { SW_LONG,  55 },
      { TOT_LEN, 256 }
    },

    /* LSEG */
    { { SW_CHAR,   4 },
      { SW_LONG,   4 },
      { SW_CHAR, 492 },
      { TOT_LEN, 512 }
    }
};


/*
 * adfSwapEndian
 *
 * magic :-) endian swap function (big -> little for read, little to big for write)
 */
void adfSwapEndian( uint8_t * const  buf,
                    const int        type )
{
    assert( type >= 0 );
    assert( type <= MAX_SWTYPE );
    if ( type > MAX_SWTYPE || type < 0 ) {
        /* this should never happen */
        adfEnv.eFct( "%s: type %d do not exist", __func__, type );
        return;
    }

    int i = 0, p = 0;
    for ( ; swapTable[ type ][ i ].type != TOT_LEN; i++ ) {
        const struct SwapEl * const swapEl = &swapTable[ type ][ i ];

        if ( swapEl->type == SW_CHAR ) {
            p += swapEl->len;
            continue;
        }

        for ( int j = 0; j < swapEl->len; j++ )  {
            switch ( swapEl->type ) {
            case SW_LONG:
                swapUint32AtPtr( buf + p );
                p += 4;
                break;
            case SW_SHORT:
                swapUint16AtPtr( buf + p );
                p += 2;
                break;
            default:
                // this should never happen
                adfEnv.eFct("%s: Error: invalid type %s", __func__, swapEl->type );
            }
        }
    }

    if ( p != swapTable[ type ][ i ].len )
        adfEnv.wFct("%s: Warning: Endian Swapping length", __func__ );	/* BV */
}


/*
 * adfReadRootBlock
 *
 * ENDIAN DEPENDENT
 */
ADF_RETCODE adfReadRootBlock( struct AdfVolume * const     vol,
                              const uint32_t               nSect,
                              struct AdfRootBlock * const  root )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];

    const ADF_RETCODE rc = adfVolReadBlock( vol, nSect, buf );
    if ( rc != ADF_RC_OK )
        return rc;

    memcpy( root, buf, ADF_LOGICAL_BLOCK_SIZE );
#ifdef LITT_ENDIAN
    adfSwapEndian( (uint8_t *) root, ADF_SWBL_ROOT );
#endif

    if ( root->type != ADF_T_HEADER ||
         root->secType != ADF_ST_ROOT )
    {
        adfEnv.wFct("%s: id not found", __func__ );
        return ADF_RC_BLOCKTYPE;
    }

    const uint32_t checksumCalculated = adfNormalSum( buf, 0x14, ADF_LOGICAL_BLOCK_SIZE );
    if ( root->checkSum != checksumCalculated ) {
        const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, volume '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct( msg, __func__, root->checkSum, checksumCalculated,
                         nSect, vol->volName );
        } else {
            adfEnv.eFct( msg, __func__, root->checkSum, checksumCalculated,
                         nSect, vol->volName );
            return ADF_RC_BLOCKSUM;
        }
    }
		
    return ADF_RC_OK;
}


/*
 * adfWriteRootBlock
 *
 * 
 */
ADF_RETCODE adfWriteRootBlock( struct AdfVolume * const     vol,
                               const uint32_t               nSect,
                               struct AdfRootBlock * const  root )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE ];

    root->type          = ADF_T_HEADER;
    root->headerKey     = 0L;
    root->highSeq       = 0L;
    root->hashTableSize = ADF_HT_SIZE;
    root->firstData     = 0L;
    /* checkSum, hashTable */
    /* bmflag */
    /* bmPages, bmExt */
    root->nextSameHash  = 0L;
    root->parent        = 0L;
    root->secType       = ADF_ST_ROOT;

    memcpy( buf, root, ADF_LOGICAL_BLOCK_SIZE );

#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_ROOT );
#endif
    const uint32_t newSum = adfNormalSum( buf, 20, ADF_LOGICAL_BLOCK_SIZE );
    swapUint32ToPtr( buf + 20, newSum );
/*	*(uint32_t*)(buf+20) = swapUint32fromPtr((uint8_t*)&newSum);*/
/* 	dumpBlock(buf);*/
    return adfVolWriteBlock( vol, nSect, buf );
}


/*
 * adfReadBootBlock
 *
 * ENDIAN DEPENDENT
 */
ADF_RETCODE adfReadBootBlock( struct AdfVolume * const     vol,
                              struct AdfBootBlock * const  boot )
{
    uint8_t buf[ 1024 ];
    ADF_RETCODE rc = adfVolReadBlock( vol, 0, buf );
    if ( rc != ADF_RC_OK )
        return rc;

    rc = adfVolReadBlock( vol, 1, buf + ADF_LOGICAL_BLOCK_SIZE );
    if ( rc != ADF_RC_OK )
        return rc;

    memcpy( boot, buf, ADF_LOGICAL_BLOCK_SIZE * 2 );
#ifdef LITT_ENDIAN
    adfSwapEndian( (uint8_t *) boot, ADF_SWBL_BOOT );
#endif
    if ( strncmp( "DOS", boot->dosType, 3 ) != 0 ) {
        if ( strncmp( "PFS", boot->dosType, 3 ) == 0 ) {
            adfEnv.wFct( "%s: PFS volume found - not supported...", __func__ );
            return ADF_RC_ERROR;
        }
        adfEnv.wFct( "%s: DOS id not found", __func__ );
        return ADF_RC_ERROR;
    }

    const ADF_SECTNUM rootSectCalculated = adfVolCalcRootBlk( vol );
    if ( boot->rootBlock != rootSectCalculated ) {
        adfEnv.wFct( " %s : non-standard rootBlock value: %u != expected %u.",
                     __func__, boot->rootBlock, rootSectCalculated );
    }

    if ( boot->data[ 0 ] != 0 ) {
        const uint32_t checksumCalculated = adfBootSum( buf );
/*printf("compsum=%lx sum=%lx\n",	adfBootSum(buf),boot->checkSum );*/		/* BV */
        if ( boot->checkSum != checksumCalculated ) {
            const char msg[] = "%s: invalid checksum 0x%x != 0x%x (calculated)"
                ", block %d, volume '%s'";
            if ( adfEnv.ignoreChecksumErrors ) {
                adfEnv.wFct( msg, __func__, boot->checkSum, checksumCalculated,
                             0, vol->volName );
            } else {
                adfEnv.eFct( msg, __func__, boot->checkSum, checksumCalculated,
                             0, vol->volName );
                return ADF_RC_BLOCKSUM;
            }
        }
    }

    return ADF_RC_OK;
}

/*
 * adfWriteBootBlock
 *
 *
 *     write bootcode ?
 */
ADF_RETCODE adfWriteBootBlock( struct AdfVolume * const     vol,
                               struct AdfBootBlock * const  boot )
{
    uint8_t buf[ ADF_LOGICAL_BLOCK_SIZE * 2 ];

    boot->dosType[ 0 ] = 'D';
    boot->dosType[ 1 ] = 'O';
    boot->dosType[ 2 ] = 'S';
    memcpy( buf, boot, ADF_LOGICAL_BLOCK_SIZE * 2 );
#ifdef LITT_ENDIAN
    adfSwapEndian( buf, ADF_SWBL_BOOT );
#endif

    if ( boot->rootBlock == 880 || boot->data[ 0 ] != 0 ) {
        const uint32_t newSum = adfBootSum( buf );
/*fprintf(stderr,"sum %x %x\n",newSum,adfBootSum2(buf));*/
        swapUint32ToPtr( buf + 4, newSum );
/*        *(uint32_t*)(buf+4) = swapUint32fromPtr((uint8_t*)&newSum);*/
    }

/*	dumpBlock(buf);
	dumpBlock(buf+512);
*/

    ADF_RETCODE rc = adfVolWriteBlock( vol, 0, buf );
    if ( rc != ADF_RC_OK )
        return rc;

    rc = adfVolWriteBlock( vol, 1, buf + 512 );
    if (rc != ADF_RC_OK )
        return rc;

    return ADF_RC_OK;
}


/*
 * NormalSum
 *
 * buf = where the block is stored
 * offset = checksum place (in bytes)
 * bufLen = buffer length (in bytes)
 */
uint32_t adfNormalSum( const uint8_t * const  buf,
                       const unsigned         offset,
                       const unsigned         bufLen )
{
    uint32_t newsum = 0L;
    for ( unsigned i = 0; i < bufLen / 4; i++ )
        if ( i != offset / 4 )       /* old chksum */
            newsum += swapUint32fromPtr( buf + i * 4 );
    newsum = (uint32_t) ( - (int32_t) newsum );	/* WARNING */

    return newsum;
}

/*
 * adfBitmapSum
 *
 */
uint32_t adfBitmapSum( const uint8_t * const  buf )
{
    uint32_t newSum = 0L;
    for ( unsigned i = 1; i < 128; i++ )
        newSum -= swapUint32fromPtr( buf + i * 4 );
    return newSum;
}


/*
 * adfBootSum
 *
 */
uint32_t adfBootSum( const uint8_t * const  buf )
{
    uint32_t d, newSum = 0L;
    for ( unsigned i = 0; i < 256; i++ ) {
        if ( i != 1 ) {
            d = swapUint32fromPtr( buf + i * 4 );
            if ( ( 0xffffffffU - newSum ) < d )
                newSum++;
            newSum += d;
        }
    }
    newSum = ~newSum;	/* not */

    return newSum;
}

uint32_t adfBootSum2( const uint8_t * const  buf )
{
    uint32_t prevsum, newSum = 0L;
    for ( unsigned i = 0; i < 1024 / sizeof( uint32_t ) ; i++ ) {
        if ( i != 1 ) {
            prevsum = newSum;
            newSum += swapUint32fromPtr( buf + i * 4 );
            if ( newSum < prevsum )
                newSum++;
        }
    }
    newSum = ~newSum;	/* not */

    return newSum;
}


/*#######################################################################################*/
