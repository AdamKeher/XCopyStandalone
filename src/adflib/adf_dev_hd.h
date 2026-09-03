/*
 *  adf_dev_hd.h - device code / harddisk
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

#ifndef ADF_DEV_HD_H
#define ADF_DEV_HD_H

#include "adf_dev.h"
#include "adf_err.h"
#include "adf_blk_hd.h"
#include "adf_prefix.h"


struct AdfPartition {
    int32_t  startCyl;
    int32_t  lenCyl;
    char *   volName;
    uint8_t  volType;
};


/* create and format hd volumes (erases data!) */
ADF_PREFIX ADF_RETCODE adfCreateHd(
    struct AdfDevice * const                   dev,
    const unsigned                             n,
    const struct AdfPartition * const * const  partList );

ADF_RETCODE adfCreateHdHeader(
    struct AdfDevice * const                   dev,
    const int                                  n,
    const struct AdfPartition * const * const  partList );

/* mount hd volumes (called by adfDevMount) */
ADF_RETCODE adfMountHd( struct AdfDevice * const  dev );


/* hd block operations */

ADF_PREFIX ADF_RETCODE adfReadRDSKblock( const struct AdfDevice * const  dev,
                                         struct AdfRDSKblock * const     blk );

ADF_PREFIX ADF_RETCODE adfWriteRDSKblock( const struct AdfDevice * const  dev,
                                          struct AdfRDSKblock * const     rdsk );

ADF_PREFIX ADF_RETCODE adfReadPARTblock( const struct AdfDevice * const  dev,
                                         const int32_t                   nSect,
                                         struct AdfPARTblock * const     blk );

ADF_PREFIX ADF_RETCODE adfWritePARTblock( const struct AdfDevice * const  dev,
                                          const int32_t                   nSect,
                                          struct AdfPARTblock * const     part );

ADF_PREFIX ADF_RETCODE adfReadFSHDblock( const struct AdfDevice * const  dev,
                                         const int32_t                   nSect,
                                         struct AdfFSHDblock * const     blk );

ADF_PREFIX ADF_RETCODE adfWriteFSHDblock( const struct AdfDevice * const  dev,
                                          const int32_t                   nSect,
                                          struct AdfFSHDblock * const     fshd );

ADF_PREFIX ADF_RETCODE adfReadLSEGblock( const struct AdfDevice * const  dev,
                                         const int32_t                   nSect,
                                         struct AdfLSEGblock * const     blk );

ADF_PREFIX ADF_RETCODE adfWriteLSEGblock( const struct AdfDevice * const  dev,
                                          const int32_t                   nSect,
                                          struct AdfLSEGblock * const     lseg );

ADF_PREFIX ADF_RETCODE adfReadBADBblock( const struct AdfDevice * const  dev,
                                         const int32_t                   nSect,
                                         struct AdfBADBblock * const     blk );

ADF_PREFIX ADF_RETCODE adfWriteBADBblock( const struct AdfDevice * const  dev,
                                          const int32_t                   nSect,
                                          struct AdfBADBblock * const     blk );

#endif /* ADF_DEV_HD_H */
