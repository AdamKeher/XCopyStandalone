#include "XCopyLive.h"
#include "XCopyScratch.h"
#include "XCopyGeometry.h"

/*
   Read directly rather than through "if (Serial)", which calls yield() and refuses for
   15ms after the DTR line moves. The session only needs to know whether the port is
   still enumerated, and this is the flag that says so.
*/
extern "C" volatile uint8_t usb_configuration;

uint8_t *liveTx = NULL;
uint8_t liveRec[XCL_REC_SCRATCH];
uint8_t liveCellBuf[XCL_MAX_RECORD_CELLS / 8];
uint16_t liveFluxBuf[XCL_MAX_FLUX_SAMPLES];

/*
   CRC-16/CCITT-FALSE, table driven.

   Identical in every respect to xclCrc16() in the shared header - same polynomial,
   same initial value, same bit order - and that inline remains the contract's
   reference; this is only a faster way to arrive at the same number, and the host is
   free to keep using the bitwise one.

   It is here because the header's note that bitwise is "a rounding error on an M4" is
   true of the 140 byte MFM records it was written for and not of flux: 524 byte
   records at 930 a second is 4,200 shift-and-test iterations per record and measured
   out at about a sixth of the CPU, which was enough to make flux mode drop records
   that MFM mode did not. 512 bytes of flash buys it back.
*/
static const uint16_t liveCrc16Table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

__attribute__((optimize("O2"))) static uint16_t liveCrc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--)
        crc = (uint16_t)((crc << 8) ^ liveCrc16Table[((crc >> 8) ^ *p++) & 0xFF]);
    return crc;
}

// --- outbound ---------------------------------------------------------------------

void XCopyLive::startRecord(uint8_t type, uint8_t flags, uint16_t count, uint16_t ticks,
                            uint32_t index)
{
    liveRec[0] = XCL_REC_SYNC0;
    liveRec[1] = XCL_REC_SYNC1;
    liveRec[2] = type;
    liveRec[3] = flags;
    liveRec[4] = (uint8_t)(count & 0xFF);
    liveRec[5] = (uint8_t)(count >> 8);
    liveRec[6] = (uint8_t)(ticks & 0xFF);
    liveRec[7] = (uint8_t)(ticks >> 8);
    liveRec[8] = (uint8_t)(index & 0xFF);
    liveRec[9] = (uint8_t)((index >> 8) & 0xFF);
    liveRec[10] = (uint8_t)((index >> 16) & 0xFF);
    liveRec[11] = (uint8_t)((index >> 24) & 0xFF);
    _recLen = sizeof(XclRecordHeader);
}

void XCopyLive::addU16(uint16_t v)
{
    liveRec[_recLen++] = (uint8_t)(v & 0xFF);
    liveRec[_recLen++] = (uint8_t)(v >> 8);
}

void XCopyLive::addU32(uint32_t v)
{
    liveRec[_recLen++] = (uint8_t)(v & 0xFF);
    liveRec[_recLen++] = (uint8_t)((v >> 8) & 0xFF);
    liveRec[_recLen++] = (uint8_t)((v >> 16) & 0xFF);
    liveRec[_recLen++] = (uint8_t)((v >> 24) & 0xFF);
}

/*
   Seals the record and queues it.

   The record is built contiguously rather than straight into the ring so that
   xclCrc16() - the copy of it the host runs, out of the shared header - can be handed
   the whole thing. A framing bug that only one end has is exactly what compiling the
   same helper into both trees is meant to prevent, and that is worth one memcpy of at
   most 526 bytes.

   Returns false when the ring has no room, which means the host has stopped reading.
   Nothing is written in that case - a half record is far worse than a missing one -
   and the caller marks the next record XCL_RF_DISCONTINUITY.
*/
__attribute__((optimize("O2"))) bool XCopyLive::finishRecord()
{
    uint16_t crc = liveCrc16(liveRec, _recLen);
    liveRec[_recLen++] = (uint8_t)(crc & 0xFF);
    liveRec[_recLen++] = (uint8_t)(crc >> 8);

    if (txFree() < _recLen)
        return false;

    for (uint32_t i = 0; i < _recLen; i++)
    {
        liveTx[_txHead] = liveRec[i];
        _txHead = (_txHead + 1) & XCL_TX_MASK;
    }

    return true;
}

/*
   Moves finished bytes out, and stops the instant USB will not take another one.

   usb_serial_write() blocks when the host has stopped reading - there is a comment
   about exactly that at XCopyTransfer.cpp:150 - and a block here would stall the loop,
   which would stall the drain, which would overrun the capture ring and put a hole in
   the angular mapping. availableForWrite() reports the room in the packet the core is
   currently filling, and a write of no more than that provably cannot reach the core's
   wait loop, so this is non blocking by construction rather than by luck.
*/
void XCopyLive::pumpUsb()
{
    /*
       No "if (!Serial)" here on purpose. That operator calls yield() and imposes a
       15ms settle on the DTR line, and this runs thousands of times a second.
       availableForWrite() already returns 0 when USB is not configured, so the loop
       below stops on its own; the session's liveness check is done once every 50ms in
       pollDrive() instead.
    */
    while (_txTail != _txHead)
    {
        int room = Serial.availableForWrite();
        if (room <= 0)
            break;

        uint32_t contig = (_txHead > _txTail) ? (_txHead - _txTail) : (XCL_TX_RING - _txTail);
        if (contig > (uint32_t)room)
            contig = (uint32_t)room;

        int written = Serial.write(&liveTx[_txTail], contig);
        if (written <= 0)
            break;

        _txTail = (_txTail + (uint32_t)written) & XCL_TX_MASK;
        _bytesOut += (uint32_t)written;
    }

    /*
       The pacing flush, and the whole reason XCL_CFG_TX_FLUSH_US exists.

       Everything above hands bytes to the USB core, which sends them as full
       64-byte bulk packets - and at ~63 KB/s the stream fills every packet
       exactly, so a short packet never occurs on its own. The host's serial
       driver (usbser.sys measured, but the behaviour is generic) completes the
       application's read only when the read buffer fills or a short packet
       terminates the transfer, so a stream of nothing but full packets reaches
       the application in read-buffer-sized bursts, 15-30ms apart, however
       promptly the device produced it. That burst is the floor under the host's
       live-serving lead, and 15-30ms of lead is 8-15 sectors of never-captured
       angle after every landing.

       usb_serial_flush_output() queues the partial packet immediately - or a
       zero-length packet if the boundary happened to land on a multiple of 64 -
       and either one completes the host's read NOW. It never blocks; that is
       what the send_now() alias in the core is for. One short packet per
       millisecond costs under 2% of full-speed bulk bandwidth and bounds
       delivery latency to about the flush interval.

       Gated on bytes actually written since the last flush so an idle session
       does not spend packet-pool entries on empty ZLPs.
    */
    if (_txFlushUs && _bytesOut != _bytesAtLastFlush &&
        (uint32_t)(micros() - _lastFlushUs) >= _txFlushUs)
        flushNow();
}

//! Forces whatever the USB core is holding onto the wire as a short packet.
void XCopyLive::flushNow()
{
    usb_serial_flush_output();
    _bytesAtLastFlush = _bytesOut;
    _lastFlushUs = micros();
}

uint8_t XCopyLive::driveStatusBits() const
{
    uint8_t s = 0;
    if (_floppy->readTrack0Line())
        s |= XCL_ST_TRACK0;
    if (_floppy->getWriteProtect())
        s |= XCL_ST_WRPROT;
    if (_floppy->readDiskChangeLine())
        s |= XCL_ST_DISKCHANGE | XCL_ST_MEDIA;
    if (_floppy->getMotorStatus())
        s |= XCL_ST_MOTOR;
    if (_streaming)
        s |= XCL_ST_STREAMING;
    if (_pll.locked())
        s |= XCL_ST_PLL_LOCKED;
    if (_seekState != seekIdle)
        s |= XCL_ST_SEEKING;
    return s;
}

/*
   An event, anchored on both clocks.

   The header index carries the tick counter and the payload carries the cell index, so
   the host never has to work out which clock an event belongs to - which matters most
   for TRACK_CHANGE, where the whole value of the event is the exact cell the new
   surface starts at.
*/
void XCopyLive::emitEvent(uint8_t event, uint32_t arg, uint32_t arg2)
{
    startRecord(XCL_REC_EVENT, 0, 0, 0, _tickIndex);
    addByte(event);
    addByte(_cylinder);
    addByte(_side);
    addByte(driveStatusBits());
    addU32(arg);
    addU32(arg2);
    addU32(_cellIndex);
    finishRecord();
}

void XCopyLive::emitAck(uint8_t command, uint8_t result, uint16_t detail)
{
    startRecord(result == XCL_RESULT_OK ? XCL_REC_ACK : XCL_REC_NAK, 0, 0, 0, _tickIndex);
    addByte(command);
    addByte(result);
    addU16(detail);
    finishRecord();
}

void XCopyLive::emitHello()
{
    uint8_t caps = XCL_CAP_MFM | XCL_CAP_FLUX | XCL_CAP_SELFTEST |
                   XCL_CAP_INBAND_SEEK | XCL_CAP_SEEK_PHASE;
    if (_capture.usingDma())
        caps |= XCL_CAP_DMA;
    if (_floppy->getMode() == HD)
        caps |= XCL_CAP_HD;
    /*
       XCL_CMD_WRITE_TRACK works, and the host bounds its transfers by ringBytes below
       - the same block, because the write buffer is the capture ring's front. An HD
       track (25,418 bytes of cells) does not fit in what is left once the transmit
       ring has its tail, so the host writes DD only; nothing here has to enforce
       that beyond the bound itself.
    */
    if (_blockFront != NULL)
        caps |= XCL_CAP_WRITE;

    startRecord(XCL_REC_HELLO, 0, 0, 0, _tickIndex);
    addByte(XCL_PROTOCOL_VERSION);
    addByte(caps);
    addByte(MAX_CYLINDERS - 1);
    addByte(1);
    addU32(_tickHz);
    addU32(liveRingSamples * 2);
    addU16(XCL_MAX_PAYLOAD);
    addU16(0x0733); // XCOPYVERSION "v733.26" - + WRITE_TRACK, WRITE_DONE, bulk
    static const char ident[16] = {'X', 'C', 'o', 'p', 'y', 'S', 't', 'a',
                                   'n', 'd', 'a', 'l', 'o', 'n', 'e', 0};
    for (uint8_t i = 0; i < 16; i++)
        addByte((uint8_t)ident[i]);
    finishRecord();
}

void XCopyLive::emitStatus()
{
    startRecord(XCL_REC_STATUS, 0, 0, 0, _tickIndex);
    addByte(_cylinder);
    addByte(_side);
    addByte(driveStatusBits());
    addByte(_mode);
    addU32(_cellIndex);
    addU32(_tickIndex);
    // Sampled here and not at the last record boundary. This is the field that lets a
    // host lock on to the platter the instant it attaches instead of waiting up to a
    // whole revolution for an index pulse.
    addU32(_capture.ticksSinceIndex());
    addU32(_capture.lastIndexPeriod());
    addU32(_lastRevCells);
    addU32(_overruns);
    addU32(_seekTicks);
    finishRecord();
}

// --- inbound ----------------------------------------------------------------------

/*
   Reads and acts on host frames.

   Runs every pass of the loop, including while a stream is going out - that is the
   requirement the whole structure of this file exists to meet. The byte budget stops
   a host that floods the port from turning this into the thing that adds latency.
*/
void XCopyLive::pollCommands()
{
    uint32_t budget = 64;

    while (Serial.available() && budget--)
    {
        uint8_t b = (uint8_t)Serial.read();

        switch (_rxState)
        {
        case 0:
            if (b == XCL_CMD_SYNC0)
                _rxState = 1;
            break;

        case 1:
            // Not the second sync byte, but it might be the first of the real frame -
            // which is what a host resynchronising mid stream looks like.
            _rxState = (b == XCL_CMD_SYNC1) ? 2 : ((b == XCL_CMD_SYNC0) ? 1 : 0);
            break;

        case 2:
            _rxCmd = b;
            _rxState = 3;
            break;

        case 3:
            if (b > XCL_CMD_MAX_PAYLOAD)
            {
                emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
                _rxState = 0;
                break;
            }
            _rxLen = b;
            _rxIndex = 0;
            _rxState = _rxLen ? 4 : 5;
            break;

        case 4:
            _rxPayload[_rxIndex++] = b;
            if (_rxIndex >= _rxLen)
                _rxState = 5;
            break;

        case 5:
        {
            uint8_t frame[2 + XCL_CMD_MAX_PAYLOAD];
            frame[0] = _rxCmd;
            frame[1] = _rxLen;
            for (uint8_t i = 0; i < _rxLen; i++)
                frame[2 + i] = _rxPayload[i];

            if (xclCrc8(frame, 2u + _rxLen) == b)
            {
                _lastCmdMs = millis();
                handleCommand();
            }
            else
            {
                emitAck(_rxCmd, XCL_RESULT_BAD_CRC);
            }

            /*
               The answer leaves NOW, not at the next pacing flush. The response to
               a command is a handful of bytes into a stream of full packets, and
               without a short packet behind it, it sits in the host driver's read
               buffer with the data - which is where most of the measured 15-31ms
               "seek written -> ACK" went. The handler has already queued the ACK
               into the ring; push it into the USB core and force it out.

               Gated on the config key with the pacing flush, so that key = 0
               reproduces the pre-v717 behaviour exactly and the two can be
               measured against each other from the host.
            */
            if (_txFlushUs)
            {
                pumpUsb();
                flushNow();
            }
            _rxState = 0;
            break;
        }

        default:
            _rxState = 0;
            break;
        }
    }
}

void XCopyLive::handleCommand()
{
    switch (_rxCmd)
    {
    case XCL_CMD_HELLO:
        emitHello();
        break;

    case XCL_CMD_PING:
        // The keepalive. Answering is all it has to do; _lastCmdMs is already stamped.
        emitAck(_rxCmd, XCL_RESULT_OK);
        break;

    case XCL_CMD_MOTOR:
        if (_rxLen < 1)
        {
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
            break;
        }
        if (_rxPayload[0])
            _floppy->motorOn();
        else
            _floppy->motorOff();
        emitEvent(XCL_EV_MOTOR, _rxPayload[0] ? 1 : 0);
        emitAck(_rxCmd, XCL_RESULT_OK);
        break;

    case XCL_CMD_SEEK:
        if (_rxLen < 2 || _rxPayload[0] >= MAX_CYLINDERS || _rxPayload[1] > 1)
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        else if (_seekState != seekIdle)
            emitAck(_rxCmd, XCL_RESULT_BUSY);
        else
        {
            emitAck(_rxCmd, XCL_RESULT_OK);
            beginSeek(_rxPayload[0], _rxPayload[1], false);
        }
        break;

    case XCL_CMD_RECAL:
        if (_seekState != seekIdle)
            emitAck(_rxCmd, XCL_RESULT_BUSY);
        else
        {
            emitAck(_rxCmd, XCL_RESULT_OK);
            beginSeek(0, _side, true);
        }
        break;

    case XCL_CMD_STREAM_START:
        if (_rxLen < 1 || _rxPayload[0] > XCL_MODE_FLUX)
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        else
        {
            emitAck(_rxCmd, XCL_RESULT_OK);
            streamStart(_rxPayload[0]);
        }
        break;

    case XCL_CMD_STREAM_STOP:
        if (_boundActive)
            endBoundedRead(XCL_RD_ABORTED);
        else
            streamStop();
        emitAck(_rxCmd, XCL_RESULT_OK);
        break;

    case XCL_CMD_READ_TRACK:
        if (_rxLen < 8 || _rxPayload[0] > XCL_MODE_FLUX)
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        else if (_streaming || _seekState != seekIdle || _trackChangePending || _selftest)
            emitAck(_rxCmd, XCL_RESULT_BUSY);
        else
        {
            uint32_t lingerMs = (uint32_t)_rxPayload[2] | ((uint32_t)_rxPayload[3] << 8);
            uint32_t maxTicks = (uint32_t)_rxPayload[4] | ((uint32_t)_rxPayload[5] << 8) |
                                ((uint32_t)_rxPayload[6] << 16) | ((uint32_t)_rxPayload[7] << 24);
            emitAck(_rxCmd, XCL_RESULT_OK);
            beginBoundedRead(_rxPayload[0], _rxPayload[1], lingerMs, maxTicks);
        }
        break;

    case XCL_CMD_NOCLICK:
        if (_seekState != seekIdle || _trackChangePending)
            emitAck(_rxCmd, XCL_RESULT_BUSY);
        else if (!_floppy->noClickStep())
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        else
        {
            _cylinder = 0;
            emitAck(_rxCmd, XCL_RESULT_OK);
        }
        break;

    case XCL_CMD_STATUS:
        emitStatus();
        break;

    case XCL_CMD_CONFIG:
        if (_rxLen < 5)
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        else
        {
            uint32_t value = (uint32_t)_rxPayload[1] | ((uint32_t)_rxPayload[2] << 8) |
                             ((uint32_t)_rxPayload[3] << 16) | ((uint32_t)_rxPayload[4] << 24);
            emitAck(_rxCmd, applyConfig(_rxPayload[0], value), _rxPayload[0]);
        }
        break;

    case XCL_CMD_SELFTEST:
        if (_rxLen < 1)
        {
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
            break;
        }
        if (_rxPayload[0])
        {
            _selftest = true;
            _selftestPaced = (_rxLen >= 2 && _rxPayload[1] != 0);
            _selftestSeq = 0;
            _selftestTick0 = XCopyLiveCapture::tickNow();
            _selftestTicks = 0;
        }
        else
        {
            _selftest = false;
        }
        emitAck(_rxCmd, XCL_RESULT_OK);
        break;

    case XCL_CMD_WRITE_TRACK:
        handleWriteTrack();
        break;

    case XCL_CMD_BYE:
        emitAck(_rxCmd, XCL_RESULT_OK);
        streamStop();
        _running = false;
        break;

    default:
        emitAck(_rxCmd, XCL_RESULT_UNSUPPORTED);
        break;
    }
}

/* --- write -------------------------------------------------------------------- */

/*
   XCL_CMD_WRITE_TRACK.

   Everything that can refuse the write is checked BEFORE the ACK, because the ACK is
   what tells the host to send 13 KB - a refusal afterwards would mean receiving and
   discarding the lot. What cannot be checked before is the transfer's own integrity,
   and that is what XCL_EV_WRITE_DONE carries.
*/
void XCopyLive::handleWriteTrack()
{
    if (_rxLen < 5)
    {
        emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        return;
    }

    const uint32_t numBytes = (uint32_t)_rxPayload[0] | ((uint32_t)_rxPayload[1] << 8);
    const uint8_t flags = _rxPayload[2];
    const uint16_t crc = (uint16_t)((uint16_t)_rxPayload[3] | ((uint16_t)_rxPayload[4] << 8));

    for (uint8_t i = 5; i < _rxLen; i++)
    {
        if (_rxPayload[i] != 0)
        {
            emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
            return;
        }
    }

    if (_blockFront == NULL)
    {
        emitAck(_rxCmd, XCL_RESULT_UNSUPPORTED);
        return;
    }
    if (_streaming || _boundActive || _selftest || _seekState != seekIdle || _trackChangePending)
    {
        emitAck(_rxCmd, XCL_RESULT_BUSY);
        return;
    }
    /*
       One byte of headroom past the cells: XCopyFloppy::writeTrack() clocks eight
       cells past the last byte the host sent and reads the byte they fall in.
    */
    if (numBytes == 0 || numBytes + 1 > _blockBytes)
    {
        emitAck(_rxCmd, XCL_RESULT_BAD_PARAM);
        return;
    }
    if (!_lastDiskPresent)
    {
        emitAck(_rxCmd, XCL_RESULT_NO_MEDIA);
        return;
    }
    if (_floppy->getWriteProtect())
    {
        emitAck(_rxCmd, XCL_RESULT_WRITE_PROTECTED);
        return;
    }

    /*
       Accepted. The ACK has to be on the wire before the host will start sending, and
       pollCommands() only flushes AFTER handleCommand() returns - which is 200 ms from
       now.
    */
    emitAck(_rxCmd, XCL_RESULT_OK);
    pumpUsb();
    flushNow();

    /*
       The capture's ring and the write buffer are the same memory, so the DMA has to
       stop before a byte of it is overwritten.
    */
    _capture.end();

    const uint32_t startMs = millis();
    uint8_t result = receiveBulk(_blockFront, numBytes, crc);

    if (result == XCL_RESULT_OK)
    {
        const int rc = _floppy->writeTrack((int)numBytes, (flags & XCL_WF_FROM_INDEX) != 0);
        switch (rc)
        {
        case 0:
            break;
        case -1:
            result = XCL_RESULT_WRITE_PROTECTED;
            break;
        case -2:
            result = XCL_RESULT_SEEK_FAILED;
            break;
        default:
            result = XCL_RESULT_BAD_PARAM;
            break;
        }
    }

    /*
       Back exactly as run() armed it. The cell and tick counters do not survive this,
       which the protocol header says out loud: XCL_CAP_SEEK_PHASE is a promise about a
       seek, not about a write, and the host's next read zeroes them anyway.
    */
    _capture.begin((uint16_t *)_blockFront, _ringSamples, _floppy->indexPin(), true);

    /*
       The host has been silent for a revolution and the watchdog counts silence, not
       traffic. The bulk transfer was not a command, so stamp it here or a slow write
       ends the session it just finished.
    */
    _lastCmdMs = millis();

    emitEvent(XCL_EV_WRITE_DONE, result, _lastCmdMs - startMs);
    pumpUsb();
    flushNow();
}

uint8_t XCopyLive::receiveBulk(uint8_t *dest, uint32_t bytes, uint16_t expectCrc)
{
    /*
       Long enough that a host stalled behind its own USB scheduling recovers, short
       enough that a host which died mid-transfer does not hold the drive. A 13 KB
       transfer over full-speed CDC is tens of milliseconds.
    */
    static const uint32_t bulkTimeoutMs = 2000;

    uint32_t deadline = millis() + bulkTimeoutMs;
    uint8_t preamble[XCL_BULK_PREAMBLE_BYTES];
    uint32_t got = 0;

    /*
       The preamble is read before a single data byte is consumed, so a host and device
       that disagree about where the bulk begins cost four bytes rather than a track.
    */
    while (got < XCL_BULK_PREAMBLE_BYTES)
    {
        pumpUsb();
        if (!Serial.available())
        {
            if ((int32_t)(millis() - deadline) > 0)
                return XCL_RESULT_OVERRUN;
            continue;
        }
        preamble[got++] = (uint8_t)Serial.read();
        deadline = millis() + bulkTimeoutMs;
    }

    const uint32_t echo = (uint32_t)preamble[2] | ((uint32_t)preamble[3] << 8);
    if (preamble[0] != XCL_BULK_SYNC0 || preamble[1] != XCL_BULK_SYNC1 || echo != bytes)
    {
        emitAck(XCL_CMD_WRITE_TRACK, XCL_RESULT_BAD_PARAM);
        /*
           Refusing is not enough: the host is most of the way through sending 13 KB of
           cells, and returning now leaves them to arrive at the command parser, where
           one byte pair in 65,536 is a sync and one CRC8 in 256 passes. So take the
           transfer that was refused and throw it away, and the wire is clean.
        */
        got = 0;
        while (got < bytes)
        {
            pumpUsb();
            int avail = Serial.available();
            if (avail <= 0)
            {
                if ((int32_t)(millis() - deadline) > 0)
                    break;
                continue;
            }
            for (int i = 0; i < avail && got < bytes; i++, got++)
                Serial.read();
            deadline = millis() + bulkTimeoutMs;
        }
        return XCL_RESULT_BAD_PARAM;
    }

    uint16_t crc = XCL_CRC16_INIT;
    got = 0;
    while (got < bytes)
    {
        pumpUsb();
        int avail = Serial.available();
        if (avail <= 0)
        {
            if ((int32_t)(millis() - deadline) > 0)
                return XCL_RESULT_OVERRUN;
            continue;
        }
        uint32_t want = bytes - got;
        if ((uint32_t)avail < want)
            want = (uint32_t)avail;
        /*
           readBytes() rather than a byte at a time: at 13 KB the per-call overhead is
           the difference between a transfer that keeps up with the host and one that
           makes the host wait on flow control.
        */
        int n = Serial.readBytes((char *)(dest + got), want);
        if (n <= 0)
        {
            if ((int32_t)(millis() - deadline) > 0)
                return XCL_RESULT_OVERRUN;
            continue;
        }
        for (int i = 0; i < n; i++)
            crc = xclCrc16Update(crc, dest[got + i]);
        got += (uint32_t)n;
        deadline = millis() + bulkTimeoutMs;
    }

    return (crc == expectCrc) ? XCL_RESULT_OK : XCL_RESULT_BAD_CRC;
}

uint8_t XCopyLive::applyConfig(uint8_t key, uint32_t value)
{
    switch (key)
    {
    case XCL_CFG_STEP_INTERVAL_US:
        if (value < 2000 || value > 100000)
            return XCL_RESULT_BAD_PARAM;
        _floppy->setStepIntervalUs(value);
        return XCL_RESULT_OK;

    case XCL_CFG_DIR_SETTLE_US:
        if (value > 100000)
            return XCL_RESULT_BAD_PARAM;
        _floppy->setDirSettleUs(value);
        return XCL_RESULT_OK;

    case XCL_CFG_SIDE_SETTLE_US:
        if (value > 100000)
            return XCL_RESULT_BAD_PARAM;
        _floppy->setSideSettleUs(value);
        return XCL_RESULT_OK;

    case XCL_CFG_SEEK_SETTLE_US:
        if (value > 100000)
            return XCL_RESULT_BAD_PARAM;
        _floppy->setSeekSettleUs(value);
        return XCL_RESULT_OK;

    case XCL_CFG_MOTOR_SPINUP_MS:
        if (value > 5000)
            return XCL_RESULT_BAD_PARAM;
        _floppy->setMotorSpinupMs(value);
        return XCL_RESULT_OK;

    case XCL_CFG_CELL_NS:
        if (value < 250 || value > 16000)
            return XCL_RESULT_BAD_PARAM;
        _cellNs = value;
        recomputeTiming();
        return XCL_RESULT_OK;

    case XCL_CFG_PLL_PULL_PERMILLE:
        if (value < 5 || value > 500)
            return XCL_RESULT_BAD_PARAM;
        _pullPermille = value;
        recomputeTiming();
        return XCL_RESULT_OK;

    case XCL_CFG_PLL_MODE:
        if (value > 1)
            return XCL_RESULT_BAD_PARAM;
        _adaptive = (value == 0);
        recomputeTiming();
        return XCL_RESULT_OK;

    case XCL_CFG_RECORD_CELLS:
        if (value < 64 || value > XCL_MAX_RECORD_CELLS || (value & 7))
            return XCL_RESULT_BAD_PARAM;
        _recordCells = value;
        return XCL_RESULT_OK;

    case XCL_CFG_DENSITY:
        if (value > 2)
            return XCL_RESULT_BAD_PARAM;
        /*
           Density sets the FTM0 prescaler, so changing it changes what a tick means.
           Doing that inside a stream would silently invalidate every tick the host has
           already banked, including its index anchor, so it is refused there rather
           than quietly accepted. Stop the stream, set it, start again.
        */
        if (_streaming)
            return XCL_RESULT_BUSY;

        _floppy->setAutoDensity(value == 0);
        if (value == 1)
            _floppy->setMode(DD);
        else if (value == 2)
            _floppy->setMode(HD);

        _cellNs = (_floppy->getMode() == HD) ? 1000 : 2000;
        recomputeTiming();
        return XCL_RESULT_OK;

    case XCL_CFG_WATCHDOG_MS:
        if (value > 600000)
            return XCL_RESULT_BAD_PARAM;
        _watchdogMs = value;
        return XCL_RESULT_OK;

    case XCL_CFG_TX_FLUSH_US:
        // 0 turns the pacing flush off entirely - the pre-0x0C batching
        // behaviour, kept reachable so the two can be measured against each
        // other from the host without reflashing.
        if (value > 100000)
            return XCL_RESULT_BAD_PARAM;
        _txFlushUs = value;
        return XCL_RESULT_OK;

    default:
        return XCL_RESULT_UNSUPPORTED;
    }
}

/*
   Recomputes everything that hangs off the density and the PLL settings.

   The nominal cell arrives from the host in nanoseconds because that is the unit that
   means the same thing on both ends; ticks are a device detail that changes with the
   prescaler. XclHello::tickHz is how the host converts, and it is computed here from
   the same source, so the two cannot disagree.
*/
void XCopyLive::recomputeTiming()
{
    _tickHz = (_floppy->getMode() == HD) ? (uint32_t)F_BUS : (uint32_t)(F_BUS / 2);

    uint64_t ticks = ((uint64_t)_cellNs * _tickHz) / 1000000000ULL;
    if (ticks < 8)
        ticks = 8;
    if (ticks > 4096)
        ticks = 4096;
    _cellTicks = (uint32_t)ticks;

    _pll.begin(_cellTicks, _pullPermille, _adjDiv, _adaptive);
}

// --- stream -----------------------------------------------------------------------

void XCopyLive::streamStart(uint8_t mode)
{
    /*
       Starting a stream zeroes both counters. Changing mode on a stream that is already
       running does not.

       The shared header asks for both: "only XCL_CMD_STREAM_START zeroes it" on the one
       hand, and modes "switchable mid-stream without restarting the capture or resetting
       any counter" on the other. The only reading that satisfies both is the one below -
       the reset belongs to starting, not to the command - and it is also the useful one,
       because the whole point of switching mode mid session is that the host can line
       the two halves up on a counter that did not move under it.
    */
    bool wasStreaming = _streaming;

    _mode = mode;
    _cellBits = 0;
    _fluxCount = 0;
    _fluxTicks = 0;
    _fluxPending = 0;
    _pendingGap = 0;
    _pendingZeros = 0;
    _pendingOne = false;
    _syntheticTicks = 0;
    _cellRemainder = 0;
    _recordUnlocked = false;

    if (!wasStreaming)
    {
        _cellIndex = 0;
        _tickIndex = 0;
        _cellAtLastIndex = 0;
        _haveIndexAnchor = false;
        _lastRevCells = 0;
        _overruns = 0;
        _pendingFlags = 0;
        _tick0 = XCopyLiveCapture::tickNow();
        _capture.discardToNow();

        /*
           Index pulses seen before the stream began belong to a timeline the host was
           never given. Left queued, every one of them satisfies the "has the decoder
           reached this tick yet" test immediately and they all fire at cell 0, which
           the host reads as a revolution of zero cells.
        */
        uint32_t staleTick, stalePeriod;
        while (_capture.nextIndex(&staleTick, &stalePeriod))
        {
        }
        _indexPending = false;
        _haveIndexAnchor = false;
        _lastCaptureOverruns = _capture.overruns();
    }

    _streaming = true;
    _pll.begin(_cellTicks, _pullPermille, _adjDiv, _adaptive);
    _recordStartTicks = 0;

    emitEvent(XCL_EV_STREAM_START, _mode);
}

void XCopyLive::streamStop()
{
    if (!_streaming)
        return;

    // A bounded read that ends by any other route than endBoundedRead() - BYE, the
    // watchdog, the session ending - is simply over. Nothing waits on READ_DONE then.
    _boundActive = false;

    // Whatever is half built goes out now rather than being dropped, so the last record
    // the host sees is the last one that was actually decoded.
    flushPartialRecord();

    _streaming = false;
    emitEvent(XCL_EV_STREAM_STOP, 0);

    // The five millisecond promise: everything buffered is pushed out before returning,
    // without ever blocking on a host that has stopped reading. 2KB over full speed CDC
    // is well inside the budget.
    uint32_t start = millis();
    while (txUsed() && millis() - start < 5)
        pumpUsb();
    if (_txFlushUs)
        flushNow(); // the tail would otherwise sit on the core's 5ms auto-flush timer
}

// --- bounded read -----------------------------------------------------------------

void XCopyLive::beginBoundedRead(uint8_t mode, uint8_t maxIndex, uint32_t lingerMs,
                                 uint32_t maxTicks)
{
    _boundMaxIndex = maxIndex;
    _boundIndexSeen = 0;
    _boundLingerTicks = (uint32_t)(((uint64_t)lingerMs * _tickHz) / 1000u);
    _boundMaxTicks = maxTicks;
    _boundLingerArmed = false;
    _boundLingerEnd = 0;

    // streamStart() zeroes the counters and discards the ring to now, so consumed
    // ticks below are measured from this moment.
    streamStart(mode);
    _boundActive = true;
}

/*
   Ends the capture if its bound has been met.

   Called after the drain has had its turn, so the record holding the cells up to the
   cut has been built; streamStop() flushes it. Two clocks, deliberately: the decoder's
   for the ordinary case, so READ_DONE never overtakes the data it announces, and wall
   time as a backstop for a surface that produces no flux, where the decoder clock
   stands still and the host would otherwise wait forever for a READ_DONE.
*/
void XCopyLive::checkBound()
{
    if (!_boundActive)
        return;

    uint32_t consumed = streamTicksConsumed();
    uint32_t wall = XCopyLiveCapture::tickNow() - _tick0;
    // Half a revolution of slack before wall time is allowed to decide.
    const uint32_t wallSlack = _tickHz / 10;

    bool lingerDone = _boundLingerArmed &&
                      ((int32_t)(consumed - _boundLingerEnd) >= 0 ||
                       (int32_t)(wall - (_boundLingerEnd + wallSlack)) >= 0);
    bool capDone = _boundMaxTicks &&
                   ((int32_t)(consumed - _boundMaxTicks) >= 0 ||
                    (int32_t)(wall - (_boundMaxTicks + wallSlack)) >= 0);

    // An index-bounded read with no cap of its own still has to end on an empty
    // drive: 600ms is three revolutions, so a turning platter cannot fail to show up.
    bool noIndexDone = false;
    if (_boundMaxIndex && !_boundMaxTicks && !_boundLingerArmed)
        noIndexDone = (int32_t)(wall - (_tickHz * 6 / 10)) >= 0;

    if (!lingerDone && !capDone && !noIndexDone)
        return;

    uint8_t reason = XCL_RD_COMPLETE;
    if (_boundMaxIndex && _boundIndexSeen < _boundMaxIndex)
        reason = XCL_RD_NO_INDEX;
    endBoundedRead(reason);
}

void XCopyLive::endBoundedRead(uint8_t reason)
{
    if (!_boundActive)
        return;
    _boundActive = false;

    // Flushes the part-built record, says STREAM_STOP, pushes the tail out.
    streamStop();

    // The cellIndex field of the event is the cell counter as it stands, which after
    // the flush above is exactly the number of cells the host was given.
    emitEvent(XCL_EV_READ_DONE, reason, _boundIndexSeen);

    uint32_t start = millis();
    while (txUsed() && millis() - start < 5)
        pumpUsb();
    if (_txFlushUs)
        flushNow();
}

/*
   Stream time that has actually been turned into cells or samples, including the
   record still being built.

   _tickIndex alone only moves when a record is flushed, so it lags by up to a whole
   record - 2ms at DD. Anything comparing consumed time against real elapsed time has
   to use this rather than _tickIndex, or a perfectly healthy decoder looks two
   milliseconds behind on every pass.
*/
uint32_t XCopyLive::streamTicksConsumed() const
{
    if (_mode == XCL_MODE_MFM)
        return _tickIndex + (_pll.consumedTicks() - _recordStartTicks);

    return _tickIndex + _fluxTicks;
}

void XCopyLive::flushPartialRecord()
{
    if (_mode == XCL_MODE_MFM)
        flushMfmRecord();
    else
        flushFluxRecord();
}

/*
   The producer half of the loop.

   Bounded to one record per call so pollCommands() runs between records and the worst
   case command latency stays inside a millisecond. At DD one MFM record is 1024 cells,
   about 160us of PLL work; one flux record is 256 samples, about 45us.
*/
void XCopyLive::drain()
{
    if (_selftest)
    {
        // Synthetic data replaces the drive entirely, so the ring is thrown away rather
        // than left to wrap - the capture keeps running so STATUS still answers.
        _capture.discardToNow();
        if (_streaming)
            drainSelftest();
        return;
    }

    if (!_streaming)
    {
        // The capture runs from the start of the session so STATUS can answer with a
        // live rotational phase the moment the host attaches. Until STREAM_START the
        // samples are thrown away rather than left to wrap the ring.
        _capture.discardToNow();

        /*
           A seek that completes with no stream running still owes its TRACK_CHANGE.
           There is no decoder clock to hold it against - nothing is being decoded -
           so it goes out the moment the head has settled. A host doing bounded reads
           (XCL_CMD_READ_TRACK) seeks between reads, with no stream up, and waits on
           exactly this event; before it was handled here the pending flag stayed set
           for the rest of the session and every bounded read was refused as BUSY.
        */
        if (_trackChangePending)
        {
            emitEvent(XCL_EV_TRACK_CHANGE, ((uint32_t)_cylinder << 8) | _side, _seekTicks);
            _trackChangePending = false;
        }
        return;
    }

    checkOverrun();
    checkIndex();

    if (_trackChangePending &&
        (int32_t)((_tick0 + streamTicksConsumed()) - _trackChangeAtTick) >= 0)
    {
        flushPartialRecord();
        emitEvent(XCL_EV_TRACK_CHANGE, ((uint32_t)_cylinder << 8) | _side, _seekTicks);
        resetPll();
        _trackChangePending = false;
    }

    if (_mode == XCL_MODE_MFM)
        drainMfm();
    else
        drainFlux();

    checkBound();
}

/*
   The next flux interval, with any time already clocked through as nominal cells taken
   off it.

   While the head is in flight there may be no transitions at all, and the cell counter
   must not pause - see XCL_CAP_SEEK_PHASE. freeRunCells() clocks it on at the nominal
   rate and records how much time it invented; the interval that eventually arrives
   spans that same period, so the invented part is subtracted here rather than being
   counted twice.
*/
/*
   The decode path, and the one place in this file built for speed rather than size.

   The whole project compiles with TEENSY_OPT_SMALLEST_CODE, which is -Os, and that is
   the right default for 200KB of firmware in 256KB of flash. It is the wrong default
   for these three functions: at DD they see 195,000 flux transitions and half a
   million bit cells a second, and -Os declines to inline across them, so every
   transition paid real call overhead. Measured, that left the decoder running at about
   99% of the drive - which sounds fine and is not, because a deficit that small still
   fills the capture ring every five seconds and costs a track.

   Scoped to the functions that need it rather than raised globally, so nothing else in
   the firmware changes size or timing.
*/
__attribute__((optimize("O2"))) uint32_t XCopyLive::nextInterval()
{
    uint32_t interval = _capture.next();

    if (_syntheticTicks)
    {
        if (_capture.usingDma())
        {
            /*
               THE INTERVAL THAT ENDS A GAP DOES NOT CONTAIN THE GAP ON THIS BACK END.

               next() says so in its own comment: the DMA ring holds absolute 16 bit
               captures, so any interval longer than 2.73ms at DD comes back modulo the
               counter, and that is a documented limitation of the back end rather than
               something to work around here. A head move is 98 to 223ms.

               So subtracting the invented span from the interval that follows removes time
               the stream never carried, and it removes it permanently. That matters because
               streamTicksConsumed() is not bookkeeping: it is the clock that releases
               XCL_EV_TRACK_CHANGE and XCL_EV_INDEX, both of which are deliberately held
               back until the decoder reaches the sample they belong to.

               Measured from the host before this change - the seek ACK is emitted
               immediately, TRACK_CHANGE is gated on this clock, and the device reports its
               own head-move time, so the difference is the lag with nothing host-side able
               to influence it: 162, 256, 356, 477, 566, 660, 757ms over seven 223ms seeks,
               never recovering, while short 39ms moves did not accumulate at all. A guest
               waited 2.2 to 2.7 seconds after every seek for a track it could read, and a
               whole-disk sweep of three reads per track scored 7 of 60.

               The invented span is therefore the only record of the gap and it stands. At
               most one counter period, 2.73ms, ends up counted twice.
            */
            _syntheticTicks = 0;
        }
        else
        {
            /*
               The interrupt back end splits a long gap into a run of 0xffff entries and
               next() sums them back into one interval, so there the interval really does
               span the gap and the invented part has to come off or it is counted twice.
            */
            uint32_t take = (interval < _syntheticTicks) ? interval : _syntheticTicks;
            interval -= take;
            _syntheticTicks -= take;
        }
    }

    /*
       Rate limited. A head in flight can produce a long run of these, and a flood of
       events would fill the transmit ring with news the host already has - the gap is
       in the data either way, and the records around it carry XCL_RF_TRACK_UNSTABLE.
    */
    if (interval >= XCL_NO_FLUX_TICKS)
    {
        uint32_t now = millis();
        if (now - _lastNoFluxMs >= 10)
        {
            _lastNoFluxMs = now;
            emitEvent(XCL_EV_NO_FLUX, interval);
        }
    }

    return interval;
}

__attribute__((optimize("O2"))) void XCopyLive::drainMfm()
{
    uint32_t payload = (_recordCells + 7) / 8;
    if (txFree() < sizeof(XclRecordHeader) + payload + 2)
        return; // the host is behind; let the capture ring take the strain

    if (_cellBits == 0)
    {
        _recordFirstCell = _cellIndex;
        _recordUnlocked = false;
    }

    /*
       Wait for flux rather than bouncing off the main loop for it.

       This used to return the moment the capture ran dry, which sounds harmless and is
       not: at DD a transition arrives every 4 to 8us, so "dry" is the normal state
       between one interval and the next, and returning there paid a whole lap of the
       main loop - usb_malloc inside availableForWrite, two millis(), the DMA cursor
       read - for every couple of cells decoded.

       The budget is what keeps the promise about command latency: 250us here, plus one
       lap, is well inside a millisecond.
    */
    uint32_t deadline = micros() + XCL_DRAIN_BUDGET_US;
    uint32_t avail = _capture.available();
    uint32_t checks = 0;
    uint32_t spins = 0;

    while (_cellBits < _recordCells)
    {
        /*
           Zero cells first, in whole bytes wherever the record is byte aligned. A run
           of zeros is by far the most common thing in the stream - two or three between
           every pair of transitions on a healthy track, and thousands of them across an
           erased one - so this is the loop that decides whether the decoder keeps up
           with the drive.
        */
        if (_pendingZeros)
        {
            uint32_t room = _recordCells - _cellBits;
            uint32_t take = (_pendingZeros < room) ? _pendingZeros : room;

            while (take)
            {
                if ((_cellBits & 7) == 0 && take >= 8)
                {
                    liveCellBuf[_cellBits >> 3] = 0;
                    _cellBits += 8;
                    _cellIndex += 8;
                    _pendingZeros -= 8;
                    take -= 8;
                    continue;
                }

                if ((_cellBits & 7) == 0)
                    liveCellBuf[_cellBits >> 3] = 0;
                _cellBits++;
                _cellIndex++;
                _pendingZeros--;
                take--;
            }
            continue;
        }

        // The transition itself.
        if (_pendingOne)
        {
            uint32_t byteIndex = _cellBits >> 3;
            uint8_t mask = (uint8_t)(0x80 >> (_cellBits & 7));
            if (mask == 0x80)
                liveCellBuf[byteIndex] = 0;
            liveCellBuf[byteIndex] |= mask;
            _cellBits++;
            _cellIndex++;
            _pendingOne = false;
            continue;
        }

        // A gap too long for one feed is consumed in chunks, and only the last chunk
        // ends at a transition.
        if (_pendingGap)
        {
            _pendingGap = _pll.feed(_pendingGap);
            _pendingZeros += _pendingGap ? _pll.consumeZeros() : _pll.consume();
            _pendingOne = (_pendingGap == 0);
            if (!_pll.locked())
                _recordUnlocked = true;
            continue;
        }

        if (avail == 0)
        {
            avail = _capture.available();
            if (avail == 0)
            {
                if ((int32_t)(micros() - deadline) >= 0)
                {
                    // Out of budget. If the platter has moved on without giving us any
                    // flux at all, keep the cell counter moving anyway.
                    freeRunCells();
                    return;
                }
                continue; // the next transition is microseconds away
            }
        }

        avail--;
        _pendingGap = _pll.feed(nextInterval());
        _pendingZeros += _pendingGap ? _pll.consumeZeros() : _pll.consume();
        _pendingOne = (_pendingGap == 0);
        if (!_pll.locked())
            _recordUnlocked = true;

        /*
           A full record is 1024 cells, and at DD that is 2ms of disk time - so filling
           one in a single visit would hold off the command poll for twice as long as
           the protocol promises. The budget is checked every 32 transitions, which is
           roughly every 200us and costs nothing measurable, and the half built record
           simply waits in _cellBits until the next visit.
        */
        if ((++checks & 31) == 0 && (int32_t)(micros() - deadline) >= 0)
            return;
    }

    flushMfmRecord();
}

void XCopyLive::flushMfmRecord()
{
    if (_cellBits == 0)
        return;

    uint32_t bytes = (_cellBits + 7) / 8;
    uint32_t pllNow = _pll.consumedTicks();
    uint32_t elapsed = pllNow - _recordStartTicks;
    _recordStartTicks = pllNow;
    _tickIndex += elapsed;

    uint8_t flags = _pendingFlags;
    if (_recordUnlocked)
        flags |= XCL_RF_PLL_UNLOCKED;
    if (_seekState != seekIdle || _trackChangePending)
        flags |= XCL_RF_TRACK_UNSTABLE;

    startRecord(XCL_REC_MFM, flags, (uint16_t)_cellBits,
                (uint16_t)(elapsed > 0xFFFF ? 0xFFFF : elapsed), _recordFirstCell);
    for (uint32_t i = 0; i < bytes; i++)
        addByte(liveCellBuf[i]);

    if (finishRecord())
    {
        _pendingFlags = 0;
    }
    else
    {
        // The host is not reading. The record is dropped whole and the next one carries
        // XCL_RF_DISCONTINUITY; the host finds the hole from the gap in the cell
        // numbering, which is what that numbering is for.
        _overruns++;
        _pendingFlags |= XCL_RF_DISCONTINUITY;
        emitEvent(XCL_EV_OVERRUN, _cellBits);
    }

    _cellBits = 0;
    _recordUnlocked = false;
}

__attribute__((optimize("O2"))) void XCopyLive::drainFlux()
{
    if (txFree() < sizeof(XclRecordHeader) + _fluxSamples * 2 + 2)
        return;

    if (_fluxCount == 0)
    {
        _fluxFirstTick = _tickIndex;
        _fluxTicks = 0;
    }

    bool timeFull = false;
    uint32_t deadline = micros() + XCL_DRAIN_BUDGET_US;
    uint32_t avail = _capture.available();
    uint32_t spins = 0;

    while (_fluxCount < _fluxSamples)
    {
        if (_fluxPending == 0)
        {
            // Same reasoning as drainMfm: wait the few microseconds for the next
            // transition rather than paying a lap of the main loop for each one.
            if (avail == 0)
            {
                avail = _capture.available();
                if (avail == 0)
                {
                    if ((++spins & 63) == 0)
                    {
                        if ((int32_t)(micros() - deadline) >= 0)
                        {
                            freeRunCells();
                            break;
                        }
                    }
                    continue;
                }
            }

            avail--;
            _fluxPending = nextInterval();
            if (_fluxPending == 0)
                continue; // the input filter forbids a zero length interval
        }

        // Past 16 bits the interval is split across samples, the same convention SCP
        // uses, so the record still adds up to the time that passed.
        uint32_t chunk = (_fluxPending > 0xFFFF) ? 0xFFFF : _fluxPending;

        if (_fluxTicks + chunk > 0xFFFF)
        {
            timeFull = true; // XclRecordHeader::ticks is 16 bits; this record ends here
            break;
        }

        liveFluxBuf[_fluxCount++] = (uint16_t)chunk;
        _fluxTicks += chunk;
        _fluxPending -= chunk;
    }

    if (_fluxCount >= _fluxSamples || timeFull)
        flushFluxRecord();
}

void XCopyLive::flushFluxRecord()
{
    if (_fluxCount == 0)
        return;

    uint8_t flags = _pendingFlags;
    if (_seekState != seekIdle || _trackChangePending)
        flags |= XCL_RF_TRACK_UNSTABLE;

    startRecord(XCL_REC_FLUX, flags, (uint16_t)_fluxCount, (uint16_t)_fluxTicks,
                _fluxFirstTick);
    for (uint32_t i = 0; i < _fluxCount; i++)
        addU16(liveFluxBuf[i]);

    if (finishRecord())
    {
        _pendingFlags = 0;
    }
    else
    {
        _overruns++;
        _pendingFlags |= XCL_RF_DISCONTINUITY;
        emitEvent(XCL_EV_OVERRUN, _fluxCount);
    }

    _tickIndex += _fluxTicks;

    /*
       No cells are decoded in flux mode, so the cell counter runs on the nominal cell
       clock - elapsed ticks over the nominal cell width, remainder carried so it does
       not drift. Flux records are indexed by ticks, so this only feeds the cellIndex
       that events carry; it keeps the counter monotonic and continuous when a host
       switches mode mid session.
    */
    uint32_t total = _fluxTicks + _cellRemainder;
    _cellIndex += total / _cellTicks;
    _cellRemainder = total % _cellTicks;

    _fluxCount = 0;
    _fluxTicks = 0;
}

/*
   Keeps the cell counter moving while the head is in flight.

   XCL_CAP_SEEK_PHASE is a promise that the counter keeps advancing at the platter rate
   through a head move, because that is what lets the host's index anchor survive the
   move and serve data off the new surface in tens of milliseconds instead of waiting a
   whole revolution for the next index. Usually the head gives garbage flux during a
   seek and the PLL clocks on that quite happily; a head genuinely off the surface gives
   nothing at all, and the counter would stall.

   Deliberately limited to a seek, and to MFM.

   Outside a seek there is nothing to paper over: a fluxless gap on a settled head is
   real, the interval that ends it carries its full length, and inventing cells there
   would corrupt records that are otherwise exact. In flux mode nothing is needed
   either - a flux record is indexed by ticks and the interval spanning the gap arrives
   with its full length, so both counters catch up by themselves.

   The invented time is remembered so nextInterval() can take it back off the interval
   that eventually arrives; without that, the gap would be counted twice. Records
   emitted across it carry XCL_RF_TRACK_UNSTABLE, and their ticks field excludes the
   invented span - the continuous cell index is the quantity the host is meant to trust
   here, which is exactly what the capability promises.
*/
void XCopyLive::freeRunCells()
{
    if (!_streaming || _mode != XCL_MODE_MFM)
        return;
    if (_seekState == seekIdle && !_trackChangePending)
        return;
    if (_capture.available())
        return;

    uint32_t elapsed = XCopyLiveCapture::tickNow() - _tick0;

    /*
       _syntheticTicks is deliberately NOT subtracted here, and subtracting it was a bug.

       Everything invented so far has already been added to _tickIndex, so it is inside
       streamTicksConsumed() before this line runs. Taking it off a second time makes the
       first pass through a gap look as though the decoder has caught up, and the free run
       stops after one chunk. Measured on the host: a 223ms head move was clocked on by
       roughly half its length, so the cell index - which XCL_CAP_SEEK_PHASE promises keeps
       advancing at the platter rate across a move - came out short on every seek.
    */
    uint32_t behind = elapsed - streamTicksConsumed();

    // Past the point where ordinary buffering explains it, and past a cell so there is
    // something whole to clock.
    if ((int32_t)behind < (int32_t)(_cellTicks * 4))
        return;

    uint32_t cells = behind / _cellTicks;
    _cellIndex += cells;
    _tickIndex += cells * _cellTicks;
    _syntheticTicks += cells * _cellTicks;
}

/*
   Synthetic records at full rate, no drive involved.

   This exists so a host harness can measure USB throughput, framing and loss detection
   before a disk is anywhere near the machine - and so that when the numbers from a real
   capture look wrong, there is a way to tell a slow host from a slow drive. The
   counters advance exactly as they would on a real stream, so a host that detects loss
   correctly here will detect it correctly there.
*/
void XCopyLive::drainSelftest()
{
    /*
       Paced mode mimics the MFM stream's exact shape - a 128 byte record (1024
       cells) every ~2ms, ~63KB/s - so a host can measure DELIVERY latency under
       the load the real decode path produces. Full rate saturates the link and
       measures THROUGHPUT; at saturation every USB packet is full and the packet
       pool runs dry, so the pacing flush has nothing to force out and delivery
       batching cannot be observed there at all. Both are needed; neither can
       stand in for the other.
    */
    uint32_t count = _selftestPaced ? 128 : 256;
    if (txFree() < sizeof(XclRecordHeader) + count + 2)
        return;

    uint32_t ticks = _selftestPaced ? count * 8 * _cellTicks : count * _cellTicks;

    if (_selftestPaced)
    {
        /*
           A record is released only once the platter-equivalent time it spans
           has really passed. Anchored on its own clock, started when the
           selftest was switched on - gating on _tickIndex would let the stream
           burst at full rate until it had "caught up" every tick that passed
           before the selftest began.
        */
        uint32_t elapsed = XCopyLiveCapture::tickNow() - _selftestTick0;
        if ((int32_t)(elapsed - (_selftestTicks + ticks)) < 0)
            return;
        _selftestTicks += ticks;
    }

    startRecord(XCL_REC_TEST, 0, (uint16_t)count,
                (uint16_t)(ticks > 0xFFFF ? 0xFFFF : ticks), _cellIndex);
    for (uint32_t i = 0; i < count; i++)
        addByte((uint8_t)(_selftestSeq + i));

    if (!finishRecord())
        return;

    _selftestSeq++;
    _cellIndex += count * 8;
    _tickIndex += ticks;
}

/*
   Notices that the capture ring has been filled and tells the host where the hole is.

   Two ways it can happen. The interrupt back end knows, because it is the thing that
   had nowhere to put a sample; the DMA back end does not, because eDMA writes over
   unread data without ever being asked, so there it is caught by the consumer having
   fallen a whole ring behind.

   Either way the answer is the one the protocol promises: drop, say so, carry on. The
   cell counter is advanced across the hole by the time that actually elapsed, so the
   angular mapping on the far side is still right - which is the entire reason the
   counter never resets.
*/
void XCopyLive::checkOverrun()
{
    uint32_t captureOverruns = _capture.overruns();
    bool ringLost = _capture.available() >= liveRingSamples - 1;

    if (!ringLost && captureOverruns == _lastCaptureOverruns)
        return;

    _lastCaptureOverruns = captureOverruns;

    /*
       Measured before the part-built record is thrown away, not after. The cells in it
       are already counted in _cellIndex and their time is only in the PLL, so
       discarding first would count that span as lost a second time.
    */
    uint32_t consumed = streamTicksConsumed();

    _cellBits = 0;
    _fluxCount = 0;
    _fluxTicks = 0;
    _fluxPending = 0;
    _pendingGap = 0;
    _pendingZeros = 0;
    _pendingOne = false;
    _syntheticTicks = 0;

    uint32_t dropped = _capture.discardToNow();

    /*
       Consumed time should never exceed real elapsed time, but if a capture back end
       ever hands back an interval longer than the time that actually passed, the
       subtraction below underflows and the cell counter jumps by hundreds of millions -
       a far worse failure than the one being reported. Clamped rather than trusted.
    */
    uint32_t elapsed = XCopyLiveCapture::tickNow() - _tick0;
    uint32_t lostTicks = ((int32_t)(elapsed - consumed) > 0) ? (elapsed - consumed) : 0;
    uint32_t lostCells = lostTicks / _cellTicks;

    _cellIndex += lostCells;
    _tickIndex = elapsed;
    _overruns++;
    _pendingFlags |= XCL_RF_DISCONTINUITY;

    emitEvent(XCL_EV_OVERRUN, dropped, lostCells);
    resetPll();
}

/*
   Places an index pulse at the exact cell it fell on.

   The pulse was recorded against a sample number by the index interrupt, so the event
   is held back until the decoder has reached that sample and the cell index is the real
   one. An event carrying "wherever the decoder happened to be when the pulse arrived"
   would be useless for the thing the host wants it for, which is anchoring angle.

   Differencing the anchor from one index to the next is also where
   XclStatus::lastRevolutionCells comes from - measured, over the revolution that just
   happened, not computed from a nominal rate.
*/
void XCopyLive::checkIndex()
{
    if (!_indexPending)
        _indexPending = _capture.nextIndex(&_indexAtTick, &_indexPeriod);

    while (_indexPending &&
           (int32_t)((_tick0 + streamTicksConsumed()) - _indexAtTick) >= 0)
    {
        if (_haveIndexAnchor)
            _lastRevCells = _cellIndex - _cellAtLastIndex;

        _cellAtLastIndex = _cellIndex;
        _haveIndexAnchor = true;

        emitEvent(XCL_EV_INDEX, _indexPeriod, _lastRevCells);
        _indexPending = _capture.nextIndex(&_indexAtTick, &_indexPeriod);

        // A bounded read counts pulses here, on the decoder's clock, so the linger
        // starts from the cell the pulse actually fell on.
        if (_boundActive && _boundMaxIndex && !_boundLingerArmed)
        {
            if (++_boundIndexSeen >= _boundMaxIndex)
            {
                _boundLingerArmed = true;
                _boundLingerEnd = streamTicksConsumed() + _boundLingerTicks;
            }
        }
    }
}

void XCopyLive::resetPll()
{
    _pll.begin(_cellTicks, _pullPermille, _adjDiv, _adaptive);
    _pendingGap = 0;
    _pendingZeros = 0;
    _pendingOne = false;
    _recordStartTicks = 0;
    _recordUnlocked = false;
    emitEvent(XCL_EV_PLL_RESET, _pll.clockTicks());
}

// --- head -------------------------------------------------------------------------

/*
   Starts a head move without stopping anything.

   The sequence and what must not happen in the middle of it are both spelled out in
   the shared header: SEEK_BEGIN at the current position, move, TRACK_CHANGE at the cell
   index where the new surface starts, and throughout that the capture keeps running,
   the counters keep counting, and no handshake is involved.

   Nothing here blocks. gotoLogicTrack() would - a 40 cylinder seek is around 155ms of
   delay() against a 78ms capture ring - so a live seek is a state machine that
   pumpSeek() advances from the main loop while drain() keeps emptying the ring.
*/
void XCopyLive::beginSeek(uint8_t cylinder, uint8_t side, bool recal)
{
    _seekStartTick = XCopyLiveCapture::tickNow();
    emitEvent(XCL_EV_SEEK_BEGIN, ((uint32_t)cylinder << 8) | side);

    // The side line is set now so its settle overlaps the stepping rather than being
    // paid after it.
    _floppy->setSideFast(side);
    _side = side;
    _seekRecal = recal;
    _seekTarget = cylinder;

    int current = _floppy->getCurrentTrack();

    if (recal || current < 0)
    {
        _floppy->setDirFast(0);
        _seekRecal = true;
        _seekSteps = MAX_CYLINDERS + 6; // bounded; the track 0 line is what stops it
        _seekState = seekDirWait;
        _seekDeadline = micros() + _floppy->getDirSettleUs();
        return;
    }

    if ((int)cylinder == current)
    {
        // A side change is the same operation with no stepping in it.
        _seekSteps = 0;
        _seekState = seekSettleWait;
        _seekDeadline = micros() + _floppy->getSideSettleUs();
        return;
    }

    _floppy->setDirFast((int)cylinder < current ? 0 : 1);
    _seekSteps = ((int)cylinder < current) ? (current - (int)cylinder) : ((int)cylinder - current);
    _seekState = seekDirWait;
    _seekDeadline = micros() + _floppy->getDirSettleUs();
}

void XCopyLive::pumpSeek()
{
    if (_seekState == seekIdle)
        return;

    if ((int32_t)(micros() - _seekDeadline) < 0)
        return;

    switch (_seekState)
    {
    case seekDirWait:
    case seekStepWait:
        if (_seekRecal && _floppy->readTrack0Line())
            _seekSteps = 0;

        if (_seekSteps <= 0)
        {
            if (_seekRecal && !_floppy->readTrack0Line())
            {
                // Stepped the whole way out and the line never came up.
                emitEvent(XCL_EV_ERROR, XCL_RESULT_SEEK_FAILED);
                _seekState = seekIdle;
                return;
            }
            _seekState = seekSettleWait;
            _seekDeadline = micros() + _floppy->getSeekSettleUs();
            return;
        }

        // Multi cylinder seeks step at the configured interval and pay the settle once
        // at the end, rather than a full settle per cylinder.
        _floppy->stepPulse();
        _seekSteps--;
        _seekState = seekStepWait;
        _seekDeadline = micros() + _floppy->getStepIntervalUs();
        return;

    case seekSettleWait:
        finishSeek();
        return;

    default:
        _seekState = seekIdle;
        return;
    }
}

void XCopyLive::finishSeek()
{
    _seekTicks = XCopyLiveCapture::tickNow() - _seekStartTick;
    _cylinder = _seekRecal ? 0 : (uint8_t)_seekTarget;
    _floppy->setTrackPosition(_cylinder);
    _seekState = seekIdle;

    /*
       Everything already in the ring came off the surface the head has just left, so
       the new one starts at the write cursor as it stands now. Holding the event back
       until the decoder reaches that sample is what puts TRACK_CHANGE on the exact cell
       the new track begins at rather than approximately near it.
    */
    _trackChangeAtTick = XCopyLiveCapture::tickNow();
    _trackChangePending = true;
}

// --- housekeeping -----------------------------------------------------------------

void XCopyLive::pollDrive()
{
    uint32_t now = millis();
    if (now - _lastPollMs < 50)
        return;
    _lastPollMs = now;

    bool present = _floppy->readDiskChangeLine();
    if (present != _lastDiskPresent)
    {
        _lastDiskPresent = present;
        emitEvent(XCL_EV_DISK_CHANGE, present ? 1 : 0);

        /*
           A disk has just been put in. Make sure the drive is actually turning
           before the host is told there is media to read: the motor should still
           be on, but if anything did stop it, this is the point at which nothing
           else would ever start it again.
        */
        if (present)
            _floppy->motorOn();
    }

    bool wp = _floppy->getWriteProtect();
    if (wp != _lastWriteProtect)
    {
        _lastWriteProtect = wp;
        emitEvent(XCL_EV_WRITE_PROTECT, wp ? 1 : 0);
    }
}

void XCopyLive::showScreen(const char *line1, const char *line2)
{
    _graphics->clearScreen();
    _graphics->setTextScale(2);
    _graphics->drawText(6, 30, ST7735_GREEN, line1, true);
    _graphics->setTextScale(1);
    _graphics->drawText(6, 60, ST7735_WHITE, line2, true);
}

// --- session ----------------------------------------------------------------------

void XCopyLive::run(volatile bool *cancel)
{
    _cancel = cancel;

    // The banner is the last ASCII on the port. From the byte after it the session is
    // binary in both directions.
    Serial.print(XCL_BANNER);
    Serial.print("\r\n");
    Serial.flush();
    while (Serial.available())
        Serial.read();

    showScreen("LIVE", "USB stream - host in control");

    /*
       The five second motor idle timeout is what a UI driven copier wants and is
       exactly wrong here: a live session spins up once and stays up, and the host says
       when that ends. Restored on the way out.
    */
    _floppy->setMotorIdleOff(false);

    /*
       And do not let a disk change stop it either. diskChangeIRQ() kills the motor
       on the /DSKCHG falling edge, which is right for a copier - the operation is
       over - and wrong here: an eject would stop the drive, and the motor would
       still be off when a disk was put back, so no index pulses would be produced
       and the host could never see the new media. Restored on the way out with
       the idle timeout.
    */
    _floppy->setDiskChangeStopsMotor(false);

    /*
       And hold drive select for the whole session. A deselected drive lets its
       status lines float to the ribbon pull-ups, where /DSKCHG reads as "disk
       present" - so with the motor parked, which is what a guest does whenever it
       is not reading, an ejected disk still answered "inserted".
    */
    _floppy->setKeepDriveSelected(true);
    _floppy->motorOn();

    _cylinder = (_floppy->getCurrentTrack() >= 0) ? (uint8_t)_floppy->getCurrentTrack() : 0;
    _side = (uint8_t)_floppy->getCurrentSide();
    _lastDiskPresent = _floppy->readDiskChangeLine();
    _lastWriteProtect = _floppy->getWriteProtect();

    recomputeTiming();

    /*
       The track buffer, split: the capture ring takes the front of it and the transmit
       ring the tail. Nothing decodes a track while a session is running, so the whole
       block is ours, and neither ring has to come out of bss.
    */
    // Registered rather than just taken: the block is shared with the transfer, MD5
    // and dump paths now, and a session that overlapped one of those would corrupt
    // both silently. A session wants all of it - capture ring at the front, transmit
    // ring at the tail - so it asks for the whole block. See XCopyScratch.h.
    uint8_t *block = XCopyScratch::borrow("live.run", XCopyScratch::capacity());
    if (block == nullptr) {
        showScreen("Live Stream", "buffer busy");
        delay(1500);
        return;
    }
    size_t blockSize = XCopyScratch::capacity();

    liveTx = block + blockSize - XCL_TX_RING;

    uint16_t *ring = (uint16_t *)block;
    uint32_t ringSamples = (uint32_t)((blockSize - XCL_TX_RING) / sizeof(uint16_t));

    /*
       Kept for XCL_CMD_WRITE_TRACK, which borrows this same front for its cells and
       has to put the capture back exactly as it was. XCopyLiveCapture::end() clears
       the globals it was handed, so the write path cannot read them back out of it.
       This is also what sets XCL_CAP_WRITE in the HELLO.
    */
    _blockFront = block;
    _blockBytes = (uint32_t)(blockSize - XCL_TX_RING);
    _ringSamples = ringSamples;

    /*
       The capture starts with the session rather than with the stream, so STATUS can
       answer with a live rotational phase the moment the host attaches instead of
       making it wait up to a revolution for an index. Until STREAM_START the drain
       throws the samples away.
    */
    bool captureOk = _capture.begin(ring, ringSamples, _floppy->indexPin(), true);

    _lastCmdMs = millis();
    _lastCaptureOverruns = 0;
    _running = true;

    if (!captureOk)
        emitEvent(XCL_EV_ERROR, XCL_RESULT_NO_DRIVE);

    /*
       State the media state once, unprompted, before the first command.

       pollDrive() only emits XCL_EV_DISK_CHANGE on a CHANGE, and _lastDiskPresent
       was just latched above - so a session that opens with an empty drive never
       emits anything, and a host that assumes a disk until told otherwise keeps
       assuming one. Saying it up front costs one record and removes the need for
       the host to infer it.
    */
    emitEvent(XCL_EV_DISK_CHANGE, _lastDiskPresent ? 1 : 0);

    while (_running)
    {
        pumpUsb();
        pollCommands();
        pumpSeek();
        drain();
        pollDrive();

        if (_cancel && *_cancel)
            break;
        if (!usb_configuration)
            break;

        // XCL_CMD_PING is the host's keepalive, so silence really does mean the host
        // has gone rather than that it is busy reading.
        if (_watchdogMs && (millis() - _lastCmdMs) > _watchdogMs)
            break;
    }

    streamStop();
    _capture.end();

    _floppy->setKeepDriveSelected(false);
    _floppy->setDiskChangeStopsMotor(true);
    _floppy->motorOff();
    _floppy->setMotorIdleOff(true);

    // Nothing the host sent after the session ended may arrive at the console prompt as
    // keystrokes.
    while (Serial.available())
        Serial.read();

    _txHead = _txTail = 0;
    _rxState = 0;
    liveTx = NULL;
    _blockFront = NULL;
    _blockBytes = 0;
    _ringSamples = 0;
    XCopyScratch::release(block);
}
