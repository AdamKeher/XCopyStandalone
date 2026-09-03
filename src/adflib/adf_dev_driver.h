/*
 *  adf_dev_driver.h - device driver interface
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

#ifndef ADF_DEV_DRIVER_H
#define ADF_DEV_DRIVER_H

#include "adf_dev.h"

struct AdfDeviceDriver {
    const char * const name;    /* driver name / id */
    void *             data;    /* private driver-specific data */


    /* at least one of these two "factories" is required */

    struct AdfDevice * (*createDev)( const char * const name,
                                     const uint32_t     cylinders,
                                     const uint32_t     heads,
                                     const uint32_t     sectors );

    struct AdfDevice * (*openDev) ( const char * const  name,
                                    const AdfAccessMode mode );

    /* required */

    ADF_RETCODE (*closeDev)(struct AdfDevice * const dev);

    ADF_RETCODE (*readSectors)( const struct AdfDevice * const  dev,
                                const uint32_t                  block,
                                const uint32_t                  lenBlocks,
                                uint8_t * const                 buf );

    ADF_RETCODE (*writeSectors)( const struct AdfDevice * const  dev,
                                 const uint32_t                  block,
                                 const uint32_t                  lenBlocks,
                                 const uint8_t * const           buf );

    bool (*isNative)( void );   /* should return true only on a native block device driver,
                                   ie. such driver which can return real hardware
                                   geometry information */

    /* optional (can be NULL); should help to match device string with the driver */

    bool (*isDevice)( const char * const name );
};

#endif  /* ADF_DEV_DRIVER_H */
