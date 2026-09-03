/*
 *  adf_dev_hdfile.h - device code / hardfile (HDF)
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

#ifndef ADF_DEV_HDFILE_H
#define ADF_DEV_HDFILE_H

#include "adf_dev.h"
#include "adf_err.h"
#include "adf_prefix.h"

/* format hdf volume (erases data!) */
ADF_PREFIX ADF_RETCODE adfCreateHdFile( struct AdfDevice * const  dev,
                                        const char * const        volName,
                                        const uint8_t             volType );

/* mount hdf volume (called by adfDevMount) */
ADF_RETCODE adfMountHdFile( struct AdfDevice * const  dev );

#endif /* ADF_DEV_HDFILE_H */
