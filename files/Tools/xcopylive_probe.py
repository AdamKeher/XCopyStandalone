#!/usr/bin/env python3
"""
xcopylive_probe.py - measures an XCopy Standalone live streaming session.

This is the measuring instrument for the numbers the firmware cannot honestly report
about itself: sustained throughput with a host consuming at real-time pace, seek
latency end to end, stream stop latency, and cells per revolution over many laps.
Everything it prints is measured on the wire, not computed from a nominal rate.

It speaks the contract in shared/XCopyLiveProtocol.h and nothing else, so it is also a
conformance check: if this runs clean, the framing, the CRCs and the counter continuity
are right.

    pip install pyserial
    python xcopylive_probe.py COM7 --all
    python xcopylive_probe.py /dev/ttyACM0 --throughput flux --seconds 30

Nothing here writes to a disk, and the device refuses XCL_CMD_WRITE_TRACK anyway.
"""

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is needed: pip install pyserial")

BANNER = b"XCLIVE1"

CMD_SYNC0, CMD_SYNC1 = 0xA5, 0x5A
REC_SYNC0, REC_SYNC1 = 0x9E, 0xC5

CMD_HELLO, CMD_MOTOR, CMD_SEEK, CMD_RECAL = 0x01, 0x02, 0x03, 0x04
CMD_STREAM_START, CMD_STREAM_STOP, CMD_STATUS = 0x05, 0x06, 0x07
CMD_CONFIG, CMD_SELFTEST, CMD_PING, CMD_WRITE_TRACK = 0x08, 0x09, 0x0A, 0x0B
CMD_BYE = 0x7F

MODE_MFM, MODE_FLUX = 0, 1

REC_MFM, REC_FLUX, REC_EVENT = 0x01, 0x02, 0x03
REC_HELLO, REC_STATUS, REC_ACK, REC_NAK, REC_TEST = 0x04, 0x05, 0x06, 0x07, 0x08

EV_INDEX, EV_TRACK_CHANGE, EV_SEEK_BEGIN, EV_PLL_RESET = 0x01, 0x02, 0x03, 0x04
EV_OVERRUN, EV_NO_FLUX, EV_DISK_CHANGE, EV_WRITE_PROTECT = 0x05, 0x06, 0x07, 0x08
EV_STREAM_START, EV_STREAM_STOP, EV_MOTOR, EV_ERROR = 0x09, 0x0A, 0x0B, 0x0C

EV_NAMES = {
    EV_INDEX: "INDEX", EV_TRACK_CHANGE: "TRACK_CHANGE", EV_SEEK_BEGIN: "SEEK_BEGIN",
    EV_PLL_RESET: "PLL_RESET", EV_OVERRUN: "OVERRUN", EV_NO_FLUX: "NO_FLUX",
    EV_DISK_CHANGE: "DISK_CHANGE", EV_WRITE_PROTECT: "WRITE_PROTECT",
    EV_STREAM_START: "STREAM_START", EV_STREAM_STOP: "STREAM_STOP",
    EV_MOTOR: "MOTOR", EV_ERROR: "ERROR",
}

RF_DISCONTINUITY, RF_PLL_UNLOCKED, RF_TRACK_UNSTABLE = 0x01, 0x02, 0x04

CFG_STEP_INTERVAL_US, CFG_DIR_SETTLE_US, CFG_SIDE_SETTLE_US = 0x01, 0x02, 0x03
CFG_SEEK_SETTLE_US, CFG_MOTOR_SPINUP_MS, CFG_CELL_NS = 0x04, 0x05, 0x06
CFG_PLL_PULL_PERMILLE, CFG_PLL_MODE, CFG_RECORD_CELLS = 0x07, 0x08, 0x09
CFG_DENSITY, CFG_WATCHDOG_MS = 0x0A, 0x0B


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def payload_bytes(rtype, count):
    if rtype == REC_MFM:
        return (count + 7) // 8
    if rtype == REC_FLUX:
        return count * 2
    if rtype == REC_EVENT:
        return 16
    if rtype in (REC_HELLO, REC_STATUS):
        return 32
    if rtype in (REC_ACK, REC_NAK):
        return 4
    if rtype == REC_TEST:
        return count
    return 0


class Record:
    __slots__ = ("type", "flags", "count", "ticks", "index", "payload")

    def __init__(self, rtype, flags, count, ticks, index, payload):
        self.type, self.flags = rtype, flags
        self.count, self.ticks, self.index = count, ticks, index
        self.payload = payload

    def event(self):
        ev, cyl, side, status = self.payload[0:4]
        arg, arg2, cell = struct.unpack("<III", self.payload[4:16])
        return dict(event=ev, name=EV_NAMES.get(ev, "?%02x" % ev), cyl=cyl, side=side,
                    status=status, arg=arg, arg2=arg2, cell=cell)


class Link:
    """The wire, with a resynchronising record reader."""

    def __init__(self, port, baud=115200, timeout=0.05):
        # Baud is ignored by USB CDC; it is here only because pyserial wants it.
        self.ser = serial.Serial(port, baud, timeout=timeout)
        try:
            # Windows defaults to a small driver buffer. At flux rates that alone can
            # make the host look like the slow end.
            self.ser.set_buffer_size(rx_size=262144, tx_size=4096)
        except Exception:
            pass
        self.buf = bytearray()
        self.bad_crc = 0
        self.resyncs = 0
        self.bytes_in = 0

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def enter(self, attempts=8):
        """Sends the console command and waits for the binary banner.

        Retried, because the device spends several seconds in its boot sequence - SD
        card, ESP link, splash screen - during which the console is not reading yet, and
        a single attempt straight after a reflash simply lands in the dark.
        """
        for _ in range(attempts):
            self.ser.reset_input_buffer()
            self.ser.write(b"\r")
            time.sleep(0.3)
            self.ser.reset_input_buffer()
            self.ser.write(b"live\r")

            deadline = time.time() + 2.0
            acc = bytearray()
            while time.time() < deadline:
                acc += self.ser.read(256)
                i = acc.find(BANNER)
                if i >= 0:
                    rest = acc[i + len(BANNER):]
                    # Step over the CRLF the device sends after the banner.
                    while rest[:1] in (b"\r", b"\n"):
                        rest = rest[1:]
                    self.buf = bytearray(rest)
                    return True

        raise RuntimeError(
            "no %s banner after %d attempts - is the device at the console prompt, "
            "and not already in a live session?" % (BANNER.decode(), attempts))

    def send(self, cmd, payload=b""):
        body = bytes([cmd, len(payload)]) + payload
        self.ser.write(bytes([CMD_SYNC0, CMD_SYNC1]) + body + bytes([crc8(body)]))

    def config(self, key, value):
        self.send(CMD_CONFIG, bytes([key]) + struct.pack("<I", value))

    def _fill(self, want=1):
        """Takes whatever is already buffered, and only blocks when there is nothing.

        read(n) blocks until n bytes have arrived or the timeout expires, so asking for
        a fixed 4096 throttles the host to one buffer per timeout and makes the device
        look slow when it is not. in_waiting is what turns this into "take everything
        that is here, now".
        """
        n = self.ser.in_waiting
        chunk = self.ser.read(n if n else 1)
        if chunk:
            self.buf += chunk
            self.bytes_in += len(chunk)
        return len(chunk)

    def read_record(self, timeout=1.0):
        """Next valid record, or None on timeout. Resynchronises on damage."""
        deadline = time.time() + timeout
        while True:
            i = self.buf.find(bytes([REC_SYNC0, REC_SYNC1]))
            if i < 0:
                # Keep one byte in case the sync straddles the boundary.
                if len(self.buf) > 1:
                    del self.buf[:-1]
                if not self._fill() and time.time() > deadline:
                    return None
                continue
            if i:
                self.resyncs += 1
                del self.buf[:i]

            if len(self.buf) < 12:
                if not self._fill(12) and time.time() > deadline:
                    return None
                continue

            rtype, flags = self.buf[2], self.buf[3]
            count, ticks, index = struct.unpack("<HHI", bytes(self.buf[4:12]))
            plen = payload_bytes(rtype, count)
            total = 12 + plen + 2

            if plen == 0 and rtype not in (REC_MFM, REC_FLUX, REC_TEST):
                # Unknown type: this was not a record boundary after all.
                self.resyncs += 1
                del self.buf[:2]
                continue

            if len(self.buf) < total:
                if not self._fill(total - len(self.buf)) and time.time() > deadline:
                    return None
                continue

            body = bytes(self.buf[:12 + plen])
            got = struct.unpack("<H", bytes(self.buf[12 + plen:total]))[0]
            if crc16(body) != got:
                self.bad_crc += 1
                del self.buf[:2]
                continue

            payload = bytes(self.buf[12:12 + plen])
            del self.buf[:total]
            return Record(rtype, flags, count, ticks, index, payload)

    def wait_for(self, rtype, timeout=2.0, sink=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.read_record(timeout=max(0.05, deadline - time.time()))
            if r is None:
                continue
            if r.type == rtype:
                return r
            if sink is not None:
                sink(r)
        return None


def parse_hello(p):
    ver, caps, maxcyl, drives = p[0:4]
    tick_hz, ring_bytes = struct.unpack("<II", p[4:12])
    max_rec, fw = struct.unpack("<HH", p[12:16])
    ident = p[16:32].split(b"\x00")[0].decode("ascii", "replace")
    return dict(version=ver, caps=caps, max_cyl=maxcyl, drives=drives, tick_hz=tick_hz,
                ring_bytes=ring_bytes, max_record=max_rec, fw=fw, ident=ident)


def parse_status(p):
    cyl, side, status, mode = p[0:4]
    (cell, tick, since_index, index_period,
     rev_cells, overruns, seek_ticks) = struct.unpack("<IIIIIII", p[4:32])
    return dict(cyl=cyl, side=side, status=status, mode=mode, cell=cell, tick=tick,
                ticks_since_index=since_index, index_period=index_period,
                rev_cells=rev_cells, overruns=overruns, seek_ticks=seek_ticks)


CAP_NAMES = [(0x01, "MFM"), (0x02, "FLUX"), (0x04, "DMA"), (0x08, "INBAND_SEEK"),
             (0x10, "WRITE"), (0x20, "SELFTEST"), (0x40, "HD"), (0x80, "SEEK_PHASE")]
ST_NAMES = [(0x01, "TRACK0"), (0x02, "WRPROT"), (0x04, "DISKCHANGE"), (0x08, "MOTOR"),
            (0x10, "STREAMING"), (0x20, "PLL_LOCKED"), (0x40, "MEDIA"), (0x80, "SEEKING")]


def flags_text(value, table):
    return ",".join(n for b, n in table if value & b) or "-"


# --- measurements -------------------------------------------------------------------

def do_hello(link):
    link.send(CMD_HELLO)
    r = link.wait_for(REC_HELLO, 2.0)
    if r is None:
        raise RuntimeError("no HELLO")
    h = parse_hello(r.payload)
    print("device      : %s fw %04x, protocol %d" % (h["ident"], h["fw"], h["version"]))
    print("capabilities: %s" % flags_text(h["caps"], CAP_NAMES))
    print("tick rate   : %d Hz (%.3f ns/tick)" % (h["tick_hz"], 1e9 / h["tick_hz"]))
    print("capture ring: %d bytes   max record payload %d bytes" %
          (h["ring_bytes"], h["max_record"]))
    print("max cylinder: %d, drives %d" % (h["max_cyl"], h["drives"]))
    print()
    if not h["caps"] & 0x04:
        print("NOTE: XCL_CAP_DMA is clear - the device fell back to its interrupt")
        print("      capture path. Report item 2 should quote this run.")
        print()
    return h


def do_status(link, label="status", required=True):
    link.send(CMD_STATUS)
    r = link.wait_for(REC_STATUS, 2.0, sink=lambda _r: None)
    if r is None:
        if not required:
            print("%-12s: no reply - the session has ended" % label)
            return None
        raise RuntimeError("no STATUS")
    s = parse_status(r.payload)
    print("%-12s: cyl %d side %d  %s" % (label, s["cyl"], s["side"],
                                         flags_text(s["status"], ST_NAMES)))
    print("              cell %u tick %u  since index %u  last index period %u" %
          (s["cell"], s["tick"], s["ticks_since_index"], s["index_period"]))
    print("              measured cells/rev %u  overruns %u  last seek %u ticks" %
          (s["rev_cells"], s["overruns"], s["seek_ticks"]))
    return s


def measure_throughput(link, mode, seconds, hello, selftest=False):
    """Sustained payload rate with the host consuming at real-time pace."""
    name = "SELFTEST" if selftest else ("MFM" if mode == MODE_MFM else "FLUX")
    print("--- throughput, %s, %ds ---" % (name, seconds))

    if selftest:
        link.send(CMD_SELFTEST, bytes([1]))
    link.send(CMD_STREAM_START, bytes([mode, 0]))

    t0 = time.time()
    last_ping = t0
    payload = 0
    wire = 0
    records = 0
    overruns = 0
    disc = 0
    gaps = 0
    expected = None
    first_index = None
    last_index_cell = None
    rev_cells = []
    index_periods = []
    lap_clean = True
    dirty_laps = 0

    while time.time() - t0 < seconds:
        now = time.time()
        if now - last_ping > 1.0:
            link.send(CMD_PING)  # the device watchdog wants to hear from us
            last_ping = now

        r = link.read_record(timeout=0.5)
        if r is None:
            continue

        if r.type in (REC_MFM, REC_FLUX, REC_TEST):
            records += 1
            payload += len(r.payload)
            wire += 14 + len(r.payload)
            if r.flags & RF_DISCONTINUITY:
                disc += 1

            if r.type == REC_MFM:
                if expected is not None and r.index != expected:
                    gaps += 1
                expected = r.index + r.count
                if last_index_cell is not None:
                    pass
            elif r.type == REC_FLUX:
                if expected is not None and r.index != expected:
                    gaps += 1
                expected = r.index + r.ticks

        elif r.type == REC_EVENT:
            e = r.event()
            if e["event"] == EV_OVERRUN:
                overruns += 1
                lap_clean = False
                expected = None  # the device told us; do not also count it as a gap
            elif e["event"] == EV_INDEX:
                if e["arg"]:
                    index_periods.append(e["arg"])
                # A lap that straddles an overrun, a PLL reset or a head move is not a
                # measurement of the disk, so it is counted and then set aside.
                if last_index_cell is not None:
                    if lap_clean:
                        rev_cells.append(e["cell"] - last_index_cell)
                    else:
                        dirty_laps += 1
                lap_clean = True
                last_index_cell = e["cell"]
                if first_index is None:
                    first_index = e["cell"]
            elif e["event"] in (EV_PLL_RESET, EV_TRACK_CHANGE, EV_SEEK_BEGIN):
                lap_clean = False
            elif e["event"] in (EV_ERROR,):
                print("  EVENT %s arg=%u" % (e["name"], e["arg"]))

    elapsed = time.time() - t0
    link.send(CMD_STREAM_STOP)
    if selftest:
        link.send(CMD_SELFTEST, bytes([0]))
    time.sleep(0.05)

    print("  payload   : %.1f KB/s  (%d bytes in %.1fs)" %
          (payload / elapsed / 1024.0, payload, elapsed))
    print("  wire      : %.1f KB/s  (%d records, %.1f%% framing overhead)" %
          (wire / elapsed / 1024.0, records,
           100.0 * (wire - payload) / wire if wire else 0.0))
    print("  overruns  : %d reported by the device, %d discontinuity flags" % (overruns, disc))
    print("  index gaps: %d unexplained breaks in the index numbering" % gaps)
    print("  crc errors: %d,  resyncs: %d" % (link.bad_crc, link.resyncs))

    if index_periods:
        tick_hz = hello["tick_hz"]
        avg = sum(index_periods) / len(index_periods)
        print("  revolution: %d laps, mean %.3f ms (min %.3f, max %.3f)" %
              (len(index_periods), avg / tick_hz * 1e3,
               min(index_periods) / tick_hz * 1e3, max(index_periods) / tick_hz * 1e3))
        print("              implied %.2f RPM" % (60.0 / (avg / tick_hz)))
    if rev_cells:
        ordered = sorted(rev_cells)
        avg = sum(rev_cells) / len(rev_cells)
        median = ordered[len(ordered) // 2]
        print("  cells/rev : %d clean laps (%d discarded), median %d, mean %.1f" %
              (len(rev_cells), dirty_laps, median, avg))
        print("              min %d, max %d, spread %.2f%%" %
              (ordered[0], ordered[-1],
               100.0 * (ordered[-1] - ordered[0]) / median if median else 0.0))
    print()
    return dict(payload_rate=payload / elapsed, overruns=overruns, gaps=gaps,
                rev_cells=rev_cells, index_periods=index_periods)


def measure_seek(link, distance, hello):
    """SEEK_BEGIN to TRACK_CHANGE, measured on the wire and on the device."""
    link.send(CMD_RECAL)
    time.sleep(1.0)
    link.send(CMD_STREAM_START, bytes([MODE_MFM, 0]))
    time.sleep(0.3)

    # Drain to a known point.
    t_end = time.time() + 0.3
    while time.time() < t_end:
        link.read_record(timeout=0.05)

    link.send(CMD_SEEK, bytes([distance, 0]))
    t_sent = time.time()

    t_begin = None
    seek_ticks = None
    cell_begin = cell_change = None
    deadline = time.time() + 5.0

    while time.time() < deadline:
        r = link.read_record(timeout=0.2)
        if r is None or r.type != REC_EVENT:
            continue
        e = r.event()
        if e["event"] == EV_SEEK_BEGIN:
            t_begin = time.time()
            cell_begin = e["cell"]
        elif e["event"] == EV_TRACK_CHANGE and t_begin is not None:
            seek_ticks = e["arg2"]
            cell_change = e["cell"]
            wall = time.time() - t_begin
            tick_hz = hello["tick_hz"]
            print("  %2d cylinder seek: device %.2f ms (%u ticks), host-observed %.2f ms" %
                  (distance, seek_ticks / tick_hz * 1e3, seek_ticks, wall * 1e3))
            print("                    cells clocked through the move: %u  (counter %s)" %
                  (cell_change - cell_begin,
                   "advanced" if cell_change > cell_begin else "STALLED - check SEEK_PHASE"))
            break
    else:
        print("  %2d cylinder seek: no TRACK_CHANGE inside 5s" % distance)

    link.send(CMD_STREAM_STOP)
    time.sleep(0.05)
    return seek_ticks


def measure_stop_latency(link, mode=MODE_FLUX, trials=5):
    """STREAM_STOP to the last byte out, worst of several trials.

    Two numbers, because they answer different questions. The STREAM_STOP event is the
    device saying "that was the last record"; the last byte is when the tail of the
    pipeline actually arrived. Host read granularity is a few milliseconds either way,
    so this is an upper bound on the device rather than a measurement of it.
    """
    worst_event = 0.0
    worst_byte = 0.0

    for _ in range(trials):
        link.send(CMD_STREAM_START, bytes([mode, 0]))
        time.sleep(0.4)

        # Time bounded. Draining "until a read times out" never finishes while a flux
        # stream is running - records arrive every millisecond - so the harness stopped
        # talking to the device altogether and the watchdog quite correctly ended the
        # session underneath it.
        drain_until = time.time() + 0.2
        while time.time() < drain_until:
            link.read_record(timeout=0.02)

        link.send(CMD_STREAM_STOP)
        t0 = time.time()
        last = t0
        seen = None
        while time.time() - t0 < 0.4:
            r = link.read_record(timeout=0.02)
            if r is None:
                continue
            last = time.time()
            if seen is None and r.type == REC_EVENT and r.event()["event"] == EV_STREAM_STOP:
                seen = last
        worst_event = max(worst_event, ((seen or last) - t0) * 1e3)
        worst_byte = max(worst_byte, (last - t0) * 1e3)

    print("  STREAM_STOP -> stop event : %.2f ms (worst of %d)" % (worst_event, trials))
    print("  STREAM_STOP -> last byte  : %.2f ms (worst of %d)" % (worst_byte, trials))
    print()


def check_write_refused(link):
    link.send(CMD_WRITE_TRACK, bytes([0, 0, 0, 0]))
    r = link.wait_for(REC_NAK, 1.0, sink=lambda _r: None)
    if r and r.payload[0] == CMD_WRITE_TRACK and r.payload[1] == 0x01:
        print("WRITE_TRACK  : correctly refused (UNSUPPORTED)")
    else:
        print("WRITE_TRACK  : UNEXPECTED - it should NAK with UNSUPPORTED")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--seconds", type=int, default=30,
                    help="throughput run length, default 30")
    ap.add_argument("--all", action="store_true", help="run every measurement")
    ap.add_argument("--throughput", choices=["mfm", "flux", "selftest"])
    ap.add_argument("--seek", action="store_true")
    ap.add_argument("--laps", type=int, default=50,
                    help="revolutions to measure cells/rev over, default 50")
    args = ap.parse_args()

    link = Link(args.port)
    try:
        link.enter()
        print("== XCopy Standalone live session ==\n")
        hello = do_hello(link)

        # This harness pauses for seconds at a time between phases, and a paused
        # harness is not a vanished host - which is all the watchdog is there to catch.
        # Widened for the session and put back on the way out.
        link.config(CFG_WATCHDOG_MS, 30000)
        link.wait_for(REC_ACK, 1.0)

        link.send(CMD_MOTOR, bytes([1]))
        time.sleep(0.8)
        do_status(link, "status")
        print()

        if args.all or args.throughput == "selftest":
            measure_throughput(link, MODE_MFM, min(10, args.seconds), hello, selftest=True)
        if args.all or args.throughput == "mfm":
            # 50 laps at ~203 ms is a little over 10 seconds; the default 30 covers it.
            secs = max(args.seconds, int(args.laps * 0.21) + 2)
            measure_throughput(link, MODE_MFM, secs, hello)
        if args.all or args.throughput == "flux":
            measure_throughput(link, MODE_FLUX, args.seconds, hello)

        if args.all or args.seek:
            print("--- seek latency ---")
            measure_seek(link, 1, hello)
            measure_seek(link, 40, hello)
            print()

        if args.all:
            print("--- stream stop latency ---")
            measure_stop_latency(link)
            check_write_refused(link)
            do_status(link, "final", required=False)

    finally:
        try:
            link.config(CFG_WATCHDOG_MS, 5000)
            link.send(CMD_MOTOR, bytes([0]))
            link.send(CMD_BYE)
            time.sleep(0.2)
        except Exception:
            pass
        link.close()


if __name__ == "__main__":
    main()
