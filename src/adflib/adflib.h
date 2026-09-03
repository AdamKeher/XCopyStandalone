/*
 *  adflib.h - main include file
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

#ifndef ADFLIB_H
#define ADFLIB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Windows - a DLL-specific function declaration prefix (to import/export library symbols) */
#include "adf_prefix.h"

#include "adf_limits.h"
#include "adf_types.h"
#include "adf_version.h"
#include "adf_err.h"

ADF_PREFIX char * adfGetVersionNumber(void);
ADF_PREFIX char * adfGetVersionDate(void);

ADF_PREFIX ADF_RETCODE adfLibInit(void);
ADF_PREFIX void adfLibCleanUp(void);

/* util */
//#include "adf_util.h"

/* dir */
#include "adf_dir.h"

/* file */
#include "adf_file.h"
#include "adf_file_block.h"

/* volume */
#include "adf_vol.h"

/* device */
#include "adf_dev.h"
#include "adf_dev_flop.h"
#include "adf_dev_hd.h"
#include "adf_dev_hdfile.h"

/* device drivers */
#include "adf_dev_drivers.h"

/* env */
#include "adf_env.h"

/* salv */
#include "adf_salv.h"

/* middle level API */

/* low level API */

#include "adf_bitm.h"
#include "adf_raw.h"

#ifdef __cplusplus
}
#endif

#endif  /* ADFLIB_H */
