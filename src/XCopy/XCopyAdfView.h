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
     * @brief List one directory through printEntry(), reporting any corruption.
     *
     * The walk itself is adfWalkDir() in XCopyAdfWalk.h, which knows nothing about
     * the terminal and is therefore testable on a host. This supplies it with the
     * two block buffers it needs, prints what it reports, and turns its truncated
     * chain warnings into console lines.
     *
     * @param dirSector the directory's header block; vol->rootBlock for the root,
     *                  vol->curDirPtr for wherever adfChangeDir() has reached
     * @result how many entries were listed
     */
    uint16_t printDirectory(struct AdfVolume *vol, ADF_SECTNUM dirSector);
}

#endif // XCOPYADFVIEW_H
