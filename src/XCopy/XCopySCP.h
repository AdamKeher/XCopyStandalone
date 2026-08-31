#ifndef XCOPYSCP_H
#define XCOPYSCP_H

#include <Arduino.h>
#include <SdFat.h>
#include "SCPFormat.h"

/**
 * @brief Writes a SuperCard Pro flux image, streaming, without buffering a track.
 *
 * A DD revolution is around 37,700 flux transitions - 75KB of samples against 64KB of
 * RAM on the whole part - so nothing here may accumulate a track, let alone a disk.
 * The writer moves forward through the file with a single 512 byte block in hand and
 * seeks backwards only to patch fixed size headers once their contents are known.
 *
 * Call order:
 *
 *   begin()
 *     beginTrack(n)
 *       beginRevolution()  writeFlux()...  endRevolution(ticks)      x revolutions
 *     endTrack()
 *     ... more tracks, ascending ...
 *   end()
 *
 * writeFlux() takes raw FTM0 timer ticks straight out of XCopyFloppy's capture ring
 * and does the conversion to the format's 25ns units itself, because a tick is
 * 41.67ns on a DD read and 20.83ns on an HD one and neither is a whole number of them.
 *
 * A single raw sample can therefore emit more than one SCP sample: 0xffff ticks at DD
 * is 109,225 units, past what a 16 bit field holds, so it becomes an overflow entry
 * plus a remainder. That is why the revolution sample count reported in the track data
 * header is counted here as samples are emitted, and is not the number of transitions
 * the drive saw.
 */
class XCopySCPWriter
{
public:
    /**
     * @brief Starts an image.
     *
     * @param file      open and positioned at 0. Not owned - the caller closes it.
     * @param startTrack first SCP track number that will be written (cylinder*2+head)
     * @param endTrack  last SCP track number, inclusive
     * @param revolutions revolutions stored per track, 1..SCP_MAX_REVS
     * @param hd        true if the capture ran with the HD prescaler, which halves the
     *                  tick and so changes the conversion to 25ns units
     */
    bool begin(File *file, uint8_t startTrack, uint8_t endTrack, uint8_t revolutions, bool hd);

    bool beginTrack(uint8_t scpTrack);
    /**
     * @brief Discards the track in progress and rewinds the file to where it started.
     *
     * A capture that overran the ring has to be retried, and without this the partial
     * flux would stay in the image as unreferenced dead weight - a marginal disk
     * needing retries on many tracks could easily double the file size. The checksum
     * is rolled back with it, so the image still describes only what it kept.
     */
    bool abortTrack();
    bool beginRevolution();
    //! @param ticks raw capture ring samples, in timer ticks
    bool writeFlux(const uint16_t *ticks, size_t count);
    //! @param durationTicks index to index length of this revolution, in timer ticks
    bool endRevolution(uint32_t durationTicks);
    bool endTrack();

    //! Writes the footer, patches the offset table and header, flushes.
    bool end();

    //! Flux bytes written for the track in progress, for progress reporting.
    uint32_t trackBytes() const { return _trackBytes; }
    //! True once any write has failed. The image is then incomplete and must be binned.
    bool failed() const { return _failed; }

private:
    bool put(const uint8_t *data, uint16_t len);
    bool flushBlock();
    bool patch(uint32_t offset, const uint8_t *data, uint16_t len);
    uint32_t toScpUnits(uint32_t ticks) const;

    File *_file = NULL;
    bool _failed = false;

    uint8_t _revolutions = 1;
    uint8_t _startTrack = 0;
    uint8_t _endTrack = 0;
    // What actually made it into the image, which is not what was asked for when a
    // capture is cancelled or a track fails every retry.
    uint8_t _firstWritten = 0;
    uint8_t _lastWritten = 0;
    bool _anyWritten = false;
    uint8_t _flags = 0;

    // tick -> 25ns unit conversion, reduced so the multiply cannot overflow 32 bits
    uint32_t _scaleNum = 5;
    uint32_t _scaleDen = 3;

    uint8_t _block[512];
    uint16_t _blockLen = 0;
    uint32_t _filePos = 0; //!< bytes written plus bytes buffered
    uint32_t _csum = 0;    //!< running sum of every byte from 0x10 to EOF

    // current track
    uint32_t _trackOffset = 0;
    uint32_t _trackCsum = 0; //!< checksum as it stood before the track, for abortTrack()
    uint32_t _trackBytes = 0;
    uint8_t _curRev = 0;
    uint32_t _revDuration[SCP_MAX_REVS];
    uint32_t _revSamples[SCP_MAX_REVS];
    uint32_t _revOffset[SCP_MAX_REVS];
    uint8_t _curTrack = 0;
    uint32_t _emitted = 0; //!< SCP samples emitted in the revolution in progress
};

#endif // XCOPYSCP_H
