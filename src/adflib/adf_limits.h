/*
 *  adf_limits.h - ADFlib limits
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

#ifndef ADF_LIMITS_H
#define ADF_LIMITS_H

#include <limits.h>
#include <stdint.h>


/**********************************
 * Device
 **********************************/

#define ADF_DEV_BLOCK_SIZE 512

/*
   The primary limitation of max dev size comes from using stdio for accessing
   devices (ie. fseek uses 'long int' as offset).

   Some more modern systems allow to mitigate this limitation with fseeko,
   but, for instance, VBCC does not have that, so using it would limit portability.

   Note that this depends on the platform: 64bit systems (with 64bit long!)
   will be able to handle very large devices, while 32bit will be limited to 2GiB
   (max value of long int meaning int32_t).
*/

#if ( LONG_MAX / ADF_DEV_BLOCK_SIZE + 1 ) < UINT32_MAX
// long int is 32bit -> 2GiB limit(!)
#define ADF_DEV_SIZE_MAX_BLOCKS  ( LONG_MAX / ADF_DEV_BLOCK_SIZE + 1 )
#else
// the type of struct AdfDevice.sizeBlocks (uint32_t) makes the limit
#define ADF_DEV_SIZE_MAX_BLOCKS UINT32_MAX
#endif

/*
  Another limitation comes from the custom type ADF_SECT ("sector numer")
  which is defined as int32_t (adf_types.h) and is used in adf_vol on device
  level(!).

  This might(?) eventually be improved (type for specifying volume location
  can be (maybe even - should be) the same as the type used for the blocks
  in adf_dev (uint32_t). However, any calculations involving also other
  values remaining ADF_SECT type would require attention...)
*/
#if ADF_DEV_SIZE_MAX_BLOCKS > INT32_MAX
#undef ADF_DEV_SIZE_MAX_BLOCKS
#define ADF_DEV_SIZE_MAX_BLOCKS  INT32_MAX
#endif


/**********************************
 * Volume
 **********************************/

// The custom type ADF_SECT is int32_t. Also - Amiga OS limitation is 2GiB.
#define ADF_VOL_SIZE_MAX_BLOCKS  INT32_MAX


/**********************************
 * File
 **********************************/

#define ADF_FILE_SIZE_MAX INT_MAX

#endif
