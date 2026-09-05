#include "XCopyDriveToolkit.h"
#include "XCopyConsole.h"
#include "XCopyLog.h"

/*
   Screen layout, 160 x 128 landscape.

   Two rows of indicators rather than one: the six outputs sit above the five
   inputs, because "what am I driving" and "what is the drive saying" are the two
   halves of every question asked here, and a single strip of eleven would put them
   in one undifferentiated line.

   The strip costs twice what the calibration screen's six boxes did, and the room
   comes from the title row. Eleven labelled boxes over a settings list name the
   screen better than a caption did, and the two glyph rows underneath were the
   only other candidate - they cannot go, they are the measurement.

   Rows are tabulated rather than open coded into every draw call because the TFT
   is repainted in place: a cell is cleared and rewritten on its own, and a y that
   disagreed between the clear and the write would leave debris.
*/
static const uint8_t ROW_OUT_BOX = 0;
static const uint8_t ROW_OUT_TEXT = 10;
static const uint8_t ROW_IN_BOX = 21;
static const uint8_t ROW_IN_TEXT = 31;
static const uint8_t ROW_SETTINGS1 = 42;
static const uint8_t ROW_SETTINGS2 = 52;
static const uint8_t ROW_SETTINGS3 = 62;
static const uint8_t ROW_HEAD0 = 74;
static const uint8_t ROW_HEAD1 = 84;
static const uint8_t ROW_PASS = 96;
static const uint8_t ROW_HELP = 108;
static const uint8_t ROW_STATUS = 118;

static const uint8_t BOX_W = 20;
static const uint8_t BOX_H = 8;
static const uint8_t BOX_X[6] = {0, 25, 50, 75, 100, 125};

// The sector glyph row: eleven on a 9 pixel pitch, with the count to its right.
static const uint8_t GLYPH_X = 14;
static const uint8_t GLYPH_PITCH = 9;
static const uint8_t COUNT_X = 122;

//! No ST7735_GREY in the library, and a line at rest has to be visibly different
//! from one that is asserted without being alarming. Mid grey in RGB565.
static const uint16_t DT_GREY = 0x4208;

// Console colours, paired one to one with the TFT colours below so the panel and
// the screen describe the same drive in the same language. Same set XCopyTrackMap
// uses, for the same reason.
#define DT_RESET   "\033[0m"
#define DT_DIM     "\033[0;90m"
#define DT_WHITE   "\033[1;37m"
#define DT_GREEN   "\033[0;32m"
#define DT_YELLOW  "\033[0;33m"
#define DT_RED     "\033[1;31m"
#define DT_MAGENTA "\033[0;35m"
#define DT_CYAN    "\033[0;36m"

static const char SPINNER[4] = {'\\', '|', '/', '-'};

/*
   Four characters at most, so one table serves the console and the TFT both. The
   TFT draws these under a 20 pixel box on a 25 pixel pitch, and a fifth character
   would overrun into its neighbour.
*/
static const char *const SIG_LABEL[XCopyDriveToolkit::sigCount] = {
    "SEL", "MOT", "DIR", "SIDE", "DENS", "STEP",
    "IDX", "RDAT", "T0", "WP", "CHG"};

static const uint8_t SIG_PIN[XCopyDriveToolkit::sigCount] = {
    12, 16, 18, 32, 2, 20, 8, 30, 26, 28, 34};

/*
   The focusable settings, in cursor order. One table, so the focus cycle, the
   labels and the cells they are drawn in cannot drift apart from each other.

   The line controls are deliberately absent - see the class comment. Every x and
   width here is unchanged from the calibration screen it came from; only the
   three y values moved, to make room for the second bank of indicators.
*/
struct FieldSlot
{
    uint8_t labelX;
    uint8_t valueX;
    uint8_t y;
    uint8_t valueW;
    const char *label;
};

static const FieldSlot FIELD_SLOT[] = {
    {0, 32, ROW_SETTINGS1, 34, "Cyl:"},   // Field::cylinder
    {70, 108, ROW_SETTINGS1, 30, "Stp:"}, // Field::step
    {0, 32, ROW_SETTINGS2, 44, "Hed:"},   // Field::head
    {80, 116, ROW_SETTINGS2, 34, "Aut:"}, // Field::autoReseek
    {0, 32, ROW_SETTINGS3, 44, "Snd:"},   // Field::sound
    {80, 116, ROW_SETTINGS3, 34, "Rd :"}, // Field::run
};

//! printf() on this core has no float support linked in, so "%.1f" prints nothing
//! at all. Every speed on every surface goes through here instead.
static void formatRpm(float rpm, char *out, size_t length)
{
    if (rpm <= 0.0f)
    {
        snprintf(out, length, "-- RPM");
        return;
    }
    const int tenths = (int)((rpm * 10.0f) + 0.5f);
    snprintf(out, length, "%d.%d RPM", tenths / 10, tenths % 10);
}

// SESSION

void XCopyDriveToolkit::begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp,
                              XCopyFloppy *floppy, uint8_t cylinder, bool startReading)
{
    _graphics = graphics;
    _audio = audio;
    _esp = esp;
    _floppy = floppy;

    /*
       Opens with every output released, and nothing spinning.

       This is the half of the merge that changes behaviour: the calibration test
       used to open by calling motorOn(). A diagnostic that starts by asserting
       things destroys the first measurement it is asked for - the question is what
       this drive does when a line is driven, and that needs a known quiet starting
       point to be a question at all. The read layer asserts what it needs when it
       starts, which is where that belongs.
    */
    _sel = false;
    _mot = false;
    _inward = false;
    _sideOne = false;
    _high = true;
    _sticky = false;
    _cylinder = cylinder > kMaxCylinder ? kMaxCylinder : cylinder;
    _headKnown = false;
    _stepPulses = 0;
    _rpm = 0.0f;
    _idxEdges = 0;
    _rdataActive = false;
    _panelActive = false;
    _drawnStatic = false;

    _read = Read::off;
    _stepSize = 1;
    _head = HeadSel::both;
    _autoReseek = false;
    _field = Field::cylinder;
    _phase = Phase::settle;
    _passes = 0;
    _spinner = 0;
    _recalPending = true;
    _tainted = false;
    _taintedPasses = 0;
    _resultTainted[0] = false;
    _resultTainted[1] = false;
    _resultValid[0] = false;
    _resultValid[1] = false;
    _nextStepMs = millis();

    // The tally is the session, so it starts empty every time the screen is opened.
    memset(_tally, 0, sizeof(_tally));
    _markedCylinder = -1;

    memset(_moved, 0, sizeof(_moved));
    memset(_previous, 0, sizeof(_previous));
    memset(_raw, 0, sizeof(_raw));
    for (uint8_t i = 0; i < sigCount; i++)
        _level[i] = Level::rest;

    _floppy->setSelectLine(false);
    _floppy->setMotorLine(false);
    _floppy->setDirFast(0);
    _floppy->setSideFast(0);
    _floppy->setDensityLine(true);

    /*
       Put the density thresholds back where setMode() left them. An earlier read
       may have gone through adjustTimings(), and a calibration session is a
       comparison between consecutive passes - if the decoder moves underneath it,
       a change on screen no longer means the hand on the screwdriver caused it.
    */
    _floppy->setMode(_floppy->getMode());

    /*
       The idle timeout would stop the motor five seconds into a session where the
       whole point may be watching a spindle that has been asked to run, and nobody
       is touching a control to keep it up while they have both hands on the drive.
       The toolkit owns the motor line for the duration and puts this back in end().
    */
    _floppy->setMotorIdleOff(false);
    /*
       beginRPM() owns the index interrupt and endRPM() gives it back, so the index
       indicator, the edge count and the speed readout all cost no interrupt of our
       own - and there is nothing left for an exit path to forget to detach. The
       feature both halves of this replaced attached three interrupts in begin(),
       detached none of them, and was then deleted with all three still armed.
    */
    _floppy->clearIndexEdges();
    _floppy->beginRPM();

    _diskPresent = _floppy->readDiskChangeLine();
    _nextSampleMs = millis();
    _active = true;
    sample();

    if (startReading)
        setReading(true);
}

void XCopyDriveToolkit::end()
{
    if (!_active)
        return;

    releaseOutputs();
    _floppy->endRPM();
    _floppy->setMotorIdleOff(true);
    _floppy->motorOff();
    _active = false;
}

void XCopyDriveToolkit::update()
{
    if (!_active)
        return;

    /*
       The sampler. Unconditional, on its own clock, whether or not a read is
       running - the interface is what this screen is, and the read is a layer over
       it. A capture blocks for up to 300ms plus a seek, so the sampler is starved
       for at most one pass and then picks its cadence straight back up.

       It also means the paused and no-disk branches below have nothing to repaint:
       the speed, the lines and the disk-present check are all coming from here
       several times a second, which is what says the drive is still there rather
       than that the panel has crashed.
    */
    if ((int32_t)(millis() - _nextSampleMs) >= 0)
    {
        _nextSampleMs = millis() + kSampleMs;
        sample();
        drawSignals();
        drawPassLine();
        drawStatusLine();
        paintSettings();
        paintSignals();
        paintStatus();
        sendState();
    }

    if (_read != Read::run)
        return;

    if ((int32_t)(millis() - _nextStepMs) < 0)
        return;

    bool present = _floppy->readDiskChangeLine();
    if (present != _diskPresent)
    {
        _diskPresent = present;
        _resultValid[0] = false;
        _resultValid[1] = false;
    }

    if (!present)
    {
        _nextStepMs = millis() + kNoDiskGapMs;
        _phase = Phase::settle;
        return;
    }

    switch (_phase)
    {
    /*
       Take the spin up here rather than inside calibrationRead().

       calibrationRead() opens with motorOn(), which blocks for motorSpinupMs -
       600ms - whenever the motor flag is false. The line controls are live during
       a read, so an operator releasing MOTOR mid pass would otherwise stall the
       whole main loop on the very next capture. Waiting on the phase clock instead
       keeps the console and the ESP pumps running through it, and motorOn() then
       finds the flag already set and returns immediately.
    */
    case Phase::spinup:
        if (!_floppy->getMotorStatus())
        {
            _floppy->setSelectLine(true);
            _floppy->setMotorLine(true);
            _sel = true;
            _mot = true;
            _nextStepMs = millis() + _floppy->getMotorSpinupMs();
            _tainted = true;
            _phase = Phase::settle;
            return;
        }
        _phase = Phase::settle;
        // fall through: nothing to wait for.

    case Phase::settle:
        // A line touched from here to the end of the pass taints both its sides.
        _tainted = false;
        _resultTainted[0] = false;
        _resultTainted[1] = false;
        _phase = (_head == HeadSel::upper) ? Phase::readUpper : Phase::readLower;
        break;

    case Phase::readLower:
        if (_head != HeadSel::upper)
            readSide(0);
        _phase = Phase::readUpper;
        break;

    case Phase::readUpper:
        if (_head != HeadSel::lower)
            readSide(1);
        _passes++;
        if (_tainted)
            _taintedPasses++;
        _spinner = (_spinner + 1) & 3;
        // Auto re-seek puts the head back every pass. With it off the head is
        // deliberately left where it is, which is what lets an adjustment show up
        // rather than being hidden by a fresh seek.
        if (_autoReseek)
            _recalPending = true;
        _nextStepMs = millis() + kPassGapMs;
        _phase = _floppy->getMotorStatus() ? Phase::settle : Phase::spinup;
        publish();
        break;
    }
}

// SAMPLING

void XCopyDriveToolkit::sample()
{
    _rpm = _floppy->readRPM();
    _idxEdges = _floppy->getIndexEdges();
    _rdataActive = _floppy->readDataActive();

    _raw[sigSel] = _floppy->readSelectLine();
    _raw[sigMot] = _floppy->readMotorLine();
    _raw[sigDir] = _floppy->readDirInward();
    _raw[sigSide] = _floppy->readSideLower();
    _raw[sigDens] = _floppy->readDensityLine();
    // STEP idles high and is pulsed for microseconds, so sampling its level would
    // read low essentially never. The count is the signal here, not the level.
    _raw[sigStep] = false;
    _raw[sigIdx] = _idxEdges > 0;
    _raw[sigRdata] = _rdataActive;
    _raw[sigT0] = _floppy->readTrack0Line();
    _raw[sigWp] = _floppy->readWriteProtectLine();
    _raw[sigChg] = _floppy->readDiskChangeLine();

    for (uint8_t i = 0; i < sigCount; i++)
    {
        if (_raw[i] != _previous[i])
            _moved[i] = true;
        _previous[i] = _raw[i];
    }

    // Outputs report what we are driving; inputs report whether the drive is
    // saying what it should. See Level.
    _level[sigSel] = _raw[sigSel] ? Level::asserted : Level::rest;
    _level[sigMot] = _raw[sigMot] ? Level::asserted : Level::rest;
    _level[sigDir] = Level::rest;
    _level[sigSide] = Level::rest;
    _level[sigDens] = Level::rest;
    _level[sigStep] = _stepPulses > 0 ? Level::asserted : Level::rest;

    /*
       Index is the one line the session usually exists for, so it gets the only
       real verdict on the strip: red specifically when the motor has been asked to
       run and nothing has arrived, which is the fault being hunted. With the motor
       off there is nothing to expect, so it rests rather than alarms.
    */
    if (_raw[sigIdx])
        _level[sigIdx] = Level::good;
    else
        _level[sigIdx] = _mot ? Level::fault : Level::rest;

    _level[sigRdata] = _raw[sigRdata] ? Level::good : Level::rest;
    _level[sigT0] = _raw[sigT0] ? Level::good : Level::rest;
    // Write protect is a fact about the disk rather than a fault.
    _level[sigWp] = _raw[sigWp] ? Level::asserted : Level::rest;
    _level[sigChg] = _raw[sigChg] ? Level::good : Level::rest;

    if (_sticky)
    {
        for (uint8_t i = 0; i < sigCount; i++)
            if (_moved[i] && _level[i] == Level::rest)
                _level[i] = Level::good;
    }
}

const char *XCopyDriveToolkit::stateText(uint8_t sig) const
{
    switch (sig)
    {
    case sigSel:
        return _raw[sigSel] ? "ASSERTED" : "RELEASED";
    case sigMot:
        return _raw[sigMot] ? "ASSERTED" : "RELEASED";
    case sigDir:
        return _raw[sigDir] ? "INWARD" : "OUTWARD";
    case sigSide:
        return _raw[sigSide] ? "HEAD 1" : "HEAD 0";
    case sigDens:
        return _raw[sigDens] ? "HIGH" : "LOW";
    case sigStep:
        return "IDLE";
    case sigIdx:
        return _raw[sigIdx] ? "PULSING" : (_mot ? "NO PULSES" : "IDLE");
    case sigRdata:
        return _raw[sigRdata] ? "ACTIVE" : "QUIET";
    case sigT0:
        return _raw[sigT0] ? "AT CYL 0" : "OFF CYL 0";
    case sigWp:
        return _raw[sigWp] ? "PROTECTED" : "WRITABLE";
    default:
        return _raw[sigChg] ? "DISK IN" : "NO DISK";
    }
}

void XCopyDriveToolkit::valueText(uint8_t sig, char *out, size_t length) const
{
    switch (sig)
    {
    case sigStep:
        snprintf(out, length, "%lu pulses", (unsigned long)_stepPulses);
        break;
    case sigIdx:
    {
        char speed[16];
        formatRpm(_rpm, speed, sizeof(speed));
        // The count is what says whether anything arrived at all; the rate needs
        // two edges inside the stall window before it can say anything.
        if (_rpm > 0.0f)
            snprintf(out, length, "%s", speed);
        else
            snprintf(out, length, "%lu edges", (unsigned long)_idxEdges);
        break;
    }
    case sigSide:
        if (!_headKnown)
            snprintf(out, length, "cyl ?");
        else
            snprintf(out, length, "cyl %d", (int)_cylinder);
        break;
    default:
        out[0] = '\0';
        break;
    }
}

// THE READ LAYER

void XCopyDriveToolkit::readSide(uint8_t side)
{
    CalibrationResult result;
    bool ok = _floppy->calibrationRead(_cylinder, side, _recalPending, result);
    // The re-seek belongs to the pass, not to the side, so the first read of a
    // pass consumes it and the second stays on the cylinder it just reached.
    _recalPending = false;
    // calibrationRead() went through gotoLogicTrack(), so wherever the head was
    // before, it is on the cylinder we asked for now.
    _headKnown = true;
    _sel = _floppy->readSelectLine();
    _mot = _floppy->readMotorLine();

    if (!ok)
    {
        _resultValid[side] = false;
        return;
    }

    _results[side] = result;
    _resultValid[side] = true;
    _resultTainted[side] = _tainted;

    /*
       Accumulate, unless a line was driven during the pass.

       A pass taken with select or motor dropped halfway through is not a
       measurement of head alignment. Counting the error would call the operator's
       finger a head fault; counting the pass without one would call it a clean
       read. So the grid is not touched at all - the glyph row above still shows
       what came off the disk, because that part really was sampled.

       Saturating rather than wrapping: the four states the grid shows only care
       whether errors is none, some or all of passes, and a count that rolled over
       at 256 would turn a cylinder that has failed every read into one that looks
       perfect.
    */
    if (_tainted)
        return;

    const uint16_t track = (_cylinder * 2) + side;
    if (track < MAX_TRACKS)
    {
        TrackTally &t = _tally[track];
        if (t.passes < 255)
            t.passes++;
        if (result.valid != result.sectorCount && t.errors < 255)
            t.errors++;
        paintTallyCell(_cylinder, side);
    }
}

void XCopyDriveToolkit::publish()
{
    drawResults();
    drawStatusLine();
    drawPassLine();

    /*
       The console panel is repainted every pass rather than only when something
       changed. It is a fixed table updated in place, so a repaint costs a few
       cursor moves and scrolls nothing - and the pass counter, the speed and the
       spinner move every pass anyway, which is what says the drive is still being
       read while the operator waits for a number to change.
    */
    paintSettings();
    paintHead(0);
    paintHead(1);
    paintStatus();

    /*
       Every pass, unconditionally. This used to go out only when the glyphs had
       moved, on the reasoning that an unchanged reading is nothing to say - but the
       accumulated counts ride out with the glyphs, and those move on every single
       pass. On a disk that reads the same way twice running, which is what a well
       adjusted drive looks like, the browser's tally froze at whatever it held when
       the reading last changed, and sat there reading "1/0" for the rest of the
       session while the TFT and the console counted up beside it.
    */
    if (_esp != nullptr)
    {
        char glyphs[24];
        char message[96];

        // Fixed buffers rather than concatenated Strings, for the same reason
        // drawFlux() uses them: this goes out a few times a second, all session.
        for (uint8_t side = 0; side < 2; side++)
        {
            // The accumulated counts ride along with the realtime row rather than
            // going out as a message of their own: the browser needs both to redraw
            // one cylinder, and they are always produced together.
            const uint16_t track = (_cylinder * 2) + side;
            const TrackTally &t = _tally[track < MAX_TRACKS ? track : 0];

            bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
            if (!wanted || !_resultValid[side])
            {
                // Carries the tally even so. Sending zeroes here told the browser
                // the cylinder had never been tested, so selecting one head wiped
                // from the grid the very history it exists to keep for the other.
                snprintf(message, sizeof(message), "broadcast dtRead,%d,%d,0,0,-,%d,%d,0\r\n",
                         (int)_cylinder, side, t.passes, t.errors);
                _esp->print(message);
                continue;
            }

            const CalibrationResult &r = _results[side];
            uint8_t cap = (uint8_t)(sizeof(glyphs) - 1);
            uint8_t n = r.sectorCount > cap ? cap : r.sectorCount;
            for (uint8_t i = 0; i < n; i++)
                glyphs[i] = glyph(r.status[i]);
            glyphs[n] = 0;

            snprintf(message, sizeof(message), "broadcast dtRead,%d,%d,%d,%d,%s,%d,%d,%d\r\n",
                     (int)_cylinder, side, r.valid, r.sectorCount, glyphs,
                     t.passes, t.errors, _resultTainted[side] ? 1 : 0);
            _esp->print(message);
        }
    }
    sendCalConfig();

    // Last, so the redraws above are finished with the SPI bus before a sample
    // starts streaming off the flash on it.
    playFeedback();
}

/*
   One short sample a pass, describing the pass.

   Blocking, and knowingly so: playFile() forces its wait flag and returns only
   when the sample has finished, because the samples are read from SerialFlash on
   the same SPI bus the TFT sits on. It is called at the end of publish() for that
   reason - every redraw this pass is already done, so nothing is competing for the
   bus, and the capture for the next pass has not started.

   Three outcomes rather than two, because "reading the wrong cylinder entirely"
   and "reading this one badly" want different actions from whoever is holding the
   screwdriver. A driven pass says nothing about the head, so it says nothing at
   all rather than bonging at an operator who caused it deliberately.
*/
void XCopyDriveToolkit::playFeedback()
{
    if (!_sound || _audio == nullptr)
        return;

    if (!_diskPresent || _tainted)
        return;

    bool anyRead = false;
    bool allClean = true;
    bool wrongCylinder = false;

    for (uint8_t side = 0; side < 2; side++)
    {
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
        if (!wanted)
            continue;
        if (!_resultValid[side])
        {
            allClean = false;
            continue;
        }
        anyRead = true;
        if (_results[side].valid != _results[side].sectorCount)
            allClean = false;
        if (_results[side].cylinderSeen >= 0 && _results[side].cylinderSeen != (int8_t)_cylinder)
            wrongCylinder = true;
    }

    if (wrongCylinder)
        _audio->playBoing(false);
    else if (anyRead && allClean)
        _audio->playClick(false);
    else
        _audio->playBong(false);
}

// STATUS

/*
   The one status sentence, written once and rendered three times.

   The TFT, the console panel and the browser are three views of the same drive;
   letting each compose its own wording is how they end up disagreeing about a
   drive somebody is in the middle of adjusting.

   The interface has the first word. A drive that is not spinning has nothing to
   say about head alignment, and reporting a failed pass over a released interface
   as a calibration result would invent exactly the diagnostic the operator is
   about to act on.
*/
const char *XCopyDriveToolkit::statusText(char *out, size_t length, uint16_t &tftColour) const
{
    if (!_mot)
    {
        tftColour = DT_GREY;
        snprintf(out, length, _sel ? "Selected, motor off" : "Outputs released");
        return DT_DIM;
    }

    /*
       The whole reason the lower layer exists, so it says the next thing to try
       rather than just naming the symptom. Stepping works with the spindle
       stopped, so "motor on and no index" almost always means the drive is not
       taking its motor command from where we are giving it.
    */
    if (_rpm <= 0.0f && _idxEdges == 0)
    {
        tftColour = ST7735_RED;
        snprintf(out, length, "MOTOR ON, no index");
        return DT_RED;
    }

    if (_rpm <= 0.0f)
    {
        tftColour = ST7735_YELLOW;
        snprintf(out, length, "%lu edges, no rate yet", (unsigned long)_idxEdges);
        return DT_YELLOW;
    }

    if (_read == Read::off)
    {
        char speed[16];
        formatRpm(_rpm, speed, sizeof(speed));
        tftColour = ST7735_GREEN;
        snprintf(out, length, "Spinning at %s", speed);
        return DT_GREEN;
    }

    if (_read == Read::hold)
    {
        snprintf(out, length, "Paused - nothing is being read");
        tftColour = ST7735_CYAN;
        return DT_CYAN;
    }

    if (!_diskPresent)
    {
        snprintf(out, length, "No disk in drive");
        tftColour = ST7735_RED;
        return DT_RED;
    }

    // The pass just taken was not a measurement, and saying so is the whole point
    // of letting the lines stay live during a read.
    if (_tainted)
    {
        snprintf(out, length, "Lines driven - pass not counted");
        tftColour = ST7735_MAGENTA;
        return DT_MAGENTA;
    }

    // The most useful single line of the lot: the head is somewhere else entirely,
    // and this says where.
    int8_t seen = -1;
    for (uint8_t side = 0; side < 2; side++)
        if (_resultValid[side] && _results[side].cylinderSeen >= 0)
            seen = _results[side].cylinderSeen;

    bool allGood = true;
    bool any = false;
    for (uint8_t side = 0; side < 2; side++)
    {
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
        if (!wanted)
            continue;
        any = true;
        if (!_resultValid[side] || _results[side].valid != _results[side].sectorCount)
            allGood = false;
    }

    if (seen >= 0 && seen != (int8_t)_cylinder)
    {
        snprintf(out, length, "Cyl %d seen as %d", (int)_cylinder, seen);
        tftColour = ST7735_YELLOW;
        return DT_YELLOW;
    }

    if (any && allGood)
    {
        snprintf(out, length, "Aligned, all sectors okay");
        tftColour = ST7735_GREEN;
        return DT_GREEN;
    }

    snprintf(out, length, "Adjust for all sectors okay");
    tftColour = ST7735_WHITE;
    return DT_WHITE;
}

// TFT

void XCopyDriveToolkit::drawSignals()
{
    if (_graphics == nullptr || !_drawnStatic)
        return;

    for (uint8_t i = 0; i < sigCount; i++)
    {
        uint16_t colour = DT_GREY;
        switch (_level[i])
        {
        case Level::asserted:
            colour = ST7735_YELLOW;
            break;
        case Level::good:
            colour = ST7735_GREEN;
            break;
        case Level::fault:
            colour = ST7735_RED;
            break;
        default:
            break;
        }

        const bool output = i < kOutputCount;
        const uint8_t slot = output ? i : (i - kOutputCount);
        _graphics->getTFT()->fillRect(BOX_X[slot], output ? ROW_OUT_BOX : ROW_IN_BOX,
                                      BOX_W, BOX_H, colour);
    }
}

const char *XCopyDriveToolkit::headName() const
{
    switch (_head)
    {
    case HeadSel::lower:
        return "Low";
    case HeadSel::upper:
        return "Upp";
    default:
        return "Both";
    }
}

const char *XCopyDriveToolkit::readName() const
{
    switch (_read)
    {
    case Read::run:
        return "On";
    case Read::hold:
        return "Hold";
    default:
        return "Off";
    }
}

void XCopyDriveToolkit::drawSettings()
{
    if (_graphics == nullptr)
        return;

    char value[8];

    for (uint8_t i = 0; i < (uint8_t)Field::count; i++)
    {
        const FieldSlot &slot = FIELD_SLOT[i];
        bool focused = ((uint8_t)_field == i);

        // The focus marker is the leading character of the label, so moving the
        // cursor never reflows anything.
        char label[8];
        snprintf(label, sizeof(label), "%c%s", focused ? '>' : ' ', slot.label);
        _graphics->getTFT()->fillRect(slot.labelX, slot.y, 32, 10, ST7735_BLACK);
        _graphics->drawText(slot.labelX, slot.y, focused ? ST7735_GREEN : ST7735_WHITE, label);

        switch ((Field)i)
        {
        case Field::cylinder:
            if (_headKnown)
                snprintf(value, sizeof(value), "%d", (int)_cylinder);
            else
                snprintf(value, sizeof(value), "%d?", (int)_cylinder);
            break;
        case Field::step:
            snprintf(value, sizeof(value), "%d", _stepSize);
            break;
        case Field::head:
            snprintf(value, sizeof(value), "%s", headName());
            break;
        case Field::autoReseek:
            snprintf(value, sizeof(value), "%s", _autoReseek ? "On" : "Off");
            break;
        case Field::sound:
            snprintf(value, sizeof(value), "%s", _sound ? "On" : "Off");
            break;
        default:
            snprintf(value, sizeof(value), "%s", readName());
            break;
        }

        _graphics->getTFT()->fillRect(slot.valueX, slot.y, slot.valueW, 10, ST7735_BLACK);
        _graphics->drawText(slot.valueX, slot.y, ST7735_YELLOW, value);
    }
}

// RESULTS

char XCopyDriveToolkit::glyph(uint8_t verdict)
{
    switch (verdict)
    {
    case sectorOK:
        return '.';
    case sectorCylLow:
        return '-';
    case sectorCylHigh:
        return '+';
    case sectorHeadWrong:
        return 'h';
    case sectorBadCheck:
        return 'c';
    default:
        return 'X';
    }
}

static uint16_t glyphColour(uint8_t verdict)
{
    switch (verdict)
    {
    case sectorOK:
        return ST7735_GREEN;
    case sectorCylLow:
    case sectorCylHigh:
        return ST7735_YELLOW;
    case sectorHeadWrong:
        return ST7735_MAGENTA;
    case sectorBadCheck:
        return ST7735_WHITE;
    default:
        return ST7735_RED;
    }
}

//! The console half of the pair above. Kept immediately beside it so the panel and
//! the screen cannot end up colouring the same verdict differently.
static const char *glyphConsoleColour(uint8_t verdict)
{
    switch (verdict)
    {
    case sectorOK:
        return DT_GREEN;
    case sectorCylLow:
    case sectorCylHigh:
        return DT_YELLOW;
    case sectorHeadWrong:
        return DT_MAGENTA;
    case sectorBadCheck:
        return DT_WHITE;
    default:
        return DT_RED;
    }
}

void XCopyDriveToolkit::drawResults()
{
    if (_graphics == nullptr || !_drawnStatic)
        return;

    for (uint8_t side = 0; side < 2; side++)
    {
        uint8_t y = side == 0 ? ROW_HEAD0 : ROW_HEAD1;
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);

        _graphics->getTFT()->fillRect(0, y, 160, 10, ST7735_BLACK);

        char label[4];
        snprintf(label, sizeof(label), "%d:", side);
        // A driven pass is marked on the row rather than in the glyphs: the
        // sectors really were read, it is the pass that is not a measurement.
        _graphics->drawText(0, y, _resultTainted[side] ? ST7735_MAGENTA : ST7735_WHITE, label);

        if (_read == Read::off)
        {
            _graphics->drawText(GLYPH_X, y, ST7735_BLUE, "read off");
            continue;
        }

        if (!wanted)
        {
            _graphics->drawText(GLYPH_X, y, ST7735_BLUE, "not selected");
            continue;
        }

        if (!_resultValid[side])
        {
            _graphics->drawText(GLYPH_X, y, ST7735_RED, _diskPresent ? "no read" : "no disk");
            continue;
        }

        const CalibrationResult &r = _results[side];
        /*
           Eleven glyphs fit the panel on a 9px pitch. HD twenty two do not, and an
           HD Amiga disk is rare enough that squeezing the layout for it would cost
           the common case its legibility - so the row is capped and the count
           beside it still reports the whole track.
        */
        uint8_t shown = r.sectorCount > 11 ? 11 : r.sectorCount;
        for (uint8_t i = 0; i < shown; i++)
        {
            char g[2] = {glyph(r.status[i]), 0};
            _graphics->drawText(GLYPH_X + (i * GLYPH_PITCH), y, glyphColour(r.status[i]), g);
        }

        char count[12];
        snprintf(count, sizeof(count), "%d/%d", r.valid, r.sectorCount);
        _graphics->drawText(COUNT_X, y, r.valid == r.sectorCount ? ST7735_GREEN : ST7735_YELLOW, count);
    }
}

/*
   The pass counter, the spinner and the speed.

   Split out of publish() because the two do not always go together: a paused
   session takes no passes but the disk is still turning, and a speed readout that
   froze with the counter would be reporting a number that is no longer true.
*/
void XCopyDriveToolkit::drawPassLine()
{
    if (_graphics == nullptr || !_drawnStatic)
        return;

    char line[32];
    if (_read == Read::off)
        snprintf(line, sizeof(line), "IDX %lu", (unsigned long)_idxEdges);
    else if (_taintedPasses > 0)
        snprintf(line, sizeof(line), "%c %lu (%u drv)", SPINNER[_spinner],
                 (unsigned long)_passes, (unsigned)_taintedPasses);
    else
        snprintf(line, sizeof(line), "%c pass %lu", SPINNER[_spinner], (unsigned long)_passes);
    _graphics->getTFT()->fillRect(0, ROW_PASS, 88, 10, ST7735_BLACK);
    _graphics->drawText(0, ROW_PASS, ST7735_WHITE, line);

    formatRpm(_rpm, line, sizeof(line));
    _graphics->getTFT()->fillRect(90, ROW_PASS, 70, 10, ST7735_BLACK);
    _graphics->drawText(90, ROW_PASS, ST7735_WHITE, line);
}

void XCopyDriveToolkit::drawStatusLine()
{
    if (_graphics == nullptr || !_drawnStatic)
        return;

    char text[40];
    uint16_t colour = ST7735_WHITE;
    statusText(text, sizeof(text), colour);
    _graphics->drawText(0, ROW_STATUS, colour, text, true);
}

void XCopyDriveToolkit::drawStatic()
{
    if (_graphics == nullptr)
        return;

    _graphics->clearScreen();

    for (uint8_t i = 0; i < sigCount; i++)
    {
        const bool output = i < kOutputCount;
        const uint8_t slot = output ? i : (i - kOutputCount);
        _graphics->drawText(BOX_X[slot], output ? ROW_OUT_TEXT : ROW_IN_TEXT,
                            ST7735_WHITE, SIG_LABEL[i]);
    }

    // One help line where the calibration screen had two and a title. The eleven
    // labelled boxes name the screen better than a caption did.
    _graphics->drawText(0, ROW_HELP, ST7735_BLUE, "PUSH/UPDN R:seek L:exit");

    _drawnStatic = true;
    drawSignals();
    drawSettings();
    drawResults();
    drawPassLine();
    drawStatusLine();
}

// LINE CONTROLS

/*
   Every line control routes through here.

   Two jobs. It marks the pass in flight as driven, which is what keeps a tainted
   pass out of the tally - see _tainted. And it forces the next pass through
   seek0(), because a raw step pulse moves the head under the read and a pass that
   trusted _currentTrack after one would be reading somewhere else while saying
   confidently which cylinder it was on.
*/
void XCopyDriveToolkit::lineChanged()
{
    if (_read == Read::run)
        _tainted = true;
    _recalPending = true;
}

void XCopyDriveToolkit::setSelect(bool on)
{
    _sel = on;
    _floppy->setSelectLine(on);
    lineChanged();
}

void XCopyDriveToolkit::setMotor(bool on)
{
    _mot = on;
    /*
       Clearing the edge count here is what makes the motor probe a measurement
       rather than a guess: after this, any index edge at all arrived because the
       motor was asked to run, and a count still at zero a second later is the
       answer rather than a leftover.
    */
    if (on)
        _floppy->clearIndexEdges();
    _floppy->setMotorLine(on);
    // The read layer waits this out on its own clock rather than letting
    // motorOn() block the main loop for it - see Phase::spinup.
    if (!on && _read == Read::run)
        _phase = Phase::spinup;
    lineChanged();
}

void XCopyDriveToolkit::setDirection(bool inward)
{
    _inward = inward;
    _floppy->setDirFast(inward ? 1 : 0);
    lineChanged();
}

void XCopyDriveToolkit::setSideOne(bool one)
{
    _sideOne = one;
    _floppy->setSideFast(one ? 1 : 0);
    lineChanged();
}

void XCopyDriveToolkit::setDensity(bool high)
{
    _high = high;
    _floppy->setDensityLine(high);
    lineChanged();
}

void XCopyDriveToolkit::setSticky(bool on)
{
    _sticky = on;
    if (!on)
        memset(_moved, 0, sizeof(_moved));
}

void XCopyDriveToolkit::pulseStep()
{
    _floppy->stepPulse();
    _stepPulses++;

    if (_headKnown)
    {
        int next = (int)_cylinder + (_inward ? 1 : -1);
        if (next < 0)
            next = 0;
        if (next > (int)kMaxCylinder)
            next = kMaxCylinder;
        _cylinder = (uint8_t)next;
    }
    lineChanged();
}

void XCopyDriveToolkit::nudgeCylinder(int delta)
{
    if (delta == 0)
        return;
    seekCylinder((int)_cylinder + delta);
}

void XCopyDriveToolkit::seekCylinder(int cylinder)
{
    if (cylinder < 0)
        cylinder = 0;
    if (cylinder > (int)kMaxCylinder)
        cylinder = kMaxCylinder;

    // Nothing sensible to seek relative to until the head has been found.
    if (!_headKnown)
    {
        recalibrate();
        if (!_headKnown)
            return;
    }

    const int previous = _markedCylinder;
    _floppy->gotoLogicTrack((cylinder * 2) + (_sideOne ? 1 : 0));
    _cylinder = (uint8_t)cylinder;

    // The results on screen describe somewhere the head no longer is.
    _resultValid[0] = false;
    _resultValid[1] = false;

    // Only the two cells that gained or lost the marker are repainted.
    if (previous >= 0)
        paintTallyCylinder(previous);
    paintTallyCylinder(_cylinder);
    _markedCylinder = _cylinder;

    lineChanged();
    settingsChanged();
}

void XCopyDriveToolkit::recalibrate()
{
    _floppy->seek0();
    // seek0() steps outward until TRACK 0 asserts. If the line never comes up the
    // head is not where the drive says it is, and claiming cylinder 0 would be a
    // guess dressed as a measurement.
    _headKnown = _floppy->readTrack0Line();
    if (_headKnown)
        _cylinder = 0;
    lineChanged();
    settingsChanged();
}

void XCopyDriveToolkit::clearCounters()
{
    _floppy->clearIndexEdges();
    _stepPulses = 0;
    memset(_moved, 0, sizeof(_moved));
}

void XCopyDriveToolkit::clearTally()
{
    memset(_tally, 0, sizeof(_tally));
    _passes = 0;
    _taintedPasses = 0;
    paintTallyGrid();
    settingsChanged();
}

void XCopyDriveToolkit::releaseOutputs()
{
    _floppy->setMotorLine(false);
    _floppy->setSelectLine(false);
    _floppy->setDensityLine(true);
    _floppy->setDirFast(0);

    _mot = false;
    _sel = false;
    _high = true;
    _inward = false;
    _headKnown = false;

    // A pass over a released interface is not a measurement of anything, and the
    // next one would put it all straight back 120ms later anyway.
    if (_read != Read::off)
        setReading(false);
    lineChanged();
}

// READ LAYER CONTROLS

void XCopyDriveToolkit::setReading(bool on)
{
    if (on == (_read != Read::off))
        return;

    if (on)
    {
        /*
           A capture needs select, motor and a known cylinder together. The session
           deliberately has none of them, so this is where they are asserted - and
           the head is found first, because a pass that did not know which cylinder
           it was on could not report the one thing this test exists to say.
        */
        if (!_headKnown)
            recalibrate();
        _floppy->setSelectLine(true);
        _sel = true;
        setMotor(true);
        _read = Read::run;
        _phase = Phase::spinup;
        _recalPending = true;
        _tainted = false;
        _nextStepMs = millis();
    }
    else
    {
        _read = Read::off;
        _resultValid[0] = false;
        _resultValid[1] = false;
    }

    settingsChanged();
}

void XCopyDriveToolkit::setHead(HeadSel head)
{
    if (head == _head)
        return;
    _head = head;
    _recalPending = true;
    settingsChanged();
}

void XCopyDriveToolkit::cycleHead()
{
    switch (_head)
    {
    case HeadSel::both:
        setHead(HeadSel::lower);
        break;
    case HeadSel::lower:
        setHead(HeadSel::upper);
        break;
    default:
        setHead(HeadSel::both);
        break;
    }
}

void XCopyDriveToolkit::setAutoReseek(bool on)
{
    if (on == _autoReseek)
        return;
    _autoReseek = on;
    settingsChanged();
}

void XCopyDriveToolkit::setSound(bool on)
{
    if (on == _sound)
        return;
    _sound = on;
    settingsChanged();
}

void XCopyDriveToolkit::setPaused(bool on)
{
    if (_read == Read::off)
        return;
    if (on == (_read == Read::hold))
        return;

    /*
       Resuming re-seeks and throws away the rows on screen. The head has had as
       long as the pause lasted to be moved by hand - that is usually why it was
       paused - so the first pass after it has to go and find the cylinder again
       rather than assume the last read left the head where it thinks.
    */
    if (on)
    {
        _read = Read::hold;
    }
    else
    {
        _read = Read::run;
        _recalPending = true;
        _resultValid[0] = false;
        _resultValid[1] = false;
        _phase = _floppy->getMotorStatus() ? Phase::settle : Phase::spinup;
        _nextStepMs = millis();
    }

    settingsChanged();
}

void XCopyDriveToolkit::togglePause() { setPaused(_read == Read::run); }

void XCopyDriveToolkit::setStepSize(uint8_t size)
{
    // 1, 10 and 40 are the three ATK offers that matter on a stroke this short.
    if (size != 1 && size != 10 && size != 40)
        return;
    if (size == _stepSize)
        return;
    _stepSize = size;
    settingsChanged();
}

void XCopyDriveToolkit::cycleStepSize(int direction)
{
    if (direction > 0)
        setStepSize(_stepSize == 1 ? 10 : (_stepSize == 10 ? 40 : 1));
    else
        setStepSize(_stepSize == 40 ? 10 : (_stepSize == 10 ? 1 : 40));
}

void XCopyDriveToolkit::reseek()
{
    _recalPending = true;
    _resultValid[0] = false;
    _resultValid[1] = false;
    drawResults();
    paintHead(0);
    paintHead(1);
    paintStatus();
}

void XCopyDriveToolkit::settingsChanged()
{
    drawSettings();
    drawResults();
    drawStatusLine();
    drawPassLine();
    paintSettings();
    paintHead(0);
    paintHead(1);
    paintStatus();
    sendCalConfig();
    sendState();
}

void XCopyDriveToolkit::nextField()
{
    _field = (Field)(((uint8_t)_field + 1) % (uint8_t)Field::count);
    drawSettings();
}

void XCopyDriveToolkit::adjustField(int direction)
{
    switch (_field)
    {
    case Field::cylinder:
        nudgeCylinder(direction > 0 ? _stepSize : -_stepSize);
        break;
    case Field::step:
        cycleStepSize(direction);
        break;
    case Field::head:
        cycleHead();
        break;
    case Field::autoReseek:
        toggleAutoReseek();
        break;
    case Field::sound:
        toggleSound();
        break;
    default:
        // Off -> On -> Hold -> Off, in the direction asked for. The read being opt
        // in makes "paused" and "not reading" the same question on this screen, so
        // one three state cell answers it rather than two of the six the TFT has.
        if (direction > 0)
        {
            if (_read == Read::off)
                setReading(true);
            else if (_read == Read::run)
                setPaused(true);
            else
                setReading(false);
        }
        else
        {
            if (_read == Read::off)
                setReading(true), setPaused(true);
            else if (_read == Read::hold)
                setPaused(false);
            else
                setReading(false);
        }
        break;
    }
}

/*
   The merged key map.

   Three of the calibration test's letters moved, and one rule decided all three:
   the line mnemonics are the pins, so s m d h n stay with the lines. Re-seek keeps
   r - it is ATK's F1, it is joystick RIGHT, the browser calls it Re-seek and it is
   pressed constantly - so release outputs, which is rare and deliberate, is what
   yields the letter and becomes x. Sound moves to z and the read's head selection
   to e, because s is SELECT and h is the SIDE line.
*/
bool XCopyDriveToolkit::handleKey(char key)
{
    switch (key)
    {
    // lines
    case 's':
        toggleSelect();
        break;
    case 'm':
        toggleMotor();
        break;
    case 'd':
        toggleDirection();
        break;
    case 'h':
        toggleSide();
        break;
    case 'n':
        toggleDensity();
        break;
    case ' ':
        pulseStep();
        break;
    case '0':
        recalibrate();
        break;

    // head position
    case '+':
    case '=':
        nudgeCylinder(1);
        break;
    case '-':
    case '_':
        nudgeCylinder(-1);
        break;
    case ']':
        nudgeCylinder(10);
        break;
    case '[':
        nudgeCylinder(-10);
        break;
    case '}':
        nudgeCylinder(40);
        break;
    case '{':
        nudgeCylinder(-40);
        break;
    case 'r':
        reseek();
        break;

    // the read
    case 'g':
        toggleReading();
        break;
    case 'p':
        togglePause();
        break;
    case 'e':
        cycleHead();
        break;
    case 'a':
        toggleAutoReseek();
        break;
    case 'z':
        toggleSound();
        break;
    case '1':
        cycleStepSize(1);
        break;

    // session
    case 'c':
        clearCounters();
        break;
    case 't':
        clearTally();
        break;
    case 'k':
        toggleSticky();
        break;
    case 'x':
        releaseOutputs();
        break;

    case 'q':
    case 'Q':
    case 0x1B: // Esc
    case 0x03: // Ctrl-C
        return false;
    default:
        return true;
    }

    // Repaint from a fresh sample, so a key press looks immediate rather than
    // waiting up to kSampleMs for the next refresh.
    sample();
    drawSignals();
    drawPassLine();
    drawStatusLine();
    paintSettings();
    paintSignals();
    paintStatus();
    sendState();
    return true;
}

// CONSOLE PANEL
//
// Everything below writes straight to Serial, never through Log. Log mirrors each
// write to the browser and sleeps 6ms doing it, and copies the whole String on the
// way; a panel repainted several times a second for as long as somebody is
// adjusting a drive would be a permanent tax on both the loop and the few KB of
// heap left. The browser gets structured websocket messages instead, so nothing is
// lost - and it would only see the escape sequences anyway.

void XCopyDriveToolkit::repeat(char character, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
        Serial.write(character);
}

void XCopyDriveToolkit::solidRow(char left, char right)
{
    Serial.write(left);
    repeat('-', INNER);
    Serial.write(right);
    Serial.print("\r\n");
}

void XCopyDriveToolkit::joinRow()
{
    Serial.write('|');
    repeat('-', LABELWIDTH);
    Serial.write('+');
    repeat('-', SIDEWIDTH);
    Serial.write('+');
    repeat('-', SIDEWIDTH);
    Serial.write('|');
    Serial.print("\r\n");
}

void XCopyDriveToolkit::textRow(const char *text)
{
    char buffer[TEXT + 1];
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print("| ");
    Serial.print(buffer);
    Serial.print(" |\r\n");
}

char XCopyDriveToolkit::columnLabel(uint8_t col)
{
    return (col < 10) ? (char)('0' + col) : (char)('A' + (col - 10));
}

/**
 * @brief Park the cursor on a panel cell.
 *
 * Moves are relative (cursor up) so they survive the terminal scrolling when the
 * panel is printed at the bottom of the window; the column is absolute.
 *
 * moveTo() saves the cursor as its first act, so a second one before the restore
 * overwrites the saved position with a spot inside the panel - and everything
 * printed afterwards, the prompt included, lands in the middle of it. Always pair
 * them.
 *
 * @param line line number within the panel, 0 being the top border
 * @param column 1 based terminal column
 */
void XCopyDriveToolkit::moveTo(uint8_t line, uint8_t column)
{
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "\033[s\033[%uA\033[%uG", (unsigned)(LINES - line), (unsigned)column);
    Serial.print(buffer);
}

void XCopyDriveToolkit::restore()
{
    Serial.print("\033[u");
}

//! One full width field, moved to and restored from as a pair.
void XCopyDriveToolkit::paintTextRow(uint8_t line, const char *colour, const char *text)
{
    moveTo(line, 3);
    Serial.print(colour);
    Serial.printf("%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print(DT_RESET);
    restore();
}

void XCopyDriveToolkit::paintSettings()
{
    if (!_panelActive)
        return;

    char buffer[TEXT + 1];
    char cylText[12];
    char speed[16];

    if (_headKnown)
        snprintf(cylText, sizeof(cylText), "%d", (int)_cylinder);
    else
        snprintf(cylText, sizeof(cylText), "%d (?)", (int)_cylinder);
    formatRpm(_rpm, speed, sizeof(speed));

    snprintf(buffer, sizeof(buffer), "CYLINDER : %-8s STEP : %d", cylText, _stepSize);
    paintTextRow(LINE_SETTINGS, DT_WHITE, buffer);

    snprintf(buffer, sizeof(buffer), "HEAD(S)  : %-8s AUTO : %-4s SOUND : %s",
             headName(), _autoReseek ? "On" : "Off", _sound ? "On" : "Off");
    paintTextRow(LINE_SETTINGS + 1, DT_WHITE, buffer);

    if (_taintedPasses > 0)
        snprintf(buffer, sizeof(buffer), "READ     : %-8s PASS : %lu (%u driven)",
                 readName(), (unsigned long)_passes, (unsigned)_taintedPasses);
    else
        snprintf(buffer, sizeof(buffer), "READ     : %-8s PASS : %-6lu SPEED : %s",
                 readName(), (unsigned long)_passes, speed);
    paintTextRow(LINE_SETTINGS + 2, DT_WHITE, buffer);
}

/*
   The eleven lines in three rows rather than eleven.

   The web renders them as a Signal / Pin / Dir / State / Count table, which is
   right there and wrong here: stacked whole under the calibration panel it came to
   forty five rows, taller than the terminal most of this is read in. Colour
   already carries each line's verdict, so two rows of names coloured exactly as
   the table would have coloured them say the same thing in two lines, and the
   numbers colour cannot carry go on a third.

   Six then five, the same split the TFT's two banks and the web table's rule make,
   because "what am I driving" and "what is the drive saying" are the two halves of
   every question asked here.
*/
void XCopyDriveToolkit::paintSignals()
{
    if (!_panelActive)
        return;

    for (uint8_t half = 0; half < 2; half++)
    {
        const uint8_t first = half == 0 ? 0 : kOutputCount;
        const uint8_t last = half == 0 ? kOutputCount : sigCount;

        moveTo(half == 0 ? LINE_SIG_OUT : LINE_SIG_IN, 3);

        // The visible width is accumulated alongside the coloured output, because
        // the escapes are not printing characters and would otherwise throw the
        // padding - and with it the right hand frame - out.
        uint8_t visible = 0;
        Serial.print(DT_WHITE);
        Serial.print(half == 0 ? "OUT  " : "IN   ");
        visible += 5;

        for (uint8_t i = first; i < last; i++)
        {
            const char *colour = DT_DIM;
            switch (_level[i])
            {
            case Level::asserted:
                colour = DT_YELLOW;
                break;
            case Level::good:
                colour = DT_GREEN;
                break;
            case Level::fault:
                colour = DT_RED;
                break;
            default:
                break;
            }
            Serial.print(colour);
            Serial.printf("%-6.6s", SIG_LABEL[i]);
            visible += 6;
        }

        Serial.print(DT_RESET);
        if (visible < TEXT)
            repeat(' ', TEXT - visible);
        restore();
    }

    char buffer[TEXT + 1];
    char speed[16];
    char cylText[8];
    formatRpm(_rpm, speed, sizeof(speed));
    if (_headKnown)
        snprintf(cylText, sizeof(cylText), "%d", (int)_cylinder);
    else
        snprintf(cylText, sizeof(cylText), "?");
    snprintf(buffer, sizeof(buffer), "     %-11s %lu edges  %lu pulses  cyl %s",
             speed, (unsigned long)_idxEdges, (unsigned long)_stepPulses, cylText);
    paintTextRow(LINE_SIG_NUM, DT_DIM, buffer);
}

/*
   The realtime half: what the head is reading this revolution.

   Kept beside the accumulated grid rather than replaced by it, because the two
   answer different questions. This one says which sectors are being missed right
   now, and moves the instant the drive is touched; the grid below says whether a
   cylinder has ever been read cleanly, which is what tells a misaligned head from
   a marginal one.
*/
void XCopyDriveToolkit::paintHead(uint8_t side)
{
    if (!_panelActive || side > 1)
        return;

    const uint8_t line = LINE_HEAD + side;
    bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
    if (_read == Read::off)
        wanted = false;

    char prefix[HEADPREFIX + 8];
    snprintf(prefix, sizeof(prefix), "%d (%s)", side, side == 0 ? "Lower" : "Upper");
    moveTo(line, 3);
    // A driven pass is marked on the row rather than in the glyphs: the sectors
    // really were read, it is the pass that is not a measurement.
    Serial.print(_resultTainted[side] ? DT_MAGENTA : DT_WHITE);
    Serial.printf("%-*.*s", (int)HEADPREFIX, (int)HEADPREFIX, prefix);
    Serial.print(DT_RESET);
    restore();

    // Glyph field. Eleven sectors on a two column pitch is exactly GLYPHFIELD; an
    // HD track is capped there rather than reflowing the panel for a disk almost
    // nobody has, and the count beside it still reports the whole track.
    moveTo(line, 3 + HEADPREFIX);
    uint8_t visible = 0;
    if (!wanted || !_resultValid[side])
    {
        const char *text = _read == Read::off ? "read off"
                                              : (!wanted ? "not selected"
                                                         : (_diskPresent ? "no read" : "no disk"));
        Serial.print(wanted ? DT_RED : DT_DIM);
        Serial.print(text);
        Serial.print(DT_RESET);
        visible = strlen(text);
    }
    else
    {
        const CalibrationResult &r = _results[side];
        for (uint8_t i = 0; i < r.sectorCount && visible + 1 <= GLYPHFIELD; i++)
        {
            if (i > 0)
            {
                if (visible + 2 > GLYPHFIELD)
                    break;
                Serial.write(' ');
                visible++;
            }
            Serial.print(glyphConsoleColour(r.status[i]));
            Serial.write(glyph(r.status[i]));
            visible++;
        }
        Serial.print(DT_RESET);
    }
    if (visible < GLYPHFIELD)
        repeat(' ', GLYPHFIELD - visible);
    restore();

    moveTo(line, 3 + HEADPREFIX + GLYPHFIELD + 1);
    char result[RESULTFIELD + 1];
    if (!wanted || !_resultValid[side])
        snprintf(result, sizeof(result), "%s", "-");
    else if (_resultTainted[side])
        snprintf(result, sizeof(result), "%2d/%d driven", _results[side].valid,
                 _results[side].sectorCount);
    else
        // Right aligned, so the two heads stack and a count that drops by one is
        // seen as a digit changing rather than the whole field shifting.
        snprintf(result, sizeof(result), "%2d/%d okay", _results[side].valid,
                 _results[side].sectorCount);

    if (wanted && _resultValid[side] && _resultTainted[side])
        Serial.print(DT_MAGENTA);
    else if (wanted && _resultValid[side] && _results[side].valid == _results[side].sectorCount)
        Serial.print(DT_GREEN);
    else if (wanted && _resultValid[side])
        Serial.print(DT_YELLOW);
    else
        Serial.print(DT_DIM);
    Serial.printf("%-*.*s", (int)RESULTFIELD, (int)RESULTFIELD, result);
    Serial.print(DT_RESET);
    restore();
}

void XCopyDriveToolkit::paintStatus()
{
    if (!_panelActive)
        return;

    char text[TEXT + 1];
    uint16_t ignored = 0;
    const char *colour = statusText(text, sizeof(text), ignored);

    // The spinner rides on the status row, so a panel with nothing changing on it
    // still shows that the drive is being read.
    char line[TEXT + 1];
    snprintf(line, sizeof(line), "%c %s", _read == Read::run ? SPINNER[_spinner] : ' ', text);
    paintTextRow(LINE_STATUS, colour, line);
}

// ACCUMULATED TALLY

XCopyDriveToolkit::TallyState XCopyDriveToolkit::tallyState(uint8_t cylinder, uint8_t side) const
{
    const uint16_t track = (cylinder * 2) + side;
    if (track >= MAX_TRACKS)
        return TallyState::untested;

    const TrackTally &t = _tally[track];
    if (t.passes == 0)
        return TallyState::untested;
    if (t.errors == 0)
        return TallyState::clean;
    if (t.errors >= t.passes)
        return TallyState::failing;
    return TallyState::intermittent;
}

static char tallyGlyph(uint8_t state)
{
    switch (state)
    {
    case 1:
        return '#'; // clean
    case 2:
        return '~'; // intermittent
    case 3:
        return 'X'; // failing
    default:
        return '.'; // untested
    }
}

static const char *tallyColour(uint8_t state)
{
    switch (state)
    {
    case 1:
        return DT_GREEN;
    case 2:
        return DT_YELLOW;
    case 3:
        return DT_RED;
    default:
        return DT_DIM;
    }
}

void XCopyDriveToolkit::paintTallyCell(uint8_t cylinder, uint8_t side)
{
    if (!_panelActive || cylinder >= MAX_CYLINDERS || side > 1)
        return;

    // Column 1 is the left border, 2 to LABELWIDTH+1 the row label and LABELWIDTH+2
    // its separator, so the first cell sits one past the pad space that follows it.
    // Identical arithmetic to XCopyTrackMap::paintCell, because it is the same grid.
    const uint8_t column = (side == 0 ? LABELWIDTH + 4 : LABELWIDTH + SIDEWIDTH + 5) +
                           ((cylinder % COLS) * 2);

    moveTo(LINE_TALLY + (cylinder / COLS), column);
    const uint8_t state = (uint8_t)tallyState(cylinder, side);
    // Reverse video marks where the head actually is, so the cylinder being
    // adjusted can be picked out of the grid without counting columns.
    if (_headKnown && cylinder == _cylinder)
        Serial.print("\033[7m");
    Serial.print(tallyColour(state));
    Serial.write(tallyGlyph(state));
    Serial.print(DT_RESET);
    restore();
}

void XCopyDriveToolkit::paintTallyCylinder(int cylinder)
{
    if (cylinder < 0 || cylinder >= (int)MAX_CYLINDERS)
        return;
    paintTallyCell((uint8_t)cylinder, 0);
    paintTallyCell((uint8_t)cylinder, 1);
}

void XCopyDriveToolkit::paintTallyGrid()
{
    if (!_panelActive)
        return;
    for (uint8_t cylinder = 0; cylinder < MAX_CYLINDERS; cylinder++)
        paintTallyCylinder(cylinder);
}

void XCopyDriveToolkit::panelBegin()
{
    char buffer[TEXT + 1];

    _panelActive = true;

    Serial.print("\r\n");

    solidRow('.', '.');
    textRow("DRIVE TOOLKIT");
    textRow("Every line sampled. The read wants a known good AmigaDOS disk.");
    solidRow('|', '|');

    // Placeholders. Every one of these is overwritten in place by the painters
    // before the first refresh finishes.
    textRow("");
    textRow("");
    textRow("");
    solidRow('|', '|');
    textRow("");
    textRow("");
    textRow("");
    solidRow('|', '|');
    textRow("");
    textRow("");
    textRow("");
    solidRow('|', '|');

    // Sector legend, then the keys. The two things an operator needs while their
    // hands are on the drive and their eyes are not on the manual.
    Serial.print("| ");
    static const uint8_t legend[] = {sectorOK, sectorMissing, sectorCylLow, sectorCylHigh,
                                     sectorHeadWrong, sectorBadCheck};
    // Short names on purpose: the six have to fit one row of TEXT, and the padding
    // below is unsigned, so a name long enough to overflow would wrap the
    // subtraction and paint most of a screen of spaces through the frame.
    static const char *const legendName[] = {"ok", "miss", "low", "high", "side", "chk"};
    uint8_t visible = 0;
    for (uint8_t i = 0; i < sizeof(legend) / sizeof(legend[0]); i++)
    {
        const uint8_t width = 4 + strlen(legendName[i]);
        if (visible + width > TEXT)
            break;
        Serial.print(glyphConsoleColour(legend[i]));
        Serial.write(glyph(legend[i]));
        Serial.print(DT_RESET);
        Serial.write(' ');
        Serial.print(legendName[i]);
        Serial.print("  ");
        visible += width;
    }
    repeat(' ', TEXT - visible);
    Serial.print(" |\r\n");

    /*
       Five rows, grouped by what they do to the drive: drive a line, move the
       head, run the read, manage the session. A single run of twenty odd letters
       reads as a wall with no shape, and the one thing an operator does constantly
       is step the head - so that gets a row of its own, spelled out.

       All are sized to TEXT - see textRow(), which truncates rather than wraps, so
       a row that outgrows the frame loses its tail silently.
    */
    textRow("LINES s sel   m mot   d dir   h side   n dens");
    textRow("HEAD  SPC step  -/+ one  [/] ten  {/} forty  0 recal");
    textRow("READ  g go/stop  p pause  r re-seek  1 step size");
    textRow("      e heads   a auto   z sound");
    textRow("KEYS  c clear  t tally  k sticky  x release  q quit");
    solidRow('|', '|');
    textRow("SESSION TALLY - all passes, every cylinder");
    joinRow();

    // Side headings, then the column labels. Generated rather than written out:
    // a stale hand written row is a silent lie about which cylinder a cell is.
    snprintf(buffer, sizeof(buffer), "%-*s", (int)LABELWIDTH, " TRK");
    Serial.write('|');
    Serial.print(buffer);
    for (uint8_t side = 0; side < 2; side++)
    {
        snprintf(buffer, sizeof(buffer), " SIDE %u (%s)", (unsigned)side, side == 0 ? "LOWER" : "UPPER");
        Serial.write('|');
        Serial.printf("%-*.*s", (int)SIDEWIDTH, (int)SIDEWIDTH, buffer);
    }
    Serial.print("|\r\n");

    Serial.write('|');
    repeat(' ', LABELWIDTH);
    for (uint8_t side = 0; side < 2; side++)
    {
        Serial.print("| ");
        for (uint8_t col = 0; col < COLS; col++)
        {
            if (col > 0)
                Serial.write(' ');
            Serial.write(columnLabel(col));
        }
        Serial.write(' ');
    }
    Serial.print("|\r\n");

    joinRow();

    for (uint8_t row = 0; row < ROWS; row++)
    {
        // The row label is the first cylinder in the row - 0, 10, 20 ... 80 - not
        // the row index, so a cylinder is read off as label plus column.
        snprintf(buffer, sizeof(buffer), "  %02u  ", (unsigned)(row * COLS));
        Serial.write('|');
        Serial.print(buffer);
        for (uint8_t side = 0; side < 2; side++)
        {
            Serial.print("| ");
            Serial.print(DT_DIM);
            for (uint8_t col = 0; col < COLS; col++)
            {
                if (col > 0)
                    Serial.write(' ');
                // The last row is short: MAX_CYLINDERS is not a multiple of COLS.
                // Blank rather than an untested glyph, or the grid would promise
                // cylinders 84 to 89 and then never fill them in.
                const uint8_t cylinder = (row * COLS) + col;
                Serial.write(cylinder < MAX_CYLINDERS ? tallyGlyph(0) : ' ');
            }
            Serial.print(DT_RESET);
            Serial.write(' ');
        }
        Serial.print("|\r\n");
    }

    joinRow();
    textRow(". untested  # all ok  ~ intermittent  X all fail");
    solidRow('`', '\'');

    paintSettings();
    paintSignals();
    paintHead(0);
    paintHead(1);
    paintStatus();
    if (_headKnown)
    {
        paintTallyCylinder(_cylinder);
        _markedCylinder = _cylinder;
    }
}

void XCopyDriveToolkit::panelEnd()
{
    if (!_panelActive)
        return;

    _panelActive = false;
    // The cursor has been parked below the bottom border all along, so a prompt
    // printed after this lands clear of the panel rather than inside it.
    Serial.print("\r\n");
}

// WEB

void XCopyDriveToolkit::sendState()
{
    if (_esp == nullptr)
        return;

    /*
       Both halves of every line go out: the level, which is the verdict the TFT
       and the console colour from, and the raw reading the verdict was made from.

       Sending the level alone would leave the browser deriving its own state text
       from nothing, and sending the text would put eleven strings on the wire
       several times a second. This way the browser has exactly the inputs
       stateText() has and mirrors that one mapping, the way the sector glyph
       vocabulary is already mirrored.

       Nine fields, deliberately: websocket.js splits every message at twelve, so
       one merged message carrying the read layer as well could not be read at the
       other end. The flags byte is what keeps it to nine - five booleans that used
       to be five fields.

       Speed goes out in tenths for the reason formatRpm() exists - printf() on
       this core has no float support linked in.
    */
    const int rpmTenths = _rpm > 0.0f ? (int)((_rpm * 10.0f) + 0.5f) : 0;

    char levels[sigCount + 1];
    char raws[sigCount + 1];
    for (uint8_t i = 0; i < sigCount; i++)
    {
        levels[i] = (char)('0' + (uint8_t)_level[i]);
        raws[i] = _raw[i] ? '1' : '0';
    }
    levels[sigCount] = '\0';
    raws[sigCount] = '\0';

    // 0 closed, 1 open and idle, 2 reading, 3 paused - exactly the four states the
    // browser's one Start/Pause/Resume button renders.
    uint8_t run = 1;
    if (_read == Read::run)
        run = 2;
    else if (_read == Read::hold)
        run = 3;

    uint8_t flags = 0;
    if (_sticky)
        flags |= 0x01;
    if (_tainted)
        flags |= 0x02;
    if (_sound)
        flags |= 0x04;
    if (_autoReseek)
        flags |= 0x08;
    if (_diskPresent)
        flags |= 0x10;

    char message[128];
    snprintf(message, sizeof(message),
             "broadcast dtState,%d,%d,%d,%lu,%lu,%d,%s,%s\r\n",
             _active ? run : 0, _headKnown ? (int)_cylinder : -1, rpmTenths,
             (unsigned long)_idxEdges, (unsigned long)_stepPulses,
             flags, levels, raws);
    _esp->print(message);
}

void XCopyDriveToolkit::sendCalConfig()
{
    if (_esp == nullptr)
        return;

    // What is left of the calibration config once the cylinder, the speed and the
    // session state have moved into dtState, which carries them anyway.
    char message[64];
    snprintf(message, sizeof(message), "broadcast dtCal,%d,%d,%d,%lu,%u\r\n",
             (int)_cylinder, _stepSize, (int)_head,
             (unsigned long)_passes, (unsigned)_taintedPasses);
    _esp->print(message);
}

void XCopyDriveToolkit::sendClosed()
{
    if (_esp == nullptr)
        return;
    _esp->print("broadcast dtState,0,-1,0,0,0,0,00000000000,00000000000\r\n");
}
