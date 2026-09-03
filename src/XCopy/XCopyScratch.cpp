#include "XCopyScratch.h"
#include "XCopyFloppy.h"
#include "XCopyLog.h"

namespace XCopyScratch
{
    static XCopyFloppy *_floppy = nullptr;
    static const char *_owner = nullptr;

    void attach(XCopyFloppy *floppy)
    {
        _floppy = floppy;
    }

    size_t capacity()
    {
        return (_floppy == nullptr) ? 0 : _floppy->getStreamSize();
    }

    const char *heldBy()
    {
        return _owner;
    }

    uint8_t *borrow(const char *owner, size_t need)
    {
        if (_floppy == nullptr)
        {
            Log << F("scratch: borrow by '") << owner << F("' before attach()\r\n");
            return nullptr;
        }

        if (_owner != nullptr)
        {
            // Two things wanted the track buffer at once. Handing it over would alias
            // them onto each other and the damage would not show up here, so refuse
            // and let the caller fail visibly instead.
            Log << F("scratch: '") << owner << F("' denied, held by '") << _owner << F("'\r\n");
            return nullptr;
        }

        if (need > _floppy->getStreamSize())
        {
            Log << F("scratch: '") << owner << F("' wants ") << (uint32_t)need
                << F(" b, buffer is ") << (uint32_t)_floppy->getStreamSize() << F(" b\r\n");
            return nullptr;
        }

        _owner = owner;
        return (uint8_t *)_floppy->getStream();
    }

    void release(const uint8_t *buffer)
    {
        // Tolerates a null so a caller can release unconditionally on its error path
        // without first testing whether the borrow succeeded.
        if (buffer == nullptr)
            return;

        _owner = nullptr;
    }
}
