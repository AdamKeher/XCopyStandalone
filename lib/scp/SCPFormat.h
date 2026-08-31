#ifndef SCPFORMAT_H
#define SCPFORMAT_H

/*
   The SuperCard Pro (.scp) flux image layout.

   Vendored from keirf/Disk-Utilities (public domain, Unlicense) - see README.md for
   exactly which pieces are copied and which are ours. Constants upstream does not
   define come from the SuperCard Pro Image File Specification v2.5:

     https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt

   File layout:

     0x000  scp_disk_header                 16 bytes
     0x010  uint32_t offsets[168]           track offset table, little endian.
                                            offsets[n] is the absolute file offset of
                                            track n's scp_track_header, or 0 if that
                                            track holds no flux.
     0x2b0  first scp_track_header          "TRK" + track number, then one
                                            scp_rev_entry per revolution
            flux samples                    big endian uint16, 25ns units
            ... more tracks ...
            footer strings, scp_footer      only when SCP_FLAG_FOOTER is set

   EVERY longword in the file is little endian EXCEPT the flux samples, which are big
   endian. That single asymmetry is the most common way to produce a file that no tool
   will open, so the accessors below are spelled out rather than left to memcpy.
*/

#include <stdint.h>
#include <string.h>

// --- header ------------------------------------------------------------------------

//! Number of tracks the offset table addresses. Track number is cylinder * 2 + head,
//! so this is 84 cylinders double sided - the format's hard limit.
#define SCP_MAX_TRACKS 168

//! File offset of the track offset table, and therefore the end of the disk header.
#define SCP_OFFSET_TABLE 0x10

//! File offset of the first track data header, with a full length offset table.
#define SCP_FIRST_TDH (SCP_OFFSET_TABLE + (SCP_MAX_TRACKS * 4))

//! Flux sample resolution. The unit every value in the file is expressed in.
#define SCK_NS_PER_TICK 25u

//! Revolutions a track data header can hold. Spec limit, not ours.
#define SCP_MAX_REVS 5

/*
   Verbatim from libdisk/container/scp.c.

   Field order and types are the byte layout, so nothing here may be reordered or
   widened. Packed because the file format, not the compiler, decides the offsets -
   natural alignment happens to agree on ARM, but saying so is free.
*/
struct __attribute__((packed)) scp_disk_header
{
    uint8_t sig[3];            //!< "SCP"
    uint8_t version;           //!< version << 4 | revision, 0 when a footer is present
    uint8_t disk_type;         //!< manufacturer << 4 | machine
    uint8_t nr_revolutions;    //!< revolutions stored per track, 1..5
    uint8_t start_track;       //!< first populated track number
    uint8_t end_track;         //!< last populated track number, inclusive
    uint8_t flags;             //!< SCP_FLAG_*
    uint8_t cell_width;        //!< bits per sample, 0 means the default 16
    uint16_t reserved;         //!< heads (spec v2.5) then resolution, see below
    uint32_t checksum;         //!< sum of every byte from 0x10 to EOF, little endian
};

/*
   Upstream calls bytes 0x0a and 0x0b "reserved"; the v2.5 spec names them. They are
   split out rather than written through the uint16 so the intent is legible at the
   call site.
*/
#define SCP_HEADS_BOTH 0  //!< 0x0a: image holds both sides
#define SCP_HEADS_SIDE0 1 //!< 0x0a: side 0 only
#define SCP_HEADS_SIDE1 2 //!< 0x0a: side 1 only
#define SCP_RESOLUTION_25NS 0 //!< 0x0b: multiplier of 25ns; 0 is the base resolution

//! Disk type. Verbatim from libdisk/container/scp.c.
#define SCP_DISKTYPE_AMIGA 4

/*
   FLAGS, byte 0x08. Bit numbers verbatim from libdisk/container/scp.c; the two the
   spec defines that upstream does not use are added from v2.5.
*/
#define SCP_FLAG_INDEX_CUED 0  //!< flux begins at the index pulse
#define SCP_FLAG_96TPI 1       //!< 96 TPI drive, clear means 48 TPI
#define SCP_FLAG_360RPM 2      //!< 360 RPM drive, clear means 300 RPM
#define SCP_FLAG_NORMALIZED 3  //!< flux was normalised, clear means original
#define SCP_FLAG_WRITABLE 4    //!< image is read/write capable
#define SCP_FLAG_FOOTER 5      //!< extension footer present
#define SCP_FLAG_EXTENDED 6    //!< v2.5: non floppy media
#define SCP_FLAG_NOT_SCP 7     //!< v2.5: created by something other than SCP hardware

// --- track data header -------------------------------------------------------------

/*
   Verbatim from libdisk/container/scp.c, which only ever writes one revolution.

   The trailing three longwords are revolution 0's scp_rev_entry - the struct is the
   single revolution case of a header that grows by 12 bytes per extra revolution. Use
   SCP_TDH_SIZE() rather than sizeof() whenever more than one revolution is stored.
*/
struct __attribute__((packed)) scp_track_header
{
    uint8_t sig[3];        //!< "TRK"
    uint8_t tracknr;       //!< cylinder * 2 + head
    uint32_t duration;     //!< revolution length in 25ns ticks, index to index
    uint32_t nr_samples;   //!< flux samples in this revolution, 0x0000 entries included
    uint32_t offset;       //!< sample data, RELATIVE TO THE START OF THIS HEADER
};

//! One revolution's entry in a multi revolution track data header.
struct __attribute__((packed)) scp_rev_entry
{
    uint32_t duration;   //!< revolution length in 25ns ticks, index to index
    uint32_t nr_samples; //!< flux samples in this revolution
    uint32_t offset;     //!< sample data, relative to the start of the track header
};

//! Size of a track data header holding @p revs revolutions.
#define SCP_TDH_SIZE(revs) (4 + (12 * (unsigned)(revs)))

// --- footer ------------------------------------------------------------------------

//! Verbatim from libdisk/container/scp.c.
struct __attribute__((packed)) scp_footer
{
    uint32_t manufacturer_offset; //!< file offset of a string, 0 if absent
    uint32_t model_offset;
    uint32_t serial_offset;
    uint32_t creator_offset;
    uint32_t application_offset;
    uint32_t comments_offset;
    uint64_t creation_time;     //!< seconds since 1970-01-01 UTC
    uint64_t modification_time;
    uint8_t application_version; //!< version << 4 | subversion
    uint8_t hardware_version;
    uint8_t firmware_version;
    uint8_t format_revision;     //!< spec revision written to, version << 4 | revision
    uint8_t sig[4];              //!< "FPCS" - the footer is found by scanning back
};

//! Specification revision this writer targets, v2.5. Upstream writes 0x16 for v1.6.
#define SCP_FORMAT_REVISION 0x25

/*
   Footer strings are a 16-bit little endian byte count followed by UTF-8, with no
   terminator counted. Spec v2.5.
*/

// --- endian helpers ----------------------------------------------------------------

/*
   Teensy is little endian, so the LE helpers compile away entirely. They exist so the
   file layout is stated at every call site rather than assumed, and so this header
   still means what it says if it is ever built for a native test harness.
*/
static inline void scpPutLE16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static inline void scpPutLE32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void scpPutLE64(uint8_t *p, uint64_t v)
{
    scpPutLE32(p, (uint32_t)v);
    scpPutLE32(p + 4, (uint32_t)(v >> 32));
}

static inline uint32_t scpGetLE32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

//! Flux samples, and only flux samples, are big endian.
static inline void scpPutBE16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static inline uint16_t scpGetBE16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

// --- checksum ----------------------------------------------------------------------

/*
   The header checksum is a plain 32-bit sum of every byte from 0x10 to EOF, taken
   verbatim from checksum_and_write() in libdisk/container/scp.c. It wraps, and that is
   the definition, not an oversight.
*/
static inline uint32_t scpChecksum(uint32_t csum, const uint8_t *data, uint32_t len)
{
    while (len--)
        csum += *data++;
    return csum;
}

// --- flux sample codec -------------------------------------------------------------

/*
   A sample of 0x0000 means "no transition for a whole 65536 ticks, keep reading", so a
   long gap is a run of zeros followed by the remainder. Every 0x0000 still counts
   toward nr_samples.

   Emission is the overflow loop from emit() in libdisk/container/scp.c; accumulation is
   the matching loop from scp_next_flux() in libdisk/stream/supercard_scp.c.
*/

//! Number of big endian samples scpEmitFlux() will write for an interval of @p ticks.
static inline uint32_t scpFluxSampleCount(uint32_t ticks)
{
    return (ticks / 0x10000u) + 1;
}

/*
   Writes @p ticks as one or more big endian samples into @p dst, returning the number
   of samples written.

   The final sample is "cell ?: 1" as upstream has it: an interval that lands exactly on
   a multiple of 65536 would otherwise end in a 0x0000 that reads back as a further
   overflow. Losing 25ns to avoid that is the format's own convention.
*/
static inline uint32_t scpEmitFlux(uint8_t *dst, uint32_t ticks)
{
    uint32_t written = 0;

    while (ticks >= 0x10000u)
    {
        scpPutBE16(dst + (written * 2), 0);
        written++;
        ticks -= 0x10000u;
    }

    scpPutBE16(dst + (written * 2), (uint16_t)(ticks ? ticks : 1));
    return written + 1;
}

/*
   The duration a revolution's samples add up to. Mirrors upstream's

       thdr.duration += dat[i] ?: 0x10000u;

   so a 0x0000 sample contributes a full 65536 rather than nothing.
*/
static inline uint32_t scpFluxDuration(uint32_t duration, uint16_t sample)
{
    return duration + (sample ? sample : 0x10000u);
}

#endif // SCPFORMAT_H
