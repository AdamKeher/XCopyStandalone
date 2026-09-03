#ifndef XCOPYADFFLOPPYDRIVER_H
#define XCOPYADFFLOPPYDRIVER_H

class XCopyDisk;
class XCopyFloppy;

extern "C"
{
#include "../adflib/adf_dev_driver.h"
}

/*
   The disk in the drive, as an ADFlib device.

   The same interface XCopyAdfSdDriver implements over a file on the card, over an
   actual floppy instead - so "dir DF0:c" and "dir ADF0:c" are the same code above
   this line, and the library neither knows nor cares which it is reading.

   ADF block n is logical track n / 11, sector n % 11, which is what
   DiskLocation::setBlock() has always computed. The read is
   XCopyDisk::readDiskTrack(), which retries and reports a track as good only when
   every sector decoded and the header named the cylinder that was asked for. That
   is the right bar for a file system: a listing built out of a track that half
   read is worse than no listing.

   One drive, so one device: the state below is file scope rather than per device,
   and a second open of DF0: is refused rather than quietly sharing it.

   Read only. Writing a single sector means reading a whole track, replacing 512
   bytes of it and writing all eleven back, and that deserves its own change with
   its own verification - it writes to the operator's disk.
*/
extern "C" const struct AdfDeviceDriver xcopyAdfFloppyDriver;

/**
 * @brief Give the driver the drive to read through.
 *
 * Called once, from the console, which is the only thing that mounts DF0:. Until
 * it has been called the driver refuses to open, which is what makes a mount from
 * somewhere that has no drive fail cleanly rather than through a null pointer.
 *
 * @param retries how many times readDiskTrack() may retry a bad track. Comes from
 *        the operator's configured retry count.
 */
void xcopyAdfFloppyAttach(XCopyDisk *disk, XCopyFloppy *floppy, unsigned char retries);

//! Forget the cached track. Call when anything else may have moved the head.
void xcopyAdfFloppyInvalidate();

/**
 * @brief Whether the drive says the disk has been taken out or swapped.
 *
 * Reads /DSKCHG without stepping, so it is safe to ask before every command.
 * False when there is no drive attached, because "no drive" is not "disk gone".
 */
bool xcopyAdfFloppyDiskGone();

#endif // XCOPYADFFLOPPYDRIVER_H
