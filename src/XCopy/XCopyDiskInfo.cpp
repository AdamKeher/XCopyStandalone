#include "XCopyDiskInfo.h"
#include "XCopyGeometry.h"
#include "XCopyLog.h"
#include "XCopyConsole.h"
#include "XCopySCP.h"
#include "XCopySdFat.h"
#include "XCopyState.h"

/*
   Broadcast buffer.

   Static and appended to in place for the reason drawFlux() spells out: a surface
   line is around 550 bytes, and building it locally meant an allocation and a
   realloc per concatenation in the few KB left between the stream buffer and the
   stack. 168 tracks of that fragmented the heap until the allocator wedged.
*/
static String dinfoData;

//! One hex digit, for the surface profile.
static char dinfoNibble(uint8_t value)
{
    return (char)(value < 10 ? ('0' + value) : ('a' + (value - 10)));
}

void XCopyDiskInfo::begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp, XCopyFloppy *floppy)
{
    _graphics = graphics;
    _audio = audio;
    _esp = esp;
    _floppy = floppy;

    dinfoData.reserve(DINFO_BUCKETS + 64);
}

/*
   Cell bytes in one revolution.

   Measured rather than assumed: a drive running slow writes a longer track, and a
   nominal 200ms would then draw every sector at the wrong angle. A DD cell is 2us
   and an HD cell 1us, so a millisecond of rotation is 62.5 or 125 bytes of cells.
   Integer only - there is no soft double left in this image and this is not the
   place to bring it back.
*/
unsigned long XCopyDiskInfo::revolutionBytes(uint16_t rotMs, bool hd) const
{
    if (rotMs == 0)
        return 0;
    return hd ? ((unsigned long)rotMs * 125UL) : (((unsigned long)rotMs * 125UL) / 2UL);
}

/*
   A byte position in the capture, as 0..4095 of a revolution.

   Wrapped rather than clamped. The capture runs a little over one revolution, so a
   sync mark past the end is the same sector coming round again, and it belongs
   where it physically is - on top of its first appearance - not piled up against
   the end of the track.
*/
uint16_t XCopyDiskInfo::angleOf(unsigned long bytePos, unsigned long revBytes) const
{
    if (revBytes == 0)
        return 0;
    return (uint16_t)(((bytePos % revBytes) * 4096UL) / revBytes);
}

void XCopyDiskInfo::emitBegin(const char *source, const String &name, uint8_t startCyl,
                              uint8_t endCyl, int8_t side, bool hd, bool wrprot, uint16_t rotMs)
{
    // The name goes last: it is a volume or file name and may hold a comma, so the
    // browser takes it as the tail of the line rather than as a split field, the
    // same way it already handles "log" and "wifi".
    _esp->print("broadcast dinfoBegin,");
    _esp->print(source);
    _esp->print("," + String(startCyl) + "," + String(endCyl) + "," + String(side) + "," +
                String(hd ? 1 : 0) + "," + String(wrprot ? 1 : 0) + "," + String(rotMs) + ",");
    _esp->print(name);
    _esp->print("\r\n");
}

void XCopyDiskInfo::emitTrack(uint8_t cylinder, uint8_t head, const CalibrationResult &result,
                              unsigned long revBytes, uint8_t flags)
{
    dinfoData.remove(0);

    /*
       One entry per sync mark rather than per sector number, because this is what
       gets drawn: a mark sits where the head found it, and a track whose sectors
       are not where a formatter would have put them is exactly what the view is
       for. status[] is indexed by sector number and so cannot say that;
       syncSector[] is what pairs the two up.
    */
    for (uint8_t i = 0; i < result.syncs && i < 22; i++)
    {
        if (i > 0)
            dinfoData.concat('|');

        const uint8_t sec = result.syncSector[i];
        dinfoData.concat(sec);
        dinfoData.concat(':');
        dinfoData.concat(sec == 0xff ? (uint8_t)sectorMissing : result.status[sec]);
        dinfoData.concat(':');
        dinfoData.concat(angleOf(_floppy->getSyncBytePos(i), revBytes));
    }

    _esp->print("broadcast dinfoTrack,");
    _esp->print(String(cylinder) + "," + String(head) + "," + String(result.syncs) + "," +
                String(result.valid) + "," + String(result.strays) + "," + String(result.duplicates) + "," +
                String(result.truncated) + "," + String((int)result.cylinderSeen) + "," +
                String(_floppy->getBitCount()) + "," + String(revBytes) + "," +
                String(_expected) + "," + String(flags) + ",");
    _esp->print(dinfoData);
    _esp->print("\r\n");
}

/*
   The disk surface, one line per track.

   DINFO_BUCKETS density levels as hex nibbles. Sized so a track's whole surface is
   one broadcast line: splitting it would mean the browser holding half drawn
   tracks, and a dropped line would leave one permanently half drawn.
*/
void XCopyDiskInfo::emitProfile(uint8_t cylinder, uint8_t head, unsigned long revBytes)
{
    if (revBytes == 0)
        return;

    const unsigned long captured = (unsigned long)_floppy->getStreamPos();
    // Never past what was actually captured. The tail of a short capture is not
    // empty track, it is track that was never looked at, and drawing it as blank
    // would invent a gap that is not on the disk.
    const unsigned long span = captured < revBytes ? captured : revBytes;

    dinfoData.remove(0);
    for (int i = 0; i < DINFO_BUCKETS; i++)
    {
        const unsigned long from = (span * (unsigned long)i) / DINFO_BUCKETS;
        const unsigned long to = (span * (unsigned long)(i + 1)) / DINFO_BUCKETS;
        dinfoData.concat(dinfoNibble(_floppy->bitDensity(from, to)));
    }

    _esp->print("broadcast dinfoProfile,");
    _esp->print(String(cylinder) + "," + String(head) + ",");
    _esp->print(dinfoData);
    _esp->print("\r\n");
}

/*
   The flux interval histogram for the track just captured.

   Free: hist[] is filled by the capture handler, and by the replay through it, so
   this is already sitting there either way. Four raw bins are summed into one and
   the result scaled to a byte, which is what the chart can draw - the absolute
   counts differ by an order of magnitude between a full track and an empty one,
   and it is the shape that says whether the cells cluster at 2, 3 and 4us like
   Amiga MFM or smear like a bad read.
*/
void XCopyDiskInfo::emitHist(uint8_t cylinder, uint8_t head)
{
    const int *hist = _floppy->getHist();

    unsigned long bins[DINFO_HIST_BINS];
    unsigned long peak = 0;
    const int perBin = 256 / DINFO_HIST_BINS;

    for (int i = 0; i < DINFO_HIST_BINS; i++)
    {
        unsigned long total = 0;
        for (int j = 0; j < perBin; j++)
            total += (unsigned long)hist[(i * perBin) + j];
        bins[i] = total;
        if (total > peak)
            peak = total;
    }

    if (peak == 0)
        return;

    dinfoData.remove(0);
    for (int i = 0; i < DINFO_HIST_BINS; i++)
    {
        const uint8_t scaled = (uint8_t)((bins[i] * 255UL) / peak);
        dinfoData.concat(dinfoNibble(scaled >> 4));
        dinfoData.concat(dinfoNibble(scaled & 0x0f));
    }

    _esp->print("broadcast dinfoHist,");
    _esp->print(String(cylinder) + "," + String(head) + ",");
    _esp->print(dinfoData);
    _esp->print("\r\n");
}

void XCopyDiskInfo::emitEnd(uint16_t tracks, uint16_t good, uint16_t bad)
{
    _esp->print("broadcast dinfoEnd,");
    _esp->print(String(tracks) + "," + String(good) + "," + String(bad) + "\r\n");
}

void XCopyDiskInfo::surveyDisk(uint8_t startCyl, uint8_t endCyl, int8_t side)
{
    _cancelOperation = false;

    _esp->setMode("Disk Info");
    _esp->setStatus("Analysing Disk");
    _esp->setState(analyseDisk);
    _esp->setTab("diskinfo");

    if (!_floppy->diskChange())
    {
        Log << XCopyConsole::error("No Disk Inserted") << "\r\n";
        _esp->setStatus("No Disk Inserted");
        _audio->playBong(false);
        return;
    }

    if (endCyl >= MAX_CYLINDERS)
        endCyl = MAX_CYLINDERS - 1;

    const String diskName = _floppy->getName();
    const bool wrprot = _floppy->getWriteProtect();

    /*
       Rotation is measured once, not per track. It does not change across a survey,
       and indexTimer() blocks waiting for two index pulses - 400ms a track, over a
       minute added to a full disk, to re-learn a number already known.
    */
    /*
       readRPM() rather than indexTimer(): it samples index edges from an interrupt
       instead of spinning on the pin, and it is measured here and then stopped, so
       that interrupt is not still firing underneath the flux captures. Nothing
       should share the CPU with ftm0_isr that does not have to.
    */
    _floppy->motorOn();
    _floppy->beginRPM();
    delay(600); // three revolutions at 300 RPM, so the window has something in it
    const float rpm = _floppy->readRPM();
    _floppy->endRPM();
    const int rotMs = (rpm > 1.0f) ? (int)(60000.0f / rpm) : 200;
    /*
       Apply the density once, here, and not per track.

       readTrack() opens every attempt with setMode(), which is what puts the timer
       prescaler and the four interval thresholds in place. surveyCapture() cannot
       do the same: setMode() also resets the sync census bound that is widened
       below, so calling it per track would quietly narrow the census again on
       every one of them. Once, before the loop, also gives the analyser the
       property calibrationRead() wants - thresholds that do not move between
       tracks, so the tracks are comparable.
    */
    _floppy->setMode(_floppy->getMode());
    const bool hd = _floppy->getMode() == HD;
    const unsigned long revBytes = revolutionBytes((uint16_t)rotMs, hd);

    emitBegin("disk", diskName, startCyl, endCyl, side, hd, wrprot, (uint16_t)rotMs);

    /*
       Widen the sync census for the survey.

       The capture handler stops filling sectorTable[] once it holds `sectors`
       marks, so a DD survey would stop looking after eleven and describe a track
       carrying more as if it were ordinary. A protected track is precisely the one
       that carries more. Put back on every path out.
    */
    const byte expected = _floppy->getExpectedSectors();
    _expected = expected;
    _floppy->setExpectedSectors(22);

    uint16_t tracks = 0, good = 0, bad = 0;

    for (uint8_t cyl = startCyl; cyl <= endCyl; cyl++)
    {
        for (uint8_t head = 0; head < 2; head++)
        {
            if (side >= 0 && head != (uint8_t)side)
                continue;

            if (_cancelOperation)
            {
                _floppy->setExpectedSectors(expected);
                emitEnd(tracks, good, bad);
                _esp->setStatus("Cancelled");
                return;
            }

            bool aligned = false;
            CalibrationResult result;

            if (!_floppy->surveyCapture(cyl, head, aligned))
                continue;
            if (!_floppy->censusTrack(cyl, head, result))
                continue;

            uint8_t flags = 0;
            if (aligned)
                flags |= DINFO_ALIGNED;
            if (hd)
                flags |= DINFO_HD;

            emitTrack(cyl, head, result, revBytes, flags);
            emitProfile(cyl, head, revBytes);
            emitHist(cyl, head);

            tracks++;
            good += result.valid;
            bad += (_expected > result.valid) ? (_expected - result.valid) : 0;

            // Paced like the SD listing: two lines a track, around 800 bytes, and
            // the websocket end of the link is the slow one.
            delay(4);
        }
    }

    _floppy->setExpectedSectors(expected);

    emitEnd(tracks, good, bad);
    _esp->setStatus("Analysis Complete - " + String(tracks) + " tracks, " + String(bad) + " bad sectors");
    _audio->playBoing(false);
}

void XCopyDiskInfo::surveyScp(const String &path, uint8_t startCyl, uint8_t endCyl, int8_t side)
{
    _cancelOperation = false;

    _esp->setMode("Disk Info");
    _esp->setStatus("Reading " + path);
    _esp->setState(analyseDisk);
    _esp->setTab("diskinfo");

    if (!xcopySdBegin())
    {
        Log << XCopyConsole::error("No SD Card") << "\r\n";
        _esp->setStatus("No SD Card");
        return;
    }

    File file;
    if (!file.open(path.c_str(), O_RDONLY))
    {
        Log << XCopyConsole::error("Cannot open " + path) << "\r\n";
        _esp->setStatus("Cannot open " + path);
        return;
    }

    XCopySCPReader reader;
    if (!reader.begin(&file))
    {
        file.close();
        Log << XCopyConsole::error("Not an SCP image: " + path) << "\r\n";
        _esp->setStatus("Not an SCP image");
        return;
    }

    if (endCyl >= MAX_CYLINDERS)
        endCyl = MAX_CYLINDERS - 1;

    /*
       An image is replayed at DD, and says so.

       The thresholds feedFluxSample() measures against are whichever density the
       drive was last set to, so a survey following an HD disk would judge a DD
       image by HD thresholds and decode noise. Setting the mode makes the replay
       independent of whatever ran before it. HD images are a one line change here
       once there is one to test against, exactly as diskToSCP() notes for the
       capture side.

       Deliberately without diskToSCP()'s setAutoDensity(false): that outlives the
       operation and would leave the next disk in the drive judged by whatever
       density this image happened to be. Nothing in a replay calls
       densityDetect(), so there is nothing here to suppress.
    */
    _floppy->setMode(DD);
    const bool hd = false;

    // An SCP revolution is index to index by definition, so its flux is always
    // angle aligned - the one thing a drive capture has to work for.
    emitBegin("scp", path, startCyl, endCyl, side, hd, true, 0);

    const byte expected = _floppy->getExpectedSectors();
    _expected = expected;
    _floppy->setExpectedSectors(22);

    uint16_t tracks = 0, good = 0, bad = 0;

    for (uint8_t cyl = startCyl; cyl <= endCyl && !_cancelOperation; cyl++)
    {
        for (uint8_t head = 0; head < 2 && !_cancelOperation; head++)
        {
            if (side >= 0 && head != (uint8_t)side)
                continue;

            const uint8_t scpTrack = (uint8_t)((cyl * 2) + head);

            if (!reader.beginFlux(scpTrack, 0))
                continue;

            _floppy->beginReplay();

            /*
               Replay the revolution through the capture handler's own decoder.

               128 samples at a time: 256 bytes of stack against a part with about
               six free KB, and deliberately not an XCopyScratch borrow - the arena
               IS the stream buffer, which is where these samples are going.

               An SCP sample of 0x0000 is a full 65,536 tick gap rather than a
               transition, and scpFluxDuration() accumulates it into the next real
               one. The accumulator lives out here because a gap can span a buffer
               boundary.
            */
            uint16_t samples[128];
            uint32_t pending = 0;
            int count;
            bool full = false;
            while (!full && (count = reader.readFlux(samples, 128)) > 0)
            {
                for (int i = 0; i < count; i++)
                {
                    pending = scpFluxDuration(pending, samples[i]);
                    if (samples[i] == 0)
                        continue; // still accumulating a long gap

                    // 25ns units to FTM0 ticks. A DD tick is 41.67ns, so 3/5 - the
                    // inverse of the scale XCopySCPWriter converts by on the way in.
                    if (!_floppy->feedFluxSample((pending * 3UL) / 5UL))
                    {
                        full = true;
                        break;
                    }
                    pending = 0;
                }
            }

            CalibrationResult result;
            if (!_floppy->censusTrack(cyl, head, result))
                continue;

            /*
               Revolution length in cells, from the file's own index to index time
               rather than from a drive that is not turning. A 2us DD cell is 80
               units of 25ns, and eight cells to the byte.
            */
            const unsigned long revBytes = reader.revolutionTicks() / (80UL * 8UL);

            emitTrack(cyl, head, result, revBytes, DINFO_ALIGNED | DINFO_FROMFILE);
            emitProfile(cyl, head, revBytes);
            emitHist(cyl, head);

            tracks++;
            good += result.valid;
            bad += (_expected > result.valid) ? (_expected - result.valid) : 0;

            delay(4);
        }
    }

    _floppy->setExpectedSectors(expected);
    file.close();

    emitEnd(tracks, good, bad);
    if (_cancelOperation)
        _esp->setStatus("Cancelled");
    else
        _esp->setStatus("Analysis Complete - " + String(tracks) + " tracks, " + String(bad) + " bad sectors");
}
