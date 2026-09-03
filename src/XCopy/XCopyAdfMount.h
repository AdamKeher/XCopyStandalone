#ifndef XCOPYADFMOUNT_H
#define XCOPYADFMOUNT_H

#include <Arduino.h>
#include "XCopyAdfPath.h"

extern "C"
{
#include "../adflib/adflib.h"
}

/*
   What is mounted, and where.

   A fixed table of slots, each naming itself the way an Amiga would - ADF0:, ADF1: -
   so that one path grammar covers the card and the images on it. The table itself
   is static: slot names, backing paths and the two pointers cost nothing from the
   heap, which is the scarce thing here.

   What does cost heap is a mounted volume: about 720 bytes, most of it the 512 byte
   bitmap block ADFlib caches so it can allocate. An open file costs another 1,600,
   being three block buffers and a handle. Against roughly 5.4KB of malloc arena
   that is the whole budget, so mounts are not free and the table says so - `mount`
   with no arguments prints what is left.

   Mounts are read only unless asked otherwise. An image opened read only cannot be
   damaged by a mistake in a command that was only meant to look at it, and looking
   at it is what nearly every use is.
*/
namespace XCopyAdfMount
{
    //! Two image slots. Adding one costs ~720 bytes of heap when it is in use.
    static const uint8_t kSlots = 2;

    struct Slot
    {
        //! "ADF0", without the colon. Static, matched case insensitively.
        const char *name;

        //! The backing file on the card. Empty when nothing is mounted.
        String path;

        struct AdfDevice *dev;
        struct AdfVolume *vol;

        bool mounted() const { return vol != NULL; }
    };

    //! Ready the library and the table. Safe to call repeatedly.
    void begin();

    //! The table. kSlots entries, always present, mounted or not.
    Slot *slots();

    /**
     * @brief The slot @p device names, or nullptr.
     * @param device a name without its colon, any case - as XCopyPath reports it.
     */
    Slot *find(const String &device);

    /**
     * @brief Mount @p path into @p slot.
     *
     * Unmounts whatever was there first, so re-mounting a slot is one command and
     * cannot leak the previous device.
     *
     * @param readWrite false opens the image read only, which is the default and
     *        what every command that only reads should ask for.
     * @result false with the reason already printed
     */
    bool mount(Slot &slot, const String &path, bool readWrite);

    //! Release a slot. Safe on one that holds nothing.
    void unmount(Slot &slot);

    //! Release every slot. What "unmount" with no subject does.
    void unmountAll();

    /**
     * @brief Point @p slot's volume at the directory part of @p path.
     *
     * Walks from the volume root, so the result does not depend on what any
     * previous command left the volume pointing at. On success the volume's
     * curDirPtr is the directory and @p leaf is the last component - which may be
     * a file, a directory, or empty when @p path named a directory outright.
     *
     * @result false with the reason already printed - a component that is not
     *         there, or is not a directory
     */
    bool walkTo(Slot &slot, const String &path, String &leaf);

    /**
     * @brief Find what @p text refers to.
     *
     * The one place a command turns a typed path into something to act on. Reports
     * the slot when the path named a mounted image, and nullptr when it named the
     * card - which is not a failure, it is the other half of the grammar.
     *
     * @param slot   the slot, or nullptr for the SD card
     * @param within the path within whichever it was
     * @result false with the reason already printed - an unknown device, or one
     *         that has nothing mounted
     */
    bool resolve(const String &text, Slot *&slot, String &within);

    //! Bytes of malloc arena still free. What `mount` reports, and why.
    uint32_t freeHeap();
}

#endif // XCOPYADFMOUNT_H
