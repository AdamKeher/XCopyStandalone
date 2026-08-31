/* XCopyLiveProtocol.h - wire contract for the XCopy Standalone live-drive link.
 *
 * ONE FILE, TWO REPOSITORIES. This header must stay byte-identical in:
 *
 *   host     : patches/floppybridge/XCopyLiveProtocol.h   (amiga_disk_manager)
 *   firmware : shared/XCopyLiveProtocol.h                 (XCopyStandalone)
 *
 * Same rule as build_puae.ps1 / build_puae.sh: a change here is a change in both
 * trees, in the same commit. Bump XCL_PROTOCOL_VERSION whenever the layout or the
 * meaning of any field changes; the host refuses a device whose version it does
 * not know rather than misreading it.
 *
 * WHY THIS PROTOCOL EXISTS
 * ------------------------
 * An Amiga disk controller reads bit cells off a platter continuously, at real
 * time. Every USB floppy device we have tried makes a read a *bounded request*, so
 * moving the head means stop, drain, seek, restart, and re-derive where the index
 * was - which costs 445-466 ms on a Greaseweazle against a 203 ms physical floor
 * (docs/floppy.md 12b, 12n.8, 12o).
 *
 * This protocol exists to make three things true, and everything else is
 * negotiable:
 *
 *   1. The stream starts once and NEVER stops.
 *   2. Seeking is a command sent INTO the running stream.
 *   3. Every byte carries its absolute rotational position, so the host never has
 *      to reconstruct one.
 *
 * Point 3 is why XclRecordHeader::index is a free-running counter that does not
 * reset for a seek, a side change, an index pulse, or a mode switch. The host maps
 * a cell to an angle as (index - cellIndexOfLastIndexPulse), and that is exact -
 * no correlation search, no rotation, no fractional-cell slip at the join.
 *
 * Byte order is LITTLE ENDIAN throughout (both ends are little endian; there is no
 * swapping anywhere).
 */

#ifndef XCOPY_LIVE_PROTOCOL_H
#define XCOPY_LIVE_PROTOCOL_H

#include <stdint.h>

#define XCL_PROTOCOL_VERSION 1

/* ------------------------------------------------------------------------ */
/* Entering and leaving binary mode                                          */
/* ------------------------------------------------------------------------ */

/* The device boots into its ordinary ASCII console. The host types this one
   command, followed by CR, to switch that USB session into binary mode. Nothing
   else about the console changes, and XCL_CMD_BYE returns to it. */
#define XCL_ENTER_COMMAND "live"

/* First thing the device emits after entering binary mode, so a host that opened
   the port mid-session can tell binary framing has begun. */
#define XCL_BANNER "XCLIVE1"

/* ------------------------------------------------------------------------ */
/* Host -> device: fixed frames, at most 13 bytes on the wire                */
/* ------------------------------------------------------------------------ */

/*   0xA5 0x5A | cmd | len | payload[len] | crc8
 *
 * Kept small deliberately: the device parses these from the middle of a
 * high-rate outbound stream, so a frame must never straddle more than one poll.
 * crc8 covers cmd, len and payload.
 */

#define XCL_CMD_SYNC0 0xA5
#define XCL_CMD_SYNC1 0x5A
#define XCL_CMD_MAX_PAYLOAD 8
#define XCL_CMD_MAX_FRAME (2 + 1 + 1 + XCL_CMD_MAX_PAYLOAD + 1)

enum XclCommand {
    XCL_CMD_HELLO = 0x01,        /* -> XCL_REC_HELLO                             */
    XCL_CMD_MOTOR = 0x02,        /* p0: 0 = off, 1 = on                          */
    XCL_CMD_SEEK = 0x03,         /* p0: cylinder, p1: side. VALID MID-STREAM.    */
    XCL_CMD_RECAL = 0x04,        /* step out to TRK00                            */
    XCL_CMD_STREAM_START = 0x05, /* p0: XCL_MODE_*, p1: flags                    */
    XCL_CMD_STREAM_STOP = 0x06,  /* last byte out within 5 ms                    */
    XCL_CMD_STATUS = 0x07,       /* -> XCL_REC_STATUS. VALID MID-STREAM.         */
    XCL_CMD_CONFIG = 0x08,       /* p0: XCL_CFG_*, p1..p4: uint32 value          */
    XCL_CMD_SELFTEST = 0x09,     /* p0: 0 = off, 1 = on. Synthetic data, no disk */
    XCL_CMD_PING = 0x0A,         /* -> XCL_REC_ACK. Also the host keepalive.     */
    XCL_CMD_WRITE_TRACK = 0x0B,  /* RESERVED - replies XCL_REC_NAK/UNSUPPORTED   */
    XCL_CMD_BYE = 0x7F           /* leave binary mode, return to the console     */
};

/* Stream modes. Selectable at XCL_CMD_STREAM_START and switchable mid-stream
   without restarting the capture or resetting any counter. */
enum XclStreamMode {
    XCL_MODE_MFM = 0,  /* device-side PLL, packed bit cells. ~63 KB/s. Default. */
    XCL_MODE_FLUX = 1  /* raw tick deltas, host does the PLL. ~350-400 KB/s.    */
};

/* XCL_CMD_CONFIG keys. Every one of these defaults to the value the firmware
   already used before this protocol existed, so the console paths are unchanged
   unless the host asks for something different. */
enum XclConfigKey {
    XCL_CFG_STEP_INTERVAL_US = 0x01,  /* step-to-step. default 3000             */
    XCL_CFG_DIR_SETTLE_US = 0x02,     /* after a DIR change. default 20000      */
    XCL_CFG_SIDE_SETTLE_US = 0x03,    /* after a SIDE change. default 2000      */
    XCL_CFG_SEEK_SETTLE_US = 0x04,    /* after the last step. default 18000     */
    XCL_CFG_MOTOR_SPINUP_MS = 0x05,   /* default 600                            */
    XCL_CFG_CELL_NS = 0x06,           /* nominal bit cell. 2000 DD / 1000 HD    */
    XCL_CFG_PLL_PULL_PERMILLE = 0x07, /* window pull range. default 100 (10%)   */
    XCL_CFG_PLL_MODE = 0x08,          /* 0 = adaptive PLL, 1 = fixed window     */
    XCL_CFG_RECORD_CELLS = 0x09,      /* cells per MFM record. default 1024     */
    XCL_CFG_DENSITY = 0x0A,           /* 0 = auto, 1 = DD, 2 = HD               */
    XCL_CFG_WATCHDOG_MS = 0x0B        /* host silence -> motor off + leave      */
};

/* ------------------------------------------------------------------------ */
/* Device -> host: self-describing records                                   */
/* ------------------------------------------------------------------------ */

#define XCL_REC_SYNC0 0x9E
#define XCL_REC_SYNC1 0xC5

enum XclRecordType {
    XCL_REC_MFM = 0x01,    /* payload: packed cells, MSB first, (count+7)/8 bytes */
    XCL_REC_FLUX = 0x02,   /* payload: uint16 tick deltas, count*2 bytes          */
    XCL_REC_EVENT = 0x03,  /* payload: XclEventPayload                            */
    XCL_REC_HELLO = 0x04,  /* payload: XclHello                                   */
    XCL_REC_STATUS = 0x05, /* payload: XclStatus                                  */
    XCL_REC_ACK = 0x06,    /* payload: XclAck                                     */
    XCL_REC_NAK = 0x07,    /* payload: XclAck, .result = XCL_RESULT_*             */
    XCL_REC_TEST = 0x08    /* payload: synthetic bytes, XCL_CMD_SELFTEST          */
};

#pragma pack(push, 1)

/* Every record on the wire is: XclRecordHeader, then payload, then a uint16
   CRC-16/CCITT-FALSE over the header and payload together. The payload length is
   derived from type and count (xclPayloadBytes below) rather than sent, because
   the two data types are the hot path and eight bytes a record matters at ~500
   records a second. */
typedef struct {
    uint8_t sync0; /* XCL_REC_SYNC0                                              */
    uint8_t sync1; /* XCL_REC_SYNC1                                              */
    uint8_t type;  /* XclRecordType                                              */
    uint8_t flags; /* XCL_RF_*                                                   */

    /* MFM: bit cells in this record. FLUX: uint16 samples. Otherwise 0.         */
    uint16_t count;

    /* Device ticks this record spans. The host turns this into real bit-cell
       density and hands it to UAE as getMFMSpeed(), which is how long-track and
       speed-variation protections survive the trip. 0 when not applicable.      */
    uint16_t ticks;

    /* ABSOLUTE position of the first item, and the single most important field
       in this header. MFM: bit cells since the stream began. FLUX and events:
       device ticks since the stream began. It does NOT reset for a seek, a side
       change, an index, a mode switch or an overrun - only XCL_CMD_STREAM_START
       zeroes it. Wraps every ~2.4 hours of cells; the host handles that.        */
    uint32_t index;
} XclRecordHeader;

/* XclRecordHeader::flags */
#define XCL_RF_DISCONTINUITY 0x01  /* data before this record was dropped        */
#define XCL_RF_PLL_UNLOCKED 0x02   /* the device PLL was not locked for this span */
#define XCL_RF_TRACK_UNSTABLE 0x04 /* head was moving or settling                */

enum XclEvent {
    XCL_EV_INDEX = 0x01,        /* arg = ticks since the previous index pulse    */
    XCL_EV_TRACK_CHANGE = 0x02, /* arg = (cyl << 8) | side. Cells from HERE on
                                   are off the new surface. arg2 = seek ticks.   */
    XCL_EV_SEEK_BEGIN = 0x03,   /* arg = (cyl << 8) | side, the target           */
    XCL_EV_PLL_RESET = 0x04,    /* lock lost and re-acquired                     */
    XCL_EV_OVERRUN = 0x05,      /* arg = items dropped. See XCL_RF_DISCONTINUITY */
    XCL_EV_NO_FLUX = 0x06,      /* arg = ticks with no transition (unformatted)  */
    XCL_EV_DISK_CHANGE = 0x07,  /* arg = 1 media present, 0 absent               */
    XCL_EV_WRITE_PROTECT = 0x08,/* arg = 1 protected                             */
    XCL_EV_STREAM_START = 0x09, /* arg = XclStreamMode                           */
    XCL_EV_STREAM_STOP = 0x0A,
    XCL_EV_MOTOR = 0x0B,        /* arg = 1 running                               */
    XCL_EV_ERROR = 0x0C         /* arg = XCL_RESULT_*                            */
};

typedef struct {
    uint8_t event;  /* XclEvent                                                  */
    uint8_t cyl;    /* head position at the moment of the event                  */
    uint8_t side;
    uint8_t status; /* XCL_ST_*                                                  */
    uint32_t arg;
    uint32_t arg2;
    /* Cell index at the event. XclRecordHeader::index carries the tick counter
       for events, so both clocks are available and the host never has to guess
       which one an event is anchored to. */
    uint32_t cellIndex;
} XclEventPayload;

/* XclEventPayload::status and XclStatus::status */
#define XCL_ST_TRACK0 0x01
#define XCL_ST_WRPROT 0x02
#define XCL_ST_DISKCHANGE 0x04
#define XCL_ST_MOTOR 0x08
#define XCL_ST_STREAMING 0x10
#define XCL_ST_PLL_LOCKED 0x20
#define XCL_ST_MEDIA 0x40
#define XCL_ST_SEEKING 0x80

typedef struct {
    uint8_t protocolVersion;  /* XCL_PROTOCOL_VERSION                             */
    uint8_t capabilities;     /* XCL_CAP_*                                        */
    uint8_t maxCylinder;      /* highest cylinder the device will step to         */
    uint8_t driveCount;
    uint32_t tickHz;          /* device timer ticks per second - NEVER assume this */
    uint32_t ringBytes;       /* capture ring size, so the host can size its reads */
    uint16_t maxRecordBytes;  /* largest payload the device will emit             */
    uint16_t firmwareVersion; /* (major << 8) | minor                             */
    char ident[16];           /* NUL-padded, "XCopyStandalone"                    */
} XclHello;

/* XclHello::capabilities */
#define XCL_CAP_MFM 0x01
#define XCL_CAP_FLUX 0x02
#define XCL_CAP_DMA 0x04         /* capture is DMA driven, not per-transition ISR */
#define XCL_CAP_INBAND_SEEK 0x08 /* SEEK works mid-stream. The host REQUIRES this */
#define XCL_CAP_WRITE 0x10       /* not implemented in protocol version 1         */
#define XCL_CAP_SELFTEST 0x20
#define XCL_CAP_HD 0x40

/* The cell counter keeps advancing at the platter rate THROUGH a seek, so the
   host's index anchor survives a head move.

   The index is a mark on the platter, not on a track, and it keeps firing while
   the head moves. If the counter is continuous across the move then the angle of
   every cell afterwards is still (cellIndex - cellIndexOfLastIndexPulse), and the
   host can serve data off the new surface the instant it arrives - tens of
   milliseconds. Without it the anchor is worthless after a move and the host must
   wait for the next index, paying up to a whole revolution (~203 ms) on EVERY
   seek, which is most of what makes the Greaseweazle path slow.

   To provide it, keep clocking cells at the nominal rate while the head is in
   flight (free-run the PLL, emit whatever the head gives - it is garbage and the
   host discards it by cell index anyway). What must not happen is the counter
   pausing, resetting, or jumping. */
#define XCL_CAP_SEEK_PHASE 0x80

typedef struct {
    uint8_t cyl;
    uint8_t side;
    uint8_t status; /* XCL_ST_*                                                  */
    uint8_t mode;   /* XclStreamMode                                             */
    uint32_t cellIndex;
    uint32_t tick;
    /* Rotational phase, RIGHT NOW. This is what lets the host lock on to the
       platter the instant it attaches instead of waiting up to a full revolution
       for an index pulse to arrive. */
    uint32_t ticksSinceIndex;
    uint32_t lastIndexPeriodTicks;
    uint32_t lastRevolutionCells; /* MEASURED. It is not 100000 - see 12f.       */
    uint32_t overrunCount;
    uint32_t lastSeekTicks;       /* SEEK_BEGIN -> TRACK_CHANGE, measured        */
} XclStatus;

typedef struct {
    uint8_t command; /* the XclCommand being answered                            */
    uint8_t result;  /* XCL_RESULT_*                                             */
    uint16_t detail;
} XclAck;

#pragma pack(pop)

enum XclResult {
    XCL_RESULT_OK = 0x00,
    XCL_RESULT_UNSUPPORTED = 0x01,
    XCL_RESULT_BAD_PARAM = 0x02,
    XCL_RESULT_NO_MEDIA = 0x03,
    XCL_RESULT_NO_DRIVE = 0x04,
    XCL_RESULT_WRITE_PROTECTED = 0x05,
    XCL_RESULT_BUSY = 0x06,
    XCL_RESULT_SEEK_FAILED = 0x07,
    XCL_RESULT_OVERRUN = 0x08,
    XCL_RESULT_BAD_CRC = 0x09
};

/* ------------------------------------------------------------------------ */
/* Shared helpers - both ends use these, so a framing bug cannot be one-sided */
/* ------------------------------------------------------------------------ */

/* Payload length for a record, from its header alone. */
static inline uint32_t xclPayloadBytes(const XclRecordHeader* h)
{
    switch (h->type) {
    case XCL_REC_MFM:
        return ((uint32_t)h->count + 7u) / 8u;
    case XCL_REC_FLUX:
        return (uint32_t)h->count * 2u;
    case XCL_REC_EVENT:
        return (uint32_t)sizeof(XclEventPayload);
    case XCL_REC_HELLO:
        return (uint32_t)sizeof(XclHello);
    case XCL_REC_STATUS:
        return (uint32_t)sizeof(XclStatus);
    case XCL_REC_ACK:
    case XCL_REC_NAK:
        return (uint32_t)sizeof(XclAck);
    case XCL_REC_TEST:
        return (uint32_t)h->count;
    default:
        return 0u;
    }
}

/* CRC-8, poly 0x07, init 0x00 - command frames. */
static inline uint8_t xclCrc8(const uint8_t* data, uint32_t len)
{
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
    }
    return crc;
}

/* CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF - records. Bitwise on purpose:
   the device may not have room for a 512-byte table, and at ~500 records a
   second over 140-byte records this is a rounding error on an M4. */
static inline uint16_t xclCrc16(const uint8_t* data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (uint16_t)((crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1));
    }
    return crc;
}

/* Compile-time proof that both trees agree about the structs. If one of these
   fires, the two copies of this header have drifted. */
#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(XclRecordHeader) == 12, "XclRecordHeader must be 12 bytes");
static_assert(sizeof(XclEventPayload) == 16, "XclEventPayload must be 16 bytes");
static_assert(sizeof(XclHello) == 32, "XclHello must be 32 bytes");
static_assert(sizeof(XclStatus) == 32, "XclStatus must be 32 bytes");
static_assert(sizeof(XclAck) == 4, "XclAck must be 4 bytes");
#endif

#endif /* XCOPY_LIVE_PROTOCOL_H */
