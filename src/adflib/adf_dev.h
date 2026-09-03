/*
 *  adf_dev.h - device code
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

#ifndef ADF_DEV_H
#define ADF_DEV_H

#include "adf_types.h"
#include "adf_err.h"
#include "adf_dev_driver.h"
#include "adf_dev_type.h"
#include "adf_prefix.h"
#include "adf_vol.h"


#include <stdio.h>

typedef enum {
    ADF_DEV_RDB_STATUS_UNKNOWN,       // nothing checked
    ADF_DEV_RDB_STATUS_UNREADABLE,    // cannot read device's block 0, can't determine
    ADF_DEV_RDB_STATUS_NOTFOUND,      // device's block 0 does not contain signature "RDSK"
    ADF_DEV_RDB_STATUS_EXIST,         // "RDSK" signature exists in block 0
    ADF_DEV_RDB_STATUS_CHECKSUMERROR, // blocks's checksum is not correct
    ADF_DEV_RDB_STATUS_OK,            // RDSK block is read successfully
    ADF_DEV_RDB_STATUS_SAMEGEOMETRY,  // geometry is the same as read from the device
    //ADF_DEV_RDB_STATUS_VALID        // validity of data in RSDK block was verified
                                      // (not certain if last one is needed...)
} AdfDevRdbStatus;


/* ----- DEVICES ----- */

struct AdfDevice {
    char *         name;
    AdfDevType     type;
    /*
       XCOPY PATCH - see PATCHES.md.

       "class" is a keyword in C++, so this header cannot be included from a C++
       translation unit as upstream writes it - and adflib.h wraps itself in
       extern "C", so being included from C++ is clearly intended.

       The member is spelled devClass to C++ and class to C. Both declarations have
       the same type at the same offset, so the struct is one layout with two names
       for one field: library code (compiled as C) goes on saying dev->class, and
       this firmware says dev->devClass. Renaming it outright would have been a
       26 line patch across eight files; this is the whole of it.
    */
#ifdef __cplusplus
    AdfDevClass    devClass;         // flop / hdf / hdd (with RDB)
#else
    AdfDevClass    class;            // flop / hdf / hdd (with RDB)
#endif
    bool           readOnly;
    uint32_t       sizeBlocks;

    struct RigidDiskBlock {
        AdfDevRdbStatus        status;
        struct AdfRDSKblock *  block;
    }              rdb;

    struct AdfDevGeometry
                   geometry;

    const struct AdfDeviceDriver *
                   drv;

    void *         drvData;          /* driver-specific device data,
                                        (private, use only in the driver code!) */

    bool           mounted;

    // stuff available when mounted
    int            nVol;       /* partitions */

    struct AdfVolume **
                   volList;
};


/*
 * adfDevCreate and adfDevOpen
 *
 * creates or open an ADF device without reading any data (ie. without finding volumes)
 *
 * An created/opened device either has to be mounted (to be used with functions
 * requiring volume data) or only functions using block access on the device
 * level (with adfDevRead/WriteBlock) can be used
 * (ie. this applies to adfCreateFlop/Hd); in general this level of access
 * is for: partitioning / formatting / creating file system data / cloning
 * the whole device on _device_ block level - and similar.
 */

ADF_PREFIX struct AdfDevice * adfDevCreate( const char * const driverName,
                                            const char * const name,
                                            const uint32_t     cylinders,
                                            const uint32_t     heads,
                                            const uint32_t     sectors );

ADF_PREFIX struct AdfDevice * adfDevOpen( const char * const  name,
                                          const AdfAccessMode mode );

/*
 * adfDevOpenWithDriver
 *
 * allows to avoid automatic driver selection done in adfOpenDev and enforce
 * opening a file/device with the driver specified by its name
 * (esp. useful for custom, user-implemented device drivers)
 */
ADF_PREFIX struct AdfDevice * adfDevOpenWithDriver(
    const char * const  driverName,
    const char * const  name,
    const AdfAccessMode mode );

ADF_PREFIX void adfDevClose( struct AdfDevice * const dev );


ADF_PREFIX int adfDevType( const struct AdfDevice * const dev );

ADF_PREFIX ADF_RETCODE adfDevMount( struct AdfDevice * const dev );
ADF_PREFIX void adfDevUnMount( struct AdfDevice * const dev );


ADF_PREFIX ADF_RETCODE adfDevReadBlock( const struct AdfDevice * const  dev,
                                        uint32_t                        pSect,
                                        const uint32_t                  size,
                                        uint8_t * const                 buf );

ADF_PREFIX ADF_RETCODE adfDevWriteBlock( const struct AdfDevice * const  dev,
                                         uint32_t                        pSect,
                                         const uint32_t                  size,
                                         const uint8_t * const           buf );

/*
 * adfDevGetInfo
 *
 * Returns pointer to dynamically allocated char array
 * with text information about the device and its volumes.
 * It must be free()-d afterwards.
 *
 * can be used before adfVolCreate() or adfVolMount()
 */

ADF_PREFIX char * adfDevGetInfo( const struct AdfDevice * const  dev );


static inline bool adfDevIsGeometryValid(
    const struct AdfDevGeometry * const  geometry,
    const uint32_t                       devSizeBlocks )
{
    return ( devSizeBlocks == geometry->cylinders *
                              geometry->heads *
                              geometry->sectors );
}

#endif  /* ADF_DEV_H */
