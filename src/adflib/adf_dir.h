/*
 *  adf_dir.c - directory code
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

#ifndef ADF_DIR_H
#define ADF_DIR_H

#include "adf_blk.h"
#include "adf_err.h"
#include "adf_prefix.h"
#include "adf_vol.h"


/* ----- ENTRY ---- */

struct AdfEntry {
    int          type;
    char *       name;
    ADF_SECTNUM  sector;
    ADF_SECTNUM  real;
    ADF_SECTNUM  parent;
    char *       comment;
    uint32_t     size;
    int32_t      access;

    int          year,
                 month,
                 days;

    int          hour,
                 mins,
                 secs;
};

/* traverse dir. tree (updating vol->curDirPtr) */
ADF_PREFIX ADF_RETCODE adfToRootDir( struct AdfVolume * const  vol );
ADF_PREFIX ADF_RETCODE adfChangeDir( struct AdfVolume * const  vol,
                                     const char * const        name );
ADF_PREFIX ADF_RETCODE adfParentDir( struct AdfVolume * const  vol );

/* read entry */
ADF_PREFIX ADF_RETCODE adfEntryRead( const struct AdfVolume * const  vol,
                                     const ADF_SECTNUM               nSect,
                                     struct AdfEntry * const         entry,
                                     struct AdfEntryBlock * const    entryBlk );

/* get and free list of directory entries */
ADF_PREFIX struct AdfList * adfGetDirEnt( const struct AdfVolume * const  vol,
                                          const ADF_SECTNUM               nSect );

ADF_PREFIX struct AdfList * adfGetRDirEnt( const struct AdfVolume * const  vol,
                                           const ADF_SECTNUM               nSect,
                                           const bool                      recurs );

ADF_PREFIX void adfFreeDirList( struct AdfList * const  list );
ADF_PREFIX void adfFreeEntry( struct AdfEntry * const  entry );

/* get entry by name */
ADF_PREFIX ADF_RETCODE adfGetEntry( struct AdfVolume * const  vol,
                                    const ADF_SECTNUM         dirPtr,
                                    const char * const        name,
                                    struct AdfEntry * const   entry );

/* get entry block number by name */
ADF_PREFIX ADF_SECTNUM adfGetEntryBlockNum( struct AdfVolume * const  vol,
                                            const ADF_SECTNUM         dirPtr,
                                            const char * const        name );

/* get entry block by name */
ADF_PREFIX ADF_SECTNUM adfGetEntryBlock( struct AdfVolume * const      vol,
                                         const ADF_SECTNUM             dirPtr,
                                         const char * const            name,
                                         struct AdfEntryBlock * const  entry );

/* create a new entry */

ADF_RETCODE adfCreateFile( struct AdfVolume * const           vol,
                           const ADF_SECTNUM                  parent,
                           const char * const                 name,
                           struct AdfFileHeaderBlock * const  fhdr );

ADF_PREFIX ADF_RETCODE adfCreateDir( struct AdfVolume * const  vol,
                                     const ADF_SECTNUM         parent,
                                     const char * const        name );

ADF_SECTNUM adfCreateEntry( struct AdfVolume * const      vol,
                            struct AdfEntryBlock * const  dir,
                            const char * const            name,
                            const ADF_SECTNUM             thisSect );

/* remove entry */
ADF_PREFIX ADF_RETCODE adfRemoveEntry( struct AdfVolume * const  vol,
                                       const ADF_SECTNUM         pSect,
                                       const char * const        name );

/* rename entry */
ADF_PREFIX ADF_RETCODE adfRenameEntry( struct AdfVolume * const  vol,
                                       const ADF_SECTNUM         pSect,
                                       const char * const        oldName,
                                       const ADF_SECTNUM         nPSect,
                                       const char * const        newName );

/* update attributes */

ADF_PREFIX ADF_RETCODE adfSetEntryAccess( struct AdfVolume * const  vol,
                                          const ADF_SECTNUM         parSect,
                                          const char * const        name,
                                          const int32_t             newAcc );

ADF_PREFIX ADF_RETCODE adfSetEntryComment( struct AdfVolume * const  vol,
                                           const ADF_SECTNUM         parSect,
                                           const char * const        name,
                                           const char * const        newCmt );

/* get directory info/stats */
bool adfIsDirEmpty( const struct AdfDirBlock * const  dir );
ADF_PREFIX int adfDirCountEntries( struct AdfVolume * const  vol,
                                   const ADF_SECTNUM         dirPtr );


/* block operations */

ADF_PREFIX ADF_RETCODE adfReadEntryBlock( const struct AdfVolume * const  vol,
                                          const ADF_SECTNUM               nSect,
                                          struct AdfEntryBlock * const    ent );

ADF_RETCODE adfWriteEntryBlock( const struct AdfVolume * const      vol,
                                const ADF_SECTNUM                   nSect,
                                const struct AdfEntryBlock * const  ent );

ADF_RETCODE adfWriteDirBlock( const struct AdfVolume * const  vol,
                              const ADF_SECTNUM               nSect,
                              struct AdfDirBlock * const      dir );

ADF_RETCODE adfEntBlock2Entry( const struct AdfEntryBlock * const  entryBlk,
                               struct AdfEntry * const             entry );

ADF_SECTNUM adfNameToEntryBlk( struct AdfVolume * const      vol,
                               const int32_t                 ht[],
                               const char * const            name,
                               struct AdfEntryBlock * const  entry,
                               ADF_SECTNUM * const           nUpdSect );

/*
 * adfEntryGetInfo
 *
 * Returns pointer to dynamically allocated char array
 * with text information about the device and its volumes.
 * It must be free()-d afterwards.
 */
ADF_PREFIX char * adfEntryGetInfo( const struct AdfEntry * const  entry );


/*
 * adfDirCheck
 *
 * Checks consistency of a directory (or a directory tree)
 *
 * So far, the check is very basic:
 * - checks if entries metadata (blocks) read properly
 * - checks if the directories do not point to themselves (block number)
 *   in the nextSameHash (see issue #99)
 *
 * Returns number of errors, 0 if success (no errors)
 */
unsigned adfDirCheck( const struct AdfVolume * const  vol,
                      const ADF_SECTNUM               nSect,
                      const bool                      recurs );

#endif  /* ADF_DIR_H */
