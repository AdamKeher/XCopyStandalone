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

    // The profile is DINFO_BUCKETS characters and the census list is at most
    // FLOPPY_MAX_SYNCS entries of "255:5:4095|", so the larger of the two decides.
    // 512 data bytes as hex is the longest line this sends, past the surface
    // profile and past the census list.
    dinfoData.reserve(1040);
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
                              unsigned long revBytes, unsigned long revUs, uint8_t flags)
{
    dinfoData.remove(0);

    /*
       One entry per sync mark rather than per sector number, because this is what
       gets drawn: a mark sits where the head found it, and a track whose sectors
       are not where a formatter would have put them is exactly what the view is
       for. status[] is indexed by sector number and so cannot say that;
       syncSector[] is what pairs the two up.
    */
    for (uint8_t i = 0; i < result.syncs && i < FLOPPY_MAX_SYNCS; i++)
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
                String(result.valid) + "," + String(result.bad) + "," +
                String(result.strays) + "," + String(result.duplicates) + "," +
                String(result.truncated) + "," + String(result.overflow) + "," +
                String((int)result.cylinderSeen) + "," +
                String(_floppy->getBitCount()) + "," + String(revBytes) + "," +
                String(revUs) + "," + String(_floppy->measureCellPeriod()) + "," +
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

/*
   One track's verdicts, added to the disk's running totals.

   Three counts and not two, because "bad" and "not there" are different findings
   and a disk that is mostly blank is the case that shows why. Reporting eleven
   failures for a track that was never formatted put 836 bad sectors on a disk
   whose every readable sector was fine, which is not a summary of the disk but of
   the assumption that it should have held one.

   valid and bad come from the census. What is left of a standard format's sector
   count is missing: a real quantity - it is what a copier would go looking for and
   not find - and reported as its own number so it cannot be read as damage.
*/
void XCopyDiskInfo::tally(const CalibrationResult &result, uint16_t &good, uint16_t &bad,
                          uint16_t &missing) const
{
    good += result.valid;
    bad += result.bad;

    const uint16_t seen = (uint16_t)result.valid + (uint16_t)result.bad;
    if (_expected > seen)
        missing += (uint16_t)(_expected - seen);
}

//! The end of survey line, worded so the three counts cannot be confused.
String XCopyDiskInfo::summary(uint16_t tracks, uint16_t good, uint16_t bad, uint16_t missing) const
{
    return "Analysis Complete - " + String(tracks) + " tracks, " + String(good) + " sectors, " +
           String(bad) + " bad, " + String(missing) + " missing";
}

void XCopyDiskInfo::emitEnd(uint16_t tracks, uint16_t good, uint16_t bad, uint16_t missing)
{
    _esp->print("broadcast dinfoEnd,");
    _esp->print(String(tracks) + "," + String(good) + "," + String(bad) + "," +
                String(missing) + "\r\n");
}

/*
   The colour one surveyed track is drawn in on the panel.

   The same four way split the browser's dinfoTint() paints the surface with, so
   the device and the web view agree about what they are looking at. Blue rather
   than red for a track with no credible header: the survey exists to describe
   disks a copier gives up on, and an unformatted track is a finding and not a
   failure.
*/
uint16_t XCopyDiskInfo::trackColour(const CalibrationResult &result) const
{
    if (result.cylinderSeen < 0)
        return ST7735_BLUE;                       // no header read - noise or blank
    if (result.valid >= _expected)
        return ST7735_GREEN;                      // a full track
    if (result.valid == 0)
        return ST7735_RED;                        // headers, but nothing came out
    return ST7735_ORANGE;                         // partly readable
}

/*
   Put the panel into the state the copy paths leave it in before a long job.

   Without this the survey ran with whatever was last on the screen, which for a
   scan started from the browser is the idle screen - so the device looked asleep
   for the two minutes it takes to read 168 tracks. drawDisk() lays out the empty
   grid and every track then fills its own cell in, which is the feedback: the
   shape of the disk appearing, and its colours saying what was found.
*/
void XCopyDiskInfo::beginPanel(const String &name)
{
    _graphics->clearScreen();
    _graphics->bmpDraw("XCPYLOGO.BMP", 0, 87);
    _graphics->drawDiskName(name);
    _graphics->drawDisk(0, ST7735_BLACK);
    _graphics->drawText(0, 10, ST7735_WHITE, "ANALYSING", true);
}

/*
   Replay one revolution of SCP flux through the capture handler's own decoder.

   128 samples at a time: 256 bytes of stack against a part with about six free KB,
   and deliberately not an XCopyScratch borrow - the arena IS the stream buffer,
   which is where these samples are going.

   An SCP sample of 0x0000 is a full 65,536 tick gap rather than a transition, and
   scpFluxDuration() accumulates it into the next real one. The accumulator lives
   out here because a gap can span a buffer boundary.

   @param scale the header's resolution multiplier, from XCopySCPReader
*/
void XCopyDiskInfo::replayScpTrack(XCopySCPReader &reader, uint32_t scale)
{
    _floppy->beginReplay();

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

            /*
               Sample units to FTM0 ticks. A DD tick is 41.67ns against the 25ns
               base unit, so 3/5 - the inverse of the scale XCopySCPWriter converts
               by on the way in - after the header's own resolution multiplier.

               Bounded first. The product would otherwise overflow 32 bits on a long
               gap in a coarse image, and nothing above a few hundred units can
               survive the thresholds anyway: high4 is 255 ticks, which is 425 base
               units, so 4096 is already far into the range feedFluxSample() throws
               away.
            */
            uint32_t units = pending;
            if (units > 4096UL)
                units = 4096UL;
            if (!_floppy->feedFluxSample((units * scale * 3UL) / 5UL))
            {
                full = true;
                break;
            }
            pending = 0;
        }
    }
}

/*
   Get one track back into stream[], from whichever source the survey used.

   Cheap when it is already there, which is the case that matters: a pointer moving
   along the track view asks for a different sector of the same track over and over,
   and only the first of those may touch the drive.
*/
bool XCopyDiskInfo::loadTrack(uint8_t cyl, uint8_t head)
{
    if (_loadedCyl == (int)cyl && _loadedHead == (int)head)
        return true;

    // Widened for the same reasons a survey widens it, and put back the same way:
    // this is describing whatever is on the track, not reading a known format.
    const byte expected = _floppy->getExpectedSectors();
    const byte syncs = _floppy->getSyncLimit();
    _floppy->setExpectedSectors(22);
    _floppy->setSyncLimit(FLOPPY_MAX_SYNCS);

    bool ok = false;

    if (_srcFromFile)
    {
        File file;
        if (xcopySdBegin() && file.open(_srcPath.c_str(), O_RDONLY))
        {
            XCopySCPReader reader;
            if (reader.begin(&file) && reader.beginFlux((uint8_t)((cyl * 2) + head), 0))
            {
                _floppy->setMode(DD); // as surveyScp does, and for the same reason
                replayScpTrack(reader, reader.sampleScale());
                ok = true;
            }
            file.close();
        }
    }
    else
    {
        bool aligned = false;
        ok = _floppy->surveyCapture(cyl, head, aligned);
    }

    _floppy->setExpectedSectors(expected);
    _floppy->setSyncLimit(syncs);

    _loadedCyl = ok ? (int)cyl : -1;
    _loadedHead = ok ? (int)head : -1;
    return ok;
}

/*
   One sector, decoded and sent.

   The data goes out as hex two bytes at a time rather than through a buffer: 512
   bytes is a tenth of the free RAM here, and it is only ever going to become 1024
   characters of text anyway.
*/
void XCopyDiskInfo::emitSectorLine(uint8_t cyl, uint8_t head, uint8_t sector)
{
    SectorDump d;
    const bool found = _floppy->dumpSector(sector, d);

    dinfoData.remove(0);
    if (found)
    {
        for (int i = 0; i < 256; i++)
        {
            const uint16_t pair = _floppy->sectorDataPair(d.bytePos, i);
            dinfoData.concat(dinfoNibble((pair >> 12) & 0x0f));
            dinfoData.concat(dinfoNibble((pair >> 8) & 0x0f));
            dinfoData.concat(dinfoNibble((pair >> 4) & 0x0f));
            dinfoData.concat(dinfoNibble(pair & 0x0f));
        }
    }

    /*
       Cell positions, which is how a flux viewer talks about where a sector sits.

       The capture starts at the index, so a byte position in stream[] is already
       an offset from it, and there are eight cells to the byte. The data begins 64
       bytes past the sync mark - eight of sync and magic, then the header, its
       checksum and the data checksum - and the whole sector is 1088.
    */
    const unsigned long startCell = d.bytePos * 8UL;

    _esp->print("broadcast dinfoSector,");
    _esp->print(String(cyl) + "," + String(head) + "," + String(sector) + "," +
                String(found ? 1 : 0) + "," + String(d.formatByte) + "," +
                String(d.trackId) + "," + String(d.toGap) + "," +
                String(d.headerSum) + "," + String(d.headerCalc) + "," +
                String(d.dataSum) + "," + String(d.dataCalc) + "," +
                String(startCell) + "," + String(startCell + (64UL * 8UL)) + "," +
                String(startCell + (1088UL * 8UL)) + ",");
    _esp->print(dinfoData);
    _esp->print("\r\n");
}

void XCopyDiskInfo::sectorDump(uint8_t cyl, uint8_t head, uint8_t sector)
{
    if (cyl >= MAX_CYLINDERS || head > 1)
        return;

    // No survey has run, so there is no source and the request means nothing.
    // Silent rather than an error: a browser tab left open on the tab should not
    // be able to start a drive that nobody has asked to read.
    if (!_srcFromFile && _srcPath.length() == 0 && _loadedCyl < 0)
        return;

    if (!loadTrack(cyl, head))
        return;

    emitSectorLine(cyl, head, sector);
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

    _srcFromFile = false;
    _srcPath = "";
    _loadedCyl = -1;
    _loadedHead = -1;

    beginPanel(diskName);
    emitBegin("disk", diskName, startCyl, endCyl, side, hd, wrprot, (uint16_t)rotMs);

    /*
       Widen the census for the survey, in both of the ways it is narrowed.

       The sector bound first: censusTrack() calls a header naming anything at or
       above it a stray, so a DD survey would dismiss the twelfth sector of a track
       that carries one. A protected track is precisely the one that carries one.

       Both are put back on every path out.
    */
    const byte expected = _floppy->getExpectedSectors();
    const byte syncs = _floppy->getSyncLimit();
    _expected = expected;
    _floppy->setExpectedSectors(22);
    /*
       And the sync table, which is the bound that actually bit. setExpectedSectors()
       lowers the sync limit to match, so this has to come second - and 22 was never
       the right number for it anyway: an unformatted surface throws sync marks by
       accident, and a table bounded by the sector count spends itself on those
       before reaching the sectors that are really there. See FLOPPY_MAX_SYNCS.
    */
    _floppy->setSyncLimit(FLOPPY_MAX_SYNCS);

    uint16_t tracks = 0, good = 0, bad = 0, missing = 0;

    for (uint8_t cyl = startCyl; cyl <= endCyl; cyl++)
    {
        for (uint8_t head = 0; head < 2; head++)
        {
            if (side >= 0 && head != (uint8_t)side)
                continue;

            if (_cancelOperation)
            {
                _floppy->setExpectedSectors(expected);
                _floppy->setSyncLimit(syncs);
                emitEnd(tracks, good, bad, missing);
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

            _loadedCyl = cyl;
            _loadedHead = head;
            _graphics->drawTrack(cyl, head, true, false, 0, false, trackColour(result));

            emitTrack(cyl, head, result, revBytes, (unsigned long)rotMs * 1000UL, flags);
            emitProfile(cyl, head, revBytes);
            emitHist(cyl, head);

            tracks++;
            tally(result, good, bad, missing);

            // Paced like the SD listing: two lines a track, around 800 bytes, and
            // the websocket end of the link is the slow one.
            delay(4);
        }
    }

    _floppy->setExpectedSectors(expected);
    _floppy->setSyncLimit(syncs);

    emitEnd(tracks, good, bad, missing);
    _esp->setStatus(summary(tracks, good, bad, missing));
    _graphics->drawText(0, 10, ST7735_WHITE, "ANALYSED " + String(tracks), true);
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

    /*
       Write protect comes from the image, not from the fact that it is one.

       An image cannot be written back through the drive whatever its header says,
       so it is tempting to report every one of them protected. But this is the
       field a viewer prints as WrProt, and it is describing the disk the image was
       taken from - saying ON for a disk that was not protected is inventing a
       property of somebody's floppy.
    */
    // An SCP revolution is index to index by definition, so its flux is always
    // angle aligned - the one thing a drive capture has to work for.
    // The file's own name for the panel, not the whole path: a 160 pixel screen has
    // room for a name and not for the directory it sits in.
    _srcFromFile = true;
    _srcPath = path;
    _loadedCyl = -1;
    _loadedHead = -1;

    beginPanel(path.substring(path.lastIndexOf('/') + 1));
    emitBegin("scp", path, startCyl, endCyl, side, hd, !reader.writable(), 0);

    // 25ns units per flux sample. One in an ordinary image; a coarser capture says
    // so in the header, and reading it as 25ns would decode every interval into the
    // wrong threshold bucket and report the whole disk as noise.
    const uint32_t scale = reader.sampleScale();

    const byte expected = _floppy->getExpectedSectors();
    const byte syncs = _floppy->getSyncLimit();
    _expected = expected;
    _floppy->setExpectedSectors(22);
    _floppy->setSyncLimit(FLOPPY_MAX_SYNCS); // see surveyDisk()

    uint16_t tracks = 0, good = 0, bad = 0, missing = 0;

    for (uint8_t cyl = startCyl; cyl <= endCyl && !_cancelOperation; cyl++)
    {
        for (uint8_t head = 0; head < 2 && !_cancelOperation; head++)
        {
            if (side >= 0 && head != (uint8_t)side)
                continue;

            const uint8_t scpTrack = (uint8_t)((cyl * 2) + head);

            if (!reader.beginFlux(scpTrack, 0))
                continue;

            replayScpTrack(reader, scale);

            CalibrationResult result;
            if (!_floppy->censusTrack(cyl, head, result))
                continue;

            /*
               Revolution length in cells, from the file's own index to index time
               rather than from a drive that is not turning. A 2us DD cell is 80
               units of 25ns, and eight cells to the byte.
            */
            // Rounded, not truncated: this is divided by 640 and then drawn back
            // out multiplied by 8, so a bias here is a bias in every angle on the
            // track rather than a rounding error in one number.
            const unsigned long revTicks = reader.revolutionTicks();
            const unsigned long revBytes = (revTicks + 320UL) / (80UL * 8UL);

            /*
               And the rotation itself, which is the file's own index to index time
               in 25ns units - 40 of them to a microsecond. Reported per track rather
               than per disk because that is what it is: a drive's speed wanders
               across a capture, and it is the one number here that can show it.
            */
            _loadedCyl = cyl;
            _loadedHead = head;
            _graphics->drawTrack(cyl, head, true, false, 0, false, trackColour(result));

            emitTrack(cyl, head, result, revBytes, revTicks / 40UL,
                      DINFO_ALIGNED | DINFO_FROMFILE);
            emitProfile(cyl, head, revBytes);
            emitHist(cyl, head);

            tracks++;
            tally(result, good, bad, missing);

            delay(4);
        }
    }

    _floppy->setExpectedSectors(expected);
    _floppy->setSyncLimit(syncs);
    file.close();

    emitEnd(tracks, good, bad, missing);
    if (_cancelOperation)
        _esp->setStatus("Cancelled");
    else
        _esp->setStatus(summary(tracks, good, bad, missing));
    _graphics->drawText(0, 10, ST7735_WHITE,
                        _cancelOperation ? "CANCELLED" : ("ANALYSED " + String(tracks)), true);
}
