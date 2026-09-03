/*
 *  adf_file_util.h - file utility functions
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

#ifndef ADF_FILE_UTIL_H
#define ADF_FILE_UTIL_H

#include "adf_blk.h"
#include "adf_prefix.h"

#include <assert.h>

ADF_PREFIX int32_t adfFilePos2DataBlock( const unsigned    pos,
                                         const unsigned    blockSize,
                                         unsigned * const  posInExtBlk,
                                         unsigned * const  posInDataBlk,
                                         unsigned * const  curDataN );

static inline unsigned adfFilePos2datablockIndex( const unsigned  pos,
                                                  const unsigned  blocksize )
{
    assert( blocksize > 0 );
    return ( pos / blocksize );
}

static inline unsigned adfFileSize2Datablocks( const unsigned  fsize,
                                               const unsigned  blocksize )
{
    assert( blocksize > 0 );
    return ( fsize / blocksize +
             ( ( fsize % blocksize > 0 ) ? 1 : 0 ) );
}

static inline unsigned adfFileDatablocks2Extblocks( const unsigned  ndatablocks )
{
    //return max ( ( ndata_blocks - 1 ) / ADF_MAX_DATABLK, 0 );
    if ( ndatablocks < 1 )
        return 0;
    return ( ndatablocks - 1 ) / ADF_MAX_DATABLK;
}

static inline unsigned adfFileSize2Extblocks( const unsigned   fsize,
                                              const  unsigned  blocksize )
{
    return adfFileDatablocks2Extblocks(
        adfFileSize2Datablocks( fsize, blocksize ) );
}


static inline unsigned adfFileSize2Blocks( const unsigned  fsize,
                                           const unsigned  blocksize )
{
    //assert ( blocksize > 0 );
    const unsigned data_blocks = adfFileSize2Datablocks( fsize, blocksize );
    const unsigned ext_blocks  = adfFileDatablocks2Extblocks( data_blocks );
    return data_blocks + ext_blocks + 1;   // +1 for the file header block
}

ADF_PREFIX uint32_t adfFileRealSize( const uint32_t    size,
                                     const unsigned    blockSize,
                                     uint32_t * const  dataN,
                                     uint32_t * const  extN );

#endif  /* ADF_FILE_UTIL_H */
