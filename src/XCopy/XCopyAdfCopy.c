#include "XCopyAdfCopy.h"

#include "../adflib/adf_dev.h"
#include "../adflib/adf_dir.h"
#include "../adflib/adf_env.h"
#include "../adflib/adf_err.h"

AdfCopyResult adfCopyFile(struct AdfVolume *src, const char *srcName,
                          struct AdfVolume *dst, const char *dstName,
                          unsigned char *buffer, unsigned long bufferSize,
                          unsigned long *copied)
{
    if (copied != NULL)
        *copied = 0;

    if (src == NULL || dst == NULL || srcName == NULL || dstName == NULL ||
        buffer == NULL || bufferSize == 0)
        return ADF_COPY_OPEN_FAILED;

    if (dst->readOnly || dst->dev->readOnly)
        return ADF_COPY_READ_ONLY;

    /*
       Refuse rather than overwrite.

       adfFileOpen() in write mode opens an existing file at position zero without
       truncating it, so copying a short file over a long one would leave the tail
       of the long one attached to it - which is worse than either overwriting or
       refusing. Refusing is the one of the three that cannot lose anything.
    */
    // adfGetEntryBlockNum() rather than adfGetEntry(): it answers the same
    // question without strdup()ing a name and a comment that would then have to
    // be freed field by field, which is the trap that adfFreeEntry() is.
    if (adfGetEntryBlockNum(dst, dst->curDirPtr, dstName) != -1)
        return ADF_COPY_EXISTS;

    struct AdfFile *in = adfFileOpen(src, srcName, ADF_FILE_MODE_READ);
    if (in == NULL)
        return ADF_COPY_NO_SOURCE;

    struct AdfFile *out = adfFileOpen(dst, dstName, ADF_FILE_MODE_WRITE);
    if (out == NULL)
    {
        adfFileClose(in);
        return ADF_COPY_OPEN_FAILED;
    }

    AdfCopyResult result = ADF_COPY_OK;
    unsigned long total = 0;

    while (!adfFileAtEOF(in))
    {
        const uint32_t got = adfFileRead(in, (uint32_t)bufferSize, buffer);
        if (got == 0)
        {
            // Not at EOF and nothing read: the source has a block it cannot give
            // back. Stopping here leaves a short file rather than a file with a
            // hole in it that looks complete.
            result = ADF_COPY_READ_FAILED;
            break;
        }

        if (adfFileWrite(out, got, buffer) != got)
        {
            // Almost always a full disk. The partial file is left in place: the
            // caller knows how far it got and can remove it, and removing it from
            // in here would throw away the only evidence of what happened.
            result = ADF_COPY_WRITE_FAILED;
            break;
        }

        total += got;
    }

    /*
       Flushed before the handle goes, and the flush is checked.

       adfFileClose() flushes too but has no way to report that it could not: a
       failure there would leave the last block of the file unwritten and the copy
       looking successful.
    */
    if (result == ADF_COPY_OK && adfFileFlush(out) != ADF_RC_OK)
        result = ADF_COPY_WRITE_FAILED;

    adfFileClose(out);
    adfFileClose(in);

    if (copied != NULL)
        *copied = total;

    return result;
}

const char *adfCopyResultText(AdfCopyResult result)
{
    switch (result)
    {
    case ADF_COPY_OK:           return "copied";
    case ADF_COPY_NO_SOURCE:    return "the source is not there, or is not a file";
    case ADF_COPY_EXISTS:       return "something of that name is already there";
    case ADF_COPY_READ_ONLY:    return "the destination is mounted read only";
    case ADF_COPY_OPEN_FAILED:  return "the destination could not be created";
    case ADF_COPY_READ_FAILED:  return "the source stopped reading part way through";
    case ADF_COPY_WRITE_FAILED: return "the destination ran out of room";
    }
    return "failed";
}
