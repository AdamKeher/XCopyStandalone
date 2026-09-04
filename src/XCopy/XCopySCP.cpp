#include "XCopySCP.h"
#include <TimeLib.h>

// The vendored structs are the file layout, so a packing surprise here would produce
// an image no tool can open, silently. Catch it at compile time instead.
static_assert(sizeof(scp_disk_header) == 16, "scp_disk_header must be 16 bytes");
static_assert(sizeof(scp_track_header) == 16, "scp_track_header must be 16 bytes");
static_assert(sizeof(scp_footer) == 48, "scp_footer must be 48 bytes");

//! Name recorded in the image footer, so a file can be traced back to what made it.
static const char SCP_APP_NAME[] = "XCopy Standalone";

static uint32_t gcd32(uint32_t a, uint32_t b)
{
    while (b)
    {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/*
   Buffered write. Everything written through here after the disk header is part of
   the checksum, which the format defines as a plain byte sum from 0x10 to EOF.
*/
bool XCopySCPWriter::put(const uint8_t *data, uint16_t len)
{
    if (_failed)
        return false;

    while (len)
    {
        uint16_t room = sizeof(_block) - _blockLen;
        uint16_t take = (len < room) ? len : room;

        memcpy(&_block[_blockLen], data, take);
        _blockLen += take;
        _filePos += take;
        _csum = scpChecksum(_csum, data, take);
        data += take;
        len -= take;

        if (_blockLen == sizeof(_block) && !flushBlock())
            return false;
    }

    return true;
}

bool XCopySCPWriter::flushBlock()
{
    if (_blockLen == 0)
        return true;

    if (_file->write(_block, _blockLen) != (int)_blockLen)
    {
        _failed = true;
        return false;
    }

    _blockLen = 0;
    return true;
}

/*
   Overwrites bytes already committed to the file, then returns to the end.

   Used only for the fixed size headers whose contents are not known until the data
   after them has been written. Those bytes are checksummed here, when their final
   value is known, and deliberately not when the placeholder was written - the
   checksum has to describe the file as it ends up, not as it was drafted.

   Only ever called between tracks, never while flux is being captured, so the flush
   this forces cannot cost samples.
*/
bool XCopySCPWriter::patch(uint32_t offset, const uint8_t *data, uint16_t len)
{
    if (_failed)
        return false;

    if (!flushBlock())
        return false;

    if (!_file->seekSet(offset) || _file->write(data, len) != (int)len)
    {
        _failed = true;
        return false;
    }

    _csum = scpChecksum(_csum, data, len);

    if (!_file->seekSet(_filePos))
    {
        _failed = true;
        return false;
    }

    return true;
}

/*
   Timer ticks to the format's 25ns units.

   ticks * (1e9 * prescale / F_BUS) / 25, rearranged to stay in integers:

       units = ticks * (40000 * prescale) / (F_BUS / 1000)

   At the stock 48MHz bus that is 80000/48000 for DD and 40000/48000 for HD, which
   reduce to 5/3 and 5/6. The reduction matters: the multiply has to stay inside 32
   bits for a 0xffff sample, and 0xffff * 5 does while 0xffff * 80000 does not.
*/
uint32_t XCopySCPWriter::toScpUnits(uint32_t ticks) const
{
    return (ticks * _scaleNum) / _scaleDen;
}

bool XCopySCPWriter::begin(File *file, uint8_t startTrack, uint8_t endTrack,
                           uint8_t revolutions, bool hd)
{
    if (file == NULL || revolutions < 1 || revolutions > SCP_MAX_REVS ||
        endTrack >= SCP_MAX_TRACKS || startTrack > endTrack)
        return false;

    _file = file;
    _failed = false;
    _revolutions = revolutions;
    _startTrack = startTrack;
    _endTrack = endTrack;
    _blockLen = 0;
    _filePos = 0;
    _csum = 0;
    _trackBytes = 0;
    _anyWritten = false;

    // HD reads run the prescaler at /1 and DD at /2, so an HD tick is half as long
    const uint32_t prescale = hd ? 1 : 2;
    uint32_t num = 40000UL * prescale;
    uint32_t den = (uint32_t)(F_BUS / 1000UL);
    uint32_t g = gcd32(num, den);
    _scaleNum = num / g;
    _scaleDen = den / g;

    // 0xffff * _scaleNum has to fit in 32 bits or a long interval silently wraps
    if (_scaleNum > (0xFFFFFFFFUL / 0xFFFFUL))
        return false;

    /*
       INDEX because beginFluxCapture() arms on an index edge and every stored
       revolution is genuinely index to index. 96TPI because this is a 3.5" drive.
       NOT_SCP because it plainly was not made by SuperCard Pro hardware, and saying
       so is what lets a reader know the timings come from someone else's clock.
       FOOTER because end() writes one.

       WRITABLE is deliberately clear: this is a capture, not a scratch image, and
       claiming read/write would invite a tool to rewrite it in place.
    */
    _flags = (1u << SCP_FLAG_INDEX_CUED) | (1u << SCP_FLAG_96TPI) |
             (1u << SCP_FLAG_FOOTER) | (1u << SCP_FLAG_NOT_SCP);

    scp_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.sig, "SCP", 3);
    hdr.version = 0; // zero when a footer carries the real version, per the spec
    hdr.disk_type = SCP_DISKTYPE_AMIGA;
    hdr.nr_revolutions = revolutions;
    hdr.start_track = startTrack;
    hdr.end_track = endTrack;
    hdr.flags = _flags;
    hdr.cell_width = 0; // 0 means the default 16 bits per sample
    // bytes 0x0a and 0x0b, which upstream calls reserved and the v2.5 spec names
    hdr.reserved = (uint16_t)(SCP_HEADS_BOTH | (SCP_RESOLUTION_25NS << 8));
    hdr.checksum = 0; // patched by end()

    // The header is not part of its own checksum, so it goes straight out rather
    // than through put().
    if (_file->write((const uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr))
    {
        _failed = true;
        return false;
    }
    _filePos = sizeof(hdr);

    /*
       Track offset table placeholder. Entries are patched in as each track completes,
       so these zeros are not checksummed here - only the values that survive into the
       finished file are, and for a track that is never written that value is zero
       anyway, which adds nothing.
    */
    uint8_t zeros[64];
    memset(zeros, 0, sizeof(zeros));
    uint32_t remaining = SCP_MAX_TRACKS * 4;
    while (remaining)
    {
        uint16_t take = (remaining < sizeof(zeros)) ? remaining : sizeof(zeros);
        if (_file->write(zeros, take) != (int)take)
        {
            _failed = true;
            return false;
        }
        _filePos += take;
        remaining -= take;
    }

    return true;
}

bool XCopySCPWriter::beginTrack(uint8_t scpTrack)
{
    if (_failed)
        return false;

    _curTrack = scpTrack;
    _trackOffset = _filePos;
    _trackCsum = _csum;
    _trackBytes = 0;
    _curRev = 0;
    _emitted = 0;

    for (uint8_t i = 0; i < SCP_MAX_REVS; i++)
    {
        _revDuration[i] = 0;
        _revSamples[i] = 0;
        _revOffset[i] = 0;
    }

    // Placeholder track data header, patched by endTrack(). Written unchecksummed for
    // the same reason as the offset table.
    uint8_t tdh[SCP_TDH_SIZE(SCP_MAX_REVS)];
    uint16_t tdhSize = SCP_TDH_SIZE(_revolutions);
    memset(tdh, 0, sizeof(tdh));

    if (!flushBlock())
        return false;

    if (_file->write(tdh, tdhSize) != (int)tdhSize)
    {
        _failed = true;
        return false;
    }
    _filePos += tdhSize;

    return true;
}

bool XCopySCPWriter::abortTrack()
{
    if (_failed)
        return false;

    // The buffered block belongs to the track being discarded, so it is dropped
    // rather than flushed. Everything after _trackOffset is then dead, and the next
    // beginTrack() writes over it.
    _blockLen = 0;
    _filePos = _trackOffset;
    _csum = _trackCsum;
    _trackBytes = 0;
    _curRev = 0;
    _emitted = 0;

    if (!_file->seekSet(_filePos))
    {
        _failed = true;
        return false;
    }

    return true;
}

bool XCopySCPWriter::beginRevolution()
{
    if (_failed || _curRev >= _revolutions)
        return false;

    // data offset is relative to the start of the track data header, not the file
    _revOffset[_curRev] = _filePos - _trackOffset;
    _emitted = 0;
    return true;
}

bool XCopySCPWriter::writeFlux(const uint16_t *ticks, size_t count)
{
    if (_failed)
        return false;

    // Worst case a single tick value becomes an overflow entry plus a remainder, so
    // two samples, four bytes.
    uint8_t out[16];

    while (count--)
    {
        uint32_t units = toScpUnits(*ticks++);
        uint32_t emitted = scpEmitFlux(out, units);

        if (!put(out, (uint16_t)(emitted * 2)))
            return false;

        _emitted += emitted;
        _trackBytes += emitted * 2;
    }

    return true;
}

bool XCopySCPWriter::endRevolution(uint32_t durationTicks)
{
    if (_failed || _curRev >= _revolutions)
        return false;

    _revDuration[_curRev] = toScpUnits(durationTicks);
    _revSamples[_curRev] = _emitted;
    _curRev++;
    _emitted = 0;
    return true;
}

bool XCopySCPWriter::endTrack()
{
    if (_failed)
        return false;

    uint8_t tdh[SCP_TDH_SIZE(SCP_MAX_REVS)];
    uint16_t tdhSize = SCP_TDH_SIZE(_revolutions);

    memset(tdh, 0, sizeof(tdh));
    tdh[0] = 'T';
    tdh[1] = 'R';
    tdh[2] = 'K';
    tdh[3] = _curTrack;

    for (uint8_t r = 0; r < _revolutions; r++)
    {
        uint8_t *entry = &tdh[4 + (r * 12)];
        scpPutLE32(entry + 0, _revDuration[r]);
        scpPutLE32(entry + 4, _revSamples[r]);
        scpPutLE32(entry + 8, _revOffset[r]);
    }

    if (!patch(_trackOffset, tdh, tdhSize))
        return false;

    // and point the offset table at it
    uint8_t entry[4];
    scpPutLE32(entry, _trackOffset);
    if (!patch(SCP_OFFSET_TABLE + (_curTrack * 4), entry, sizeof(entry)))
        return false;

    if (!_anyWritten || _curTrack < _firstWritten) _firstWritten = _curTrack;
    if (!_anyWritten || _curTrack > _lastWritten) _lastWritten = _curTrack;
    _anyWritten = true;

    return true;
}

bool XCopySCPWriter::end()
{
    if (_failed)
        return false;

    /*
       Footer: a length prefixed application name, then the footer struct itself. The
       string offsets point at the length word, not the text. Anything the footer does
       not name stays zero, which readers take as absent.
    */
    uint32_t appOffset = _filePos;
    uint16_t nameLen = (uint16_t)strlen(SCP_APP_NAME);
    uint8_t lenBytes[2];
    scpPutLE16(lenBytes, nameLen);

    if (!put(lenBytes, sizeof(lenBytes)))
        return false;
    if (!put((const uint8_t *)SCP_APP_NAME, nameLen))
        return false;

    scp_footer ftr;
    memset(&ftr, 0, sizeof(ftr));
    memcpy(ftr.sig, "FPCS", 4);
    ftr.application_offset = appOffset;
    ftr.creation_time = (uint64_t)now();
    ftr.modification_time = ftr.creation_time;
    ftr.application_version = 0x10;
    ftr.format_revision = SCP_FORMAT_REVISION;

    if (!put((const uint8_t *)&ftr, sizeof(ftr)))
        return false;

    if (!flushBlock())
        return false;

    /*
       Drop anything past the footer.

       A retried or cancelled track rewinds _filePos, but rewinding does not shorten
       the file - the bytes of the abandoned attempt are still there, and the checksum
       is defined over everything from 0x10 to EOF. Without this the sum would cover
       data the image does not reference and no reader could reproduce it.
    */
    if (!_file->truncate(_filePos))
    {
        _failed = true;
        return false;
    }

    /*
       Finally the header, now that the checksum covers every byte from 0x10 to here.
       Rewritten whole rather than patched, because the header is the one part of the
       file the checksum does not describe and put()/patch() would fold it in.
    */
    scp_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.sig, "SCP", 3);
    hdr.disk_type = SCP_DISKTYPE_AMIGA;
    hdr.nr_revolutions = _revolutions;
    // The range that is actually in the file, not the one that was asked for: a
    // cancelled capture stops early, and saying otherwise sends a reader looking
    // for tracks that are not there.
    hdr.start_track = _anyWritten ? _firstWritten : _startTrack;
    hdr.end_track = _anyWritten ? _lastWritten : _endTrack;
    hdr.flags = _flags;
    hdr.reserved = (uint16_t)(SCP_HEADS_BOTH | (SCP_RESOLUTION_25NS << 8));
    hdr.checksum = _csum;

    if (!_file->seekSet(0) ||
        _file->write((const uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr))
    {
        _failed = true;
        return false;
    }

    _file->flush();
    return true;
}

// --- reader ------------------------------------------------------------------------

/*
   Read len bytes from an absolute file offset.

   Every read below is a seek and a read of a fixed size struct or a run of samples,
   and all of them have to be bounds checked against the file: an image truncated
   mid-track has an offset table that still points confidently at data that is not
   there, and following it would hand the decoder whatever SdFat left in the buffer.
*/
bool XCopySCPReader::readAt(uint32_t offset, uint8_t *dst, uint16_t len)
{
    if (_file == NULL || offset + len > _fileSize)
        return false;
    if (!_file->seekSet(offset))
        return false;
    return _file->read(dst, len) == (int)len;
}

bool XCopySCPReader::begin(File *file)
{
    _file = file;
    _fileSize = 0;
    _fluxRemaining = 0;

    if (_file == NULL)
        return false;

    _fileSize = _file->fileSize();
    if (_fileSize < SCP_FIRST_TDH)
        return false;

    uint8_t hdr[sizeof(scp_disk_header)];
    if (!readAt(0, hdr, sizeof(hdr)))
        return false;

    if (hdr[0] != 'S' || hdr[1] != 'C' || hdr[2] != 'P')
        return false;

    _diskType = hdr[4];
    _revolutions = hdr[5];
    _startTrack = hdr[6];
    _endTrack = hdr[7];
    _flags = hdr[8];
    _heads = hdr[10];

    /*
       cell_width at 0x09 is bits per sample, and 0 means the default 16. Anything
       else is a format this reader does not speak - the samples would not be 16 bit
       big endian - so say so here rather than decode noise.
    */
    if (hdr[9] != 0 && hdr[9] != 16)
        return false;

    if (_revolutions < 1 || _revolutions > SCP_MAX_REVS)
        return false;
    if (_endTrack >= SCP_MAX_TRACKS || _startTrack > _endTrack)
        return false;

    return true;
}

bool XCopySCPReader::trackOffset(uint8_t scpTrack, uint32_t &offset)
{
    if (scpTrack >= SCP_MAX_TRACKS)
        return false;

    uint8_t entry[4];
    if (!readAt(SCP_OFFSET_TABLE + (scpTrack * 4), entry, 4))
        return false;

    offset = scpGetLE32(entry);
    // Zero is the format's "no flux for this track", not offset zero.
    return offset != 0 && offset < _fileSize;
}

bool XCopySCPReader::trackPresent(uint8_t scpTrack)
{
    uint32_t offset;
    return trackOffset(scpTrack, offset);
}

bool XCopySCPReader::beginFlux(uint8_t scpTrack, uint8_t rev)
{
    _fluxRemaining = 0;
    _revTicks = 0;
    _revSamples = 0;

    if (rev >= _revolutions)
        return false;

    uint32_t base;
    if (!trackOffset(scpTrack, base))
        return false;

    uint8_t sig[4];
    if (!readAt(base, sig, 4))
        return false;
    if (sig[0] != 'T' || sig[1] != 'R' || sig[2] != 'K')
        return false;

    /*
       The track number in the header is checked against the one the offset table
       was indexed by. They disagree only in a corrupt or hand assembled image, and
       trusting the table over the header would analyse one track's flux under
       another track's name - which looks exactly like a badly aligned drive.
    */
    if (sig[3] != scpTrack)
        return false;

    uint8_t entry[sizeof(scp_rev_entry)];
    if (!readAt(base + 4 + (rev * sizeof(scp_rev_entry)), entry, sizeof(entry)))
        return false;

    _revTicks = scpGetLE32(entry);
    _revSamples = scpGetLE32(entry + 4);
    // The sample offset is relative to the start of the track header, not the file.
    _fluxPos = base + scpGetLE32(entry + 8);
    _fluxRemaining = _revSamples;

    /*
       Bounded before the multiply, not after. nr_samples is a 32 bit field straight
       out of the file, so a corrupt one times two wraps and the range check passes
       on a track that runs off the end of the image.
    */
    if (_revSamples == 0 || _revSamples > (_fileSize / 2))
    {
        _fluxRemaining = 0;
        return false;
    }
    if (_fluxPos + (_revSamples * 2) > _fileSize)
    {
        _fluxRemaining = 0;
        return false;
    }
    return true;
}

int XCopySCPReader::readFlux(uint16_t *dst, int max)
{
    if (_fluxRemaining == 0 || max <= 0)
        return 0;

    int want = (uint32_t)max < _fluxRemaining ? max : (int)_fluxRemaining;

    /*
       Read into the caller's buffer as bytes and byte swap in place, so a track
       needs no staging buffer of its own. Each entry is read and written at the
       same two bytes, so the pass has no direction to get wrong - but it does have
       to be a pass, because the file is big endian and only the flux samples are.
    */
    uint8_t *raw = (uint8_t *)dst;
    if (!readAt(_fluxPos, raw, (uint16_t)(want * 2)))
        return 0;

    for (int i = 0; i < want; i++)
        dst[i] = scpGetBE16(raw + (i * 2));

    _fluxPos += want * 2;
    _fluxRemaining -= want;
    return want;
}
