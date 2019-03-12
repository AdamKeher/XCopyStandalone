#ifndef ADF_FILE_H
#define ADF_FILE_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_file.h
 *
 *  $Id$
 *
 *  This file is part of ADFLib.
 *
 *  ADFLib is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ADFLib is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Foobar; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include"prefix.h"

#include"adf_str.h"

RETCODE adfGetFileBlocks(struct Volume* vol, struct bFileHeaderBlock* entry,
    struct FileBlocks* );
RETCODE adfFreeFileBlocks(struct Volume* vol, struct bFileHeaderBlock *entry);
PREFIX int32_t adfFileRealSize(uint32_t size, int blockSize, int32_t *dataN, int32_t *extN);

int32_t adfPos2DataBlock(int32_t pos, int blockSize, int *posInExtBlk, int *posInDataBlk, int32_t *curDataN );

RETCODE adfWriteFileHdrBlock(struct Volume *vol, SECTNUM nSect, struct bFileHeaderBlock* fhdr);

RETCODE adfReadDataBlock(struct Volume *vol, SECTNUM nSect, void *data);
RETCODE adfWriteDataBlock(struct Volume *vol, SECTNUM nSect, void *data);
RETCODE adfReadFileExtBlock(struct Volume *vol, SECTNUM nSect, struct bFileExtBlock* fext);
RETCODE adfWriteFileExtBlock(struct Volume *vol, SECTNUM nSect, struct bFileExtBlock* fext);

PREFIX struct AFile* adfOpenFile(struct Volume *vol, char* name, char *mode);
PREFIX void adfCloseFile(struct AFile *file);
PREFIX int32_t adfReadFile(struct AFile* file, int32_t n, uint8_t *buffer);
PREFIX BOOL adfEndOfFile(struct AFile* file);
PREFIX void adfFileSeek(struct AFile *file, uint32_t pos);		/* BV */
RETCODE adfReadNextFileBlock(struct AFile* file);
PREFIX int32_t adfWriteFile(struct AFile *file, int32_t n, uint8_t *buffer);
SECTNUM adfCreateNextFileBlock(struct AFile* file);
PREFIX void adfFlushFile(struct AFile *file);

#ifdef __cplusplus
}
#endif

#endif /* ADF_FILE_H */

