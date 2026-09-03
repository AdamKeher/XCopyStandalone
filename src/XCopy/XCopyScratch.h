#ifndef XCOPYSCRATCH_H
#define XCOPYSCRATCH_H

#include <Arduino.h>

class XCopyFloppy;

/**
 * @brief Borrowing interface for the one large buffer on the device.
 *
 * XCopyFloppy::getStream() is a ~26KB block malloc'd once at boot and pinned above
 * 0x20000000 for bit-band access. It is the only sizeable free-standing block on a
 * 64KB part, and outside an actual track read, write or flux capture it sits idle.
 *
 * Everything else is squeezed into what is left. After .bss (~28KB) and that block,
 * roughly 6KB remains between the top of the heap and _estack, and the stack and the
 * heap both grow into it from opposite ends. So a transfer buffer costs the same
 * scarce bytes whether it is declared static or on the stack -- moving one to the
 * other only changes which end runs out first, which is why the choice kept getting
 * reversed in this code.
 *
 * Borrowing the idle track buffer costs nothing from that 6KB. The rule is that a
 * borrower must not overlap a floppy operation, and since the collision would
 * otherwise be silent -- a corrupted track or a corrupted file, some seconds later --
 * borrow() refuses rather than aliasing, and names the current holder so the report
 * says which two things overlapped.
 *
 * Single threaded by design. The ISRs never call this; XCopyLive and the SCP capture
 * take the borrow on the main loop before arming their interrupts.
 *
 * Two users deliberately do NOT borrow: the MFM read path, where ftm0_isr writes the
 * stream directly, and floppyTrackMfmEncode() on the write path. Those are the
 * buffer's primary owner, and a denied borrow there would abort a disk operation
 * rather than prevent a collision. They are safe unguarded because no borrower can
 * run while they do - every borrower is reached from the main loop, and a disk
 * operation blocks it. Nothing inside XCopyDisk calls _esp->update(), so an incoming
 * web command cannot re-enter doCommand() mid-track; cancel is a flag, not a
 * dispatch. Keep it that way: servicing commands inside a disk loop would make the
 * collision real, and the two paths above would not report it.
 */
namespace XCopyScratch
{
    //! Bind to the floppy that owns the buffer. Called once, from XCopy::begin().
    void attach(XCopyFloppy *floppy);

    /**
     * @brief Claim the buffer.
     *
     * @param owner  caller name, kept for the diagnostic if someone else collides.
     *               Must outlive the borrow; pass a string literal.
     * @param need   bytes required. Fails rather than truncating if the block is
     *               smaller, so a caller cannot quietly overrun it.
     * @result the buffer, or nullptr if it is already held or too small.
     */
    uint8_t *borrow(const char *owner, size_t need);

    //! Release a borrow. Safe to call with a null pointer from a failed borrow().
    void release(const uint8_t *buffer);

    //! Bytes available to a borrower.
    size_t capacity();

    //! Who holds it, or nullptr if free. For logging.
    const char *heldBy();

    /**
     * @brief Scoped borrow, for callers with more than one way out.
     *
     * captureDiskTrack() alone has six early returns; releasing by hand on each is
     * how a borrow gets leaked and every later borrow denied. Test with valid()
     * before using get().
     */
    class Guard
    {
    public:
        Guard(const char *owner, size_t need) : _buffer(borrow(owner, need)) {}
        ~Guard() { release(_buffer); }

        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

        bool valid() const { return _buffer != nullptr; }
        uint8_t *get() const { return _buffer; }

    private:
        uint8_t *_buffer;
    };
}

#endif // XCOPYSCRATCH_H
