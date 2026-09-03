/*
 *  adf_file_block.h - Volume block-level code for files
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

#ifndef ADF_FILE_BLOCK_H
#define ADF_FILE_BLOCK_H

#include "adf_blk.h"
#include "adf_err.h"
#include "adf_vector.h"
#include "adf_vol.h"

struct AdfFileBlocks {
    ADF_SECTNUM              header;
    struct AdfVectorSectors  data,
                             extens;
};


ADF_RETCODE adfGetFileBlocks( struct AdfVolume * const                 vol,
                              const struct AdfFileHeaderBlock * const  entry,
                              struct AdfFileBlocks * const             fileBlocks );

ADF_RETCODE adfFreeFileBlocks( struct AdfVolume * const           vol,
                               struct AdfFileHeaderBlock * const  entry );

ADF_RETCODE adfWriteFileHdrBlock( struct AdfVolume * const           vol,
                                  const ADF_SECTNUM                  nSect,
                                  struct AdfFileHeaderBlock * const  fhdr );

ADF_RETCODE adfReadDataBlock( struct AdfVolume * const  vol,
                              const ADF_SECTNUM         nSect,
                              void * const              data );

ADF_RETCODE adfWriteDataBlock( struct AdfVolume * const  vol,
                               const ADF_SECTNUM         nSect,
                               void * const              data );

ADF_PREFIX ADF_RETCODE adfReadFileExtBlock( struct AdfVolume * const        vol,
                                            const ADF_SECTNUM               nSect,
                                            struct AdfFileExtBlock * const  fext );

ADF_PREFIX ADF_RETCODE adfWriteFileExtBlock( struct AdfVolume * const        vol,
                                             const ADF_SECTNUM               nSect,
                                             struct AdfFileExtBlock * const  fext );
#endif  /* ADF_FILE_BLOCK_H */
