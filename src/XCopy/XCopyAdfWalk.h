#ifndef XCOPYADFWALK_H
#define XCOPYADFWALK_H

/*
   Walking a directory without building a list of it.

   ADFlib's own adfGetDirEnt() mallocs an AdfEntry per entry, plus a name and a
   comment inside each one, and returns a linked list that has to be held whole
   before the first entry can be looked at. On a disk with sixty files that is
   around six kilobytes, against the 5.6KB of heap this board has. It is the right
   shape on a desktop and the wrong one here.

   This walks the same hash table and reports one entry at a time. Peak cost is one
   entry - about ninety bytes - plus two block buffers the caller supplies.

   Plain C, and depends on nothing but ADFlib. That is what lets it run under
   `pio test -e native` against a dump-file image: everything that knows about the
   terminal, the log or the SD card is on the other side of the two callbacks.
*/

#include "../adflib/adf_dir.h"
#include "../adflib/adf_vol.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
       Why a chain stopped early. Both are corrupt-directory cases that ADFlib
       reports and recovers from (upstream issue #99), and both are worth telling
       the operator about: they mean the disk needs repairing before it is written.
    */
    typedef enum
    {
        //! An entry's nextSameHash points at the entry itself.
        ADF_WALK_SELF_LINK = 1,
        //! It points at something already in the parent's hash table.
        ADF_WALK_HASH_LOOP = 2
    } AdfWalkFault;

    /**
     * @brief Called once per entry.
     *
     * @p entry is valid only for the duration of the call - its name and comment
     * are freed as soon as it returns - so a visitor keeping anything must copy it.
     */
    typedef void (*AdfWalkVisit)(void *context, const struct AdfEntry *entry);

    //! Called instead of a visit when a chain is truncated. May be NULL.
    typedef void (*AdfWalkWarn)(void *context, ADF_SECTNUM sector, AdfWalkFault fault);

    /**
     * @brief Report every entry of one directory.
     *
     * Not recursive, deliberately. adfGetRDirEnt() recurses up to 512 levels and
     * this board has 6,252 bytes of stack; a caller wanting a tree should keep its
     * own list of directories still to visit.
     *
     * @param vol       a mounted volume
     * @param dirSector the directory's header block - vol->rootBlock for the root,
     *                  vol->curDirPtr for wherever adfChangeDir() has reached
     * @param scratch   two block buffers, supplied by the caller so this can be
     *                  called from a path that has already borrowed its own memory.
     *                  Must not alias anything else in use.
     * @result false only if the directory block itself could not be read, or an
     *         entry in it could not be. A truncated chain is a warning, not a
     *         failure: everything already reported is real.
     */
    bool adfWalkDir(struct AdfVolume *vol,
                    ADF_SECTNUM dirSector,
                    struct AdfEntryBlock scratch[2],
                    AdfWalkVisit visit,
                    AdfWalkWarn warn,
                    void *context);

    //! How many entries adfWalkDir() would report. Reads the directory twice over.
    int adfWalkCount(struct AdfVolume *vol,
                     ADF_SECTNUM dirSector,
                     struct AdfEntryBlock scratch[2]);

#ifdef __cplusplus
}
#endif

#endif /* XCOPYADFWALK_H */
