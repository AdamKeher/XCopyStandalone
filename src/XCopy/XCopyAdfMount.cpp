#include "XCopyAdfMount.h"
#include "XCopyAdfHost.h"
#include "XCopyLog.h"
#include "XCopyConsole.h"
#include "XCopyPins.h"
#include "XCopyAdfFloppyDriver.h"

#include <stdlib.h>

namespace
{
    /*
       The table.

       Static, and the Strings inside it are the only part that touches the heap -
       a slot's backing path, which is a few dozen bytes and is released with the
       mount. Everything a mount actually costs is inside ADFlib.
    */
    XCopyAdfMount::Slot _slots[XCopyAdfMount::kSlots] = {
        {"DF0", true, "", NULL, NULL},
        {"ADF0", false, "", NULL, NULL},
        {"ADF1", false, "", NULL, NULL},
    };

    bool sameName(const char *slot, const String &device)
    {
        return device.equalsIgnoreCase(String(slot));
    }

    /*
       Whether the card was there last time anyone asked.

       A mounted image holds a FatFile open on the card, and the card can be pulled
       at any moment with nothing to stop it. There is no card change event in this
       firmware - every caller polls the detect pin - so this polls it too, from the
       one place every command that touches a mount goes through.

       A change in either direction drops the mounts. Absent is obvious. Present
       after absent matters just as much: the card may not be the same one, and a
       cluster chain remembered from the old card would read the new one's data and
       report it as an Amiga file system.
    */
    int8_t _cardWasPresent = -1;

    bool cardPresent()
    {
        // Pulled up, driven low by the card switch: the same test XCopySDCard
        // makes, without dragging its error string along.
        return digitalRead(PIN_CARDDETECT) == 0;
    }
}

void XCopyAdfMount::begin()
{
    /*
       The disk in the drive can be swapped as freely as the card, and a DF0:
       mount that outlives its disk would answer with the new disk's blocks laid
       out to the old one's directory - which reads as a corrupt file system
       rather than as the mistake it is.
    */
    if (_slots[kDriveSlot].mounted() && xcopyAdfFloppyDiskGone())
    {
        unmount(_slots[kDriveSlot]);
        Log << F("disk changed: DF0: released\r\n");
    }

    const bool present = cardPresent();
    if (_cardWasPresent >= 0 && (present ? 1 : 0) != _cardWasPresent)
    {
        // Said out loud. Finding a slot empty with no explanation is worse than
        // being told why, and this is the one thing that empties one unasked.
        unmountAll();
        Log << F("card changed: all slots released\r\n");
    }
    _cardWasPresent = present ? 1 : 0;

    XCopyAdf::begin();
}

XCopyAdfMount::Slot *XCopyAdfMount::slots()
{
    return _slots;
}

XCopyAdfMount::Slot *XCopyAdfMount::find(const String &device)
{
    for (uint8_t i = 0; i < kSlots; i++)
        if (sameName(_slots[i].name, device))
            return &_slots[i];
    return nullptr;
}

bool XCopyAdfMount::mountDrive(Slot &slot)
{
    begin();
    unmount(slot);

    XCopyAdf::clearErrors();

    slot.dev = adfDevOpenWithDriver("floppy", "DF0:", ADF_ACCESS_MODE_READONLY);
    if (slot.dev == NULL)
    {
        Log << XCopyConsole::error("cannot read the disk in the drive") << F("\r\n");
        return false;
    }

    if (adfDevMount(slot.dev) != ADF_RC_OK)
    {
        Log << XCopyConsole::error("no Amiga filesystem on the disk in the drive") << F("\r\n");
        unmount(slot);
        return false;
    }

    slot.vol = adfVolMount(slot.dev, 0, ADF_ACCESS_MODE_READONLY);
    if (slot.vol == NULL)
    {
        Log << XCopyConsole::error("cannot mount a volume on the disk in the drive") << F("\r\n");
        unmount(slot);
        return false;
    }

    slot.path = "";
    return true;
}

bool XCopyAdfMount::mount(Slot &slot, const String &path, bool readWrite)
{
    begin();

    // Whatever was here goes first, so re-mounting a slot cannot leak a device
    // and cannot leave a half mounted one behind if the new image fails to open.
    unmount(slot);

    XCopyAdf::clearErrors();

    const AdfAccessMode mode =
        readWrite ? ADF_ACCESS_MODE_READWRITE : ADF_ACCESS_MODE_READONLY;

    // Named rather than guessed: the driver that reads a file off the card is
    // known here, and adfDevOpen() guessing from the path could only guess wrong.
    slot.dev = adfDevOpenWithDriver("sd", path.c_str(), mode);
    if (slot.dev == NULL)
    {
        Log << XCopyConsole::error("cannot open '" + path + "'") << F("\r\n");
        return false;
    }

    if (adfDevMount(slot.dev) != ADF_RC_OK)
    {
        Log << XCopyConsole::error("no filesystem on '" + path + "'") << F("\r\n");
        unmount(slot);
        return false;
    }

    /*
       Volume 0 of however many.

       A floppy image has exactly one. A hardfile with a Rigid Disk Block can have
       several, and mounting the first of them is a decision rather than an
       oversight: there is no syntax yet for saying which, and "vol" prints all of
       them so the operator can see what they would be choosing between.
    */
    slot.vol = adfVolMount(slot.dev, 0, mode);
    if (slot.vol == NULL)
    {
        Log << XCopyConsole::error("cannot mount a volume on '" + path + "'") << F("\r\n");
        unmount(slot);
        return false;
    }

    slot.path = path;
    return true;
}

void XCopyAdfMount::unmount(Slot &slot)
{
    if (slot.vol != NULL)
    {
        adfVolUnMount(slot.vol);
        slot.vol = NULL;
    }

    if (slot.dev != NULL)
    {
        // adfDevClose() unmounts the device too, which frees the volume structs in
        // its volList - including the one just released above.
        adfDevClose(slot.dev);
        slot.dev = NULL;
    }

    slot.path = "";
}

void XCopyAdfMount::unmountAll()
{
    for (uint8_t i = 0; i < kSlots; i++)
        unmount(_slots[i]);
}

bool XCopyAdfMount::walkTo(Slot &slot, const String &path, String &leaf)
{
    if (!slot.mounted())
    {
        Log << XCopyConsole::error(String(slot.name) + ": has nothing mounted") << F("\r\n");
        return false;
    }

    String directory;
    xcopySplitLeaf(path, directory, leaf);

    /*
       Always from the root.

       adfChangeDir() moves the volume's own current directory and leaves it moved,
       so a walk that started from wherever the last command finished would make
       "dir ADF0:c" mean different things on consecutive runs. Starting from the
       root every time costs one block read and removes the whole class of problem.
    */
    if (adfToRootDir(slot.vol) != ADF_RC_OK)
    {
        Log << XCopyConsole::error(String(slot.name) + ": cannot read the root directory") << F("\r\n");
        return false;
    }

    String remaining = directory;
    String component;
    while (xcopyNextComponent(remaining, component))
    {
        XCopyAdf::clearErrors();
        if (adfChangeDir(slot.vol, component.c_str()) != ADF_RC_OK)
        {
            Log << XCopyConsole::error("'" + component + "' is not a directory in " +
                                       String(slot.name) + ":")
                << F("\r\n");
            return false;
        }
    }

    return true;
}

bool XCopyAdfMount::resolve(const String &text, Slot *&slot, String &within)
{
    slot = nullptr;
    within = text;

    XCopyPath path;
    if (!xcopySplitPath(text, path))
    {
        Log << XCopyConsole::error("'" + text + "' names no device") << F("\r\n");
        return false;
    }

    within = path.rest;

    if (path.isCard())
        return true;

    slot = find(path.device);
    if (slot == nullptr)
    {
        Log << XCopyConsole::error("no such device '" + path.device + ":'") << F("\r\n");
        Log << F("try 'mount' to see what there is\r\n");
        return false;
    }

    if (!slot->mounted())
    {
        Log << XCopyConsole::error(path.device + ": has nothing mounted") << F("\r\n");
        return false;
    }

    return true;
}

uint32_t XCopyAdfMount::freeHeap()
{
    /*
       The same measurement cmdMem() makes, and with the same caveat: it is the gap
       between the top of the heap and the current stack pointer, so it counts the
       arena that is left rather than the largest block that could be taken out of
       it. Fragmentation makes the second number smaller and there is no cheap way
       to ask newlib for it.
    */
    uint32_t stackTop = (uint32_t)&stackTop;
    void *heapTop = malloc(1);
    if (heapTop == NULL)
        return 0;
    const uint32_t top = (uint32_t)heapTop;
    free(heapTop);

    return stackTop > top ? stackTop - top : 0;
}
