#ifndef XCOPYADFVIEW_H
#define XCOPYADFVIEW_H

#include <Arduino.h>

extern "C"
{
#include "../adflib/adflib.h"
}

/*
   Reading a mounted volume out to the console, one line at a time.

   Two things here are deliberate and both are about the 5.6KB heap.

   The printers write through XCopyLog as they go instead of returning a String.
   The version this replaces built its whole directory listing by appending to one
   String - sixty entries is several kilobytes plus the reallocation churn to get
   there, on a part where that is most of the free heap.

   eachEntry() walks a directory without building a list. ADFlib's own
   adfGetDirEnt() mallocs an AdfEntry per entry and a name and comment inside each
   one, and hands back a linked list that has to be held whole before the first line
   can be printed. This walks the same hash table and reports one entry at a time,
   which is the shape XCopyDirLister already uses for the SD card and for the same
   reason. Its cost is a fixed ~1KB of stack and nothing else.
*/
namespace XCopyAdfView
{
    //! Device geometry, type, and where it came from.
    void printDevice(const struct AdfDevice *dev);

    //! Volume name, filesystem, and how full it is.
    void printVolume(struct AdfVolume *vol);

    //! The two header lines a listing sits under.
    void printListHeader();

    //! One entry as a row: size, date, block, name, comment.
    void printEntry(const struct AdfEntry *entry);

    /**
     * @brief Called once per entry of a directory. See eachEntry().
     *
     * @p entry is valid only for the duration of the call - its name and comment
     * are freed as soon as it returns - so a visitor that wants to keep anything
     * must copy it.
     */
    typedef void (*EntryVisit)(void *context, const struct AdfEntry *entry);

    /**
     * @brief Walk one directory, reporting each entry as it is read.
     *
     * Not recursive, and deliberately so: adfGetRDirEnt() recurses up to 512 levels
     * and there are 6,252 bytes of stack on this board. A caller that wants a tree
     * should keep its own explicit list of directories still to visit.
     *
     * Carries ADFlib's two nextSameHash sanity checks (upstream issue #99): some
     * disks link an entry back to itself or into the parent's hash table, which
     * locks AmigaOS and would spin here. Both are reported and end that chain
     * rather than the whole listing.
     *
     * @param vol       a mounted volume
     * @param dirSector the directory's header block; vol->curDirPtr for the current
     *                  one, vol->rootBlock for the root
     * @result false if the directory block could not be read at all
     */
    bool eachEntry(struct AdfVolume *vol,
                   ADF_SECTNUM dirSector,
                   EntryVisit visit,
                   void *context);

    //! eachEntry() + printEntry(), and a count of what was listed.
    uint16_t printDirectory(struct AdfVolume *vol, ADF_SECTNUM dirSector);
}

#endif // XCOPYADFVIEW_H
