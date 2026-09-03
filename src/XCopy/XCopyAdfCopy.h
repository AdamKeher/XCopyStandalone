#ifndef XCOPYADFCOPY_H
#define XCOPYADFCOPY_H

/*
   Copying a file from one mounted volume to another, or to somewhere else in the
   same one.

   Plain C over ADFlib, so it runs under `pio test -e native` against two dump-file
   images. That matters more here than anywhere else in this feature: this is the
   part that writes, and the only other way to know whether a copied file arrived
   intact is to put the disk in an Amiga and look.

   Both files are open at once, which is about 3.2KB of heap - three block buffers
   and a handle each. On a part with roughly 5.4KB of arena that is most of it, so
   the caller is expected to have checked there is room before asking.
*/

#include "../adflib/adf_file.h"
#include "../adflib/adf_vol.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        ADF_COPY_OK = 0,
        ADF_COPY_NO_SOURCE,     //!< the source file is not there, or is not a file
        ADF_COPY_EXISTS,        //!< something of that name is already at the destination
        ADF_COPY_READ_ONLY,     //!< the destination volume was mounted read only
        ADF_COPY_OPEN_FAILED,   //!< the destination could not be created
        ADF_COPY_READ_FAILED,   //!< the source stopped reading part way through
        ADF_COPY_WRITE_FAILED   //!< the destination stopped accepting part way through
    } AdfCopyResult;

    /**
     * @brief Copy @p srcName from @p src into @p dst as @p dstName.
     *
     * Both volumes must already be positioned at the directories concerned -
     * ADFlib resolves a name against vol->curDirPtr - which is what
     * XCopyAdfMount::walkTo() does. @p src and @p dst may be the same volume, in
     * which case the current directory is whatever the last walk left it at, and
     * the copy is within that directory.
     *
     * Refuses rather than overwrites. A cp that silently replaced a file would be
     * the fastest way to lose one, and there is no undo on a floppy.
     *
     * @param buffer     working space, supplied by the caller so this does not add
     *                   to the two open files already on the heap
     * @param bufferSize how much of it; bigger is fewer round trips to the disk
     * @param copied     bytes transferred, set even when the result is a failure
     *                   part way through
     */
    AdfCopyResult adfCopyFile(struct AdfVolume *src, const char *srcName,
                              struct AdfVolume *dst, const char *dstName,
                              unsigned char *buffer, unsigned long bufferSize,
                              unsigned long *copied);

    //! A sentence for @p result, ready to print. Never null.
    const char *adfCopyResultText(AdfCopyResult result);

#ifdef __cplusplus
}
#endif

#endif /* XCOPYADFCOPY_H */
