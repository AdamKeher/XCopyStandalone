/*
 *  adf_dev_type.h - device type
 *
 *  Copyright (C) 2023-2026 Tomasz Wolak
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

#ifndef ADF_DEV_TYPE_H
#define ADF_DEV_TYPE_H

#include "adf_prefix.h"

#include <stdint.h>

typedef enum {
    ADF_DEVCLASS_UNKNOWN,
    ADF_DEVCLASS_FLOP,
    ADF_DEVCLASS_HARDDISK,
    ADF_DEVCLASS_HARDFILE
} AdfDevClass;

typedef enum {
    ADF_DEVTYPE_UNKNOWN,

    ADF_DEVTYPE_FDD,

    ADF_DEVTYPE_FSD,
    ADF_DEVTYPE_FHD1520,
    ADF_DEVTYPE_FHD,   // 1760

    ADF_DEVTYPE_FDD81,
    ADF_DEVTYPE_FDD82,
    ADF_DEVTYPE_FDD83,

    ADF_DEVTYPE_FHD81,
    ADF_DEVTYPE_FHD82,
    ADF_DEVTYPE_FHD83,

    ADF_DEVTYPE_FPC360,
    ADF_DEVTYPE_FPC1200,
    ADF_DEVTYPE_FPC720,
    ADF_DEVTYPE_FPC1440,
    ADF_DEVTYPE_FPC2880,

    ADF_DEVTYPE_ADF,
    ADF_DEVTYPE_HDF,
    ADF_DEVTYPE_HD,    
    
    ADF_DEVTYPE_HD_ZIPDISK,

    ADF_DEVTYPE_HD_A590_WD93028X,
    ADF_DEVTYPE_HD_WDAC280,

    ADF_DEVTYPE_NUMTYPES
} AdfDevType;


struct AdfDevGeometry {
    unsigned  cylinders,
              heads,
              sectors,
              blockSize;   // always 512?
};


ADF_PREFIX AdfDevClass adfDevTypeGetClass( AdfDevType type );
ADF_PREFIX struct AdfDevGeometry adfDevTypeGetGeometry( AdfDevType type );
ADF_PREFIX const char * adfDevTypeGetDescription( AdfDevType type );

ADF_PREFIX AdfDevType adfDevGetTypeByGeometry(
    const struct AdfDevGeometry * const geometry );
ADF_PREFIX AdfDevType adfDevGetTypeBySizeBlocks( uint32_t sizeBlocks );
ADF_PREFIX AdfDevClass adfDevGetClassBySizeBlocks( uint32_t sizeBlocks );

#endif
