#ifndef XCOPYDISKINFO_H
#define XCOPYDISKINFO_H

#include <Arduino.h>
#include "XCopyESP8266.h"
#include "XCopyFloppy.h"
#include "XCopyGraphics.h"
#include "XCopyAudio.h"

/**
 * @brief Angular buckets a track surface is described in.
 *
 * A cylinder ring is about two pixels wide once 84 of them are drawn inside a
 * disk, so the outer circumference is around 1,600 pixels and the inner nearer
 * 550. 512 buckets is therefore already finer than the pixels available to draw
 * them in at every radius, and asking for more would only ship detail that lands
 * on top of itself. It is also what keeps a track's surface inside one broadcast
 * line: 512 nibbles is 512 characters against the 2,048 byte limit.
 */
#define DINFO_BUCKETS 512

/**
 * @brief Flux interval histogram bins sent per track.
 *
 * The capture keeps 256, one per timer tick, which is the resolution the density
 * thresholds are tuned against. Four of those to a bin is still finer than the
 * three peaks the chart exists to show - 2, 3 and 4us cells - and it turns a
 * 1.1KB line into a 128 byte one, which matters when it is sent 168 times.
 */
#define DINFO_HIST_BINS 64

//! Flags in the dinfoTrack message.
#define DINFO_ALIGNED 0x01 //!< capture started on an index pulse, so angles mean something
#define DINFO_HD 0x02      //!< read at HD density
#define DINFO_FROMFILE 0x04 //!< replayed from an SCP image rather than read from the drive

/**
 * @brief Amiga track analyser: what is actually on a disk, track by track.
 *
 * Answers a different question from the copy paths. They want a disk read
 * correctly and treat anything else as a failure; this wants to describe whatever
 * is there, including the tracks a copier gives up on. A protected track with the
 * wrong cylinder in its headers, a track with no sectors at all, a disk with 84
 * cylinders instead of 80 - all of those are the output, not an error.
 *
 * That is why the per track work is XCopyFloppy::surveyCapture() and
 * censusTrack() rather than readTrack(): one pass, no retries, no threshold
 * adjustment between tracks, and a header naming the wrong cylinder reported
 * rather than failed.
 *
 * A disk and an SCP image go through the same census. surveyScp() replays the
 * file's flux through XCopyFloppy::feedFluxSample(), which is the body of the
 * capture interrupt, so both sources fill the same stream[] and sectorTable[] and
 * are then asked the same question. The browser cannot tell them apart, and there
 * is no second decoder to drift out of step with the first.
 */
class XCopyDiskInfo
{
  public:
    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp, XCopyFloppy *floppy);

    /**
     * @brief Survey the disk in the drive.
     *
     * @param startCyl first cylinder, 0 based
     * @param endCyl last cylinder, inclusive. Up to MAX_CYLINDERS-1: the raw track
     *        paths have no 80 cylinder limit, only the ADF shaped ones do.
     * @param side 0, 1, or -1 for both
     */
    void surveyDisk(uint8_t startCyl, uint8_t endCyl, int8_t side);

    /**
     * @brief Survey an SCP image on the memory card.
     *
     * The drive is not touched and need not hold a disk.
     */
    void surveyScp(const String &path, uint8_t startCyl, uint8_t endCyl, int8_t side);

    void cancelOperation() { _cancelOperation = true; }

  private:
    void emitBegin(const char *source, const String &name, uint8_t startCyl,
                   uint8_t endCyl, int8_t side, bool hd, bool wrprot, uint16_t rotMs);
    void emitTrack(uint8_t cylinder, uint8_t head, const CalibrationResult &result,
                   unsigned long revBytes, uint8_t flags);

    /*
       Sectors the density says a track should hold, 11 or 22.

       Not CalibrationResult::sectorCount, which during a survey is the widened
       census bound and not a claim about the format. Reporting the bound would
       have every clean DD track come back as eleven of twenty two, i.e. eleven
       bad, which is the opposite of what it found.
    */
    uint8_t _expected = 11;
    void emitProfile(uint8_t cylinder, uint8_t head, unsigned long revBytes);
    void emitHist(uint8_t cylinder, uint8_t head);
    void emitEnd(uint16_t tracks, uint16_t good, uint16_t bad);

    //! Cell bytes in one revolution, from the measured rotation period.
    unsigned long revolutionBytes(uint16_t rotMs, bool hd) const;

    //! Turn a byte position in the capture into 0..4095 of a revolution.
    uint16_t angleOf(unsigned long bytePos, unsigned long revBytes) const;

    XCopyGraphics *_graphics = nullptr;
    XCopyAudio *_audio = nullptr;
    XCopyESP8266 *_esp = nullptr;
    XCopyFloppy *_floppy = nullptr;

    /*
       Set from ISR_CANCEL by way of XCopy::cancelOperation() and polled by the
       survey loops. volatile for the reason XCopyDisk's and XCopy's are: the
       interrupt is the only writer and the loop the only reader, and with LTO the
       whole program is one translation unit, so nothing else stops the compiler
       deciding the loop's copy can never change and hoisting the load out.
    */
    volatile bool _cancelOperation = false;
};

#endif // XCOPYDISKINFO_H
