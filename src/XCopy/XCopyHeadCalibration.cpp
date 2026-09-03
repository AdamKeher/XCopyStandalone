#include "XCopyHeadCalibration.h"
#include "XCopyConsole.h"
#include "XCopyLog.h"

/*
   Screen layout, 160 x 128 landscape.

   The rows are tabulated rather than open coded into every draw call because the
   TFT is repainted in place: a value cell is cleared and rewritten on its own,
   and a y that disagreed between the clear and the write would leave debris.
*/
static const uint8_t ROW_SIGNAL_BOX = 0;
static const uint8_t ROW_SIGNAL_TEXT = 10;
static const uint8_t ROW_TITLE = 22;
static const uint8_t ROW_SETTINGS1 = 34;
static const uint8_t ROW_SETTINGS2 = 44;
static const uint8_t ROW_SETTINGS3 = 54;
static const uint8_t ROW_HEAD0 = 64;
static const uint8_t ROW_HEAD1 = 74;
static const uint8_t ROW_PASS = 86;
static const uint8_t ROW_HELP1 = 96;
static const uint8_t ROW_HELP2 = 106;
//! Last row that fits: the panel is 128 tall and a text row is 10.
static const uint8_t ROW_STATUS = 118;

//! Sector glyphs sit on a fixed pitch. The font is proportional, so a printed run
//! of them would neither line up under each other nor be repaintable one at a time.
static const uint8_t GLYPH_X = 14;
static const uint8_t GLYPH_PITCH = 9;
static const uint8_t COUNT_X = 122;

static const uint8_t SIGNAL_COUNT = 6;
static const uint8_t SIGNAL_X[SIGNAL_COUNT] = {0, 25, 50, 75, 100, 125};
static const uint8_t SIGNAL_W = 20;
static const uint8_t SIGNAL_H = 8;
static const char *const SIGNAL_LABEL[SIGNAL_COUNT] = {"SEL", "MOT", "IDX", "CHG", "WP", "T0"};

// Console colours, paired one to one with the TFT glyph colours below so the
// panel and the screen describe the same pass in the same language. Same set
// XCopyTrackMap uses, for the same reason.
#define HC_RESET   "\033[0m"
#define HC_GREY    "\033[0;90m"
#define HC_WHITE   "\033[1;37m"
#define HC_GREEN   "\033[0;32m"
#define HC_YELLOW  "\033[0;33m"
#define HC_RED     "\033[1;31m"
#define HC_MAGENTA "\033[0;35m"
#define HC_CYAN    "\033[0;36m"

static const char SPINNER[4] = {'\\', '|', '/', '-'};

/*
   The focusable settings, in cursor order. One table, so the focus cycle, the
   labels and the cells they are drawn in cannot drift apart from each other.
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
    {80, 116, ROW_SETTINGS3, 34, "Pse:"}, // Field::paused
};

void XCopyHeadCalibration::begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp,
                                 XCopyFloppy *floppy, uint8_t cylinder)
{
    _graphics = graphics;
    _audio = audio;
    _esp = esp;
    _floppy = floppy;

    _cylinder = cylinder > kMaxCylinder ? kMaxCylinder : cylinder;
    _stepSize = 1;
    _head = HeadSel::both;
    _autoReseek = false;
    _paused = false;
    _field = Field::cylinder;
    _phase = Phase::settle;
    _passes = 0;
    _spinner = 0;
    _recalPending = true;
    _resultValid[0] = false;
    _resultValid[1] = false;
    _drawnStatic = false;
    _nextStepMs = millis();

    // The tally is the session, so it starts empty every time the screen is opened.
    memset(_tally, 0, sizeof(_tally));
    _markedCylinder = 0xff;

    /*
       Put the density thresholds back where setMode() left them. An earlier read
       may have gone through adjustTimings(), and a calibration session is a
       comparison between consecutive passes - if the decoder moves underneath it,
       a change on screen no longer means the hand on the screwdriver caused it.
    */
    _floppy->setMode(_floppy->getMode());

    _floppy->motorOn();
    // The five second idle timeout would stop the drive mid session; nobody is
    // touching a control to keep it up while they have both hands on the drive.
    _floppy->setMotorIdleOff(false);
    /*
       beginRPM() owns the index interrupt and endRPM() gives it back, so the index
       indicator and the speed readout cost no interrupt of our own - and there is
       nothing left for an exit path to forget to detach. The feature this replaces
       attached three interrupts in begin(), detached none of them, and was then
       deleted with all three still armed.
    */
    _floppy->beginRPM();

    _diskPresent = _floppy->readDiskChangeLine();
    _active = true;
}

void XCopyHeadCalibration::end()
{
    if (!_active)
        return;

    _floppy->endRPM();
    _floppy->setMotorIdleOff(true);
    _floppy->motorOff();
    _active = false;
}

void XCopyHeadCalibration::update()
{
    if (!_active)
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

    /*
       Paused. No pass is taken, so the sector rows and the tally keep describing
       the last one - which is the point, a still picture to read or a quiet drive
       to work on - but the surfaces are not frozen with them. The drive lines, the
       speed and the disk-present check are what say the drive is still there, and
       a panel that stopped moving altogether would read as a crash rather than as
       a pause.

       Checked ahead of the empty drive case, so a pause is the same pause whether
       or not there is a disk in the drive - and so nothing plays a pass tone for a
       pass that never happened.

       Only these are repainted, not the whole of publish(): the sector rows have
       not changed, and repainting them four times a second would flicker to say
       nothing. sendConfig() goes out at the same pace so a browser that connects
       mid pause still learns what it has walked into.
    */
    if (_paused)
    {
        _nextStepMs = millis() + kPausedGapMs;
        _phase = Phase::settle;
        drawSignals();
        drawStatusLine();
        drawPassLine();
        paintSettings();
        paintSignals();
        paintStatus();
        sendConfig();
        return;
    }

    if (!present)
    {
        _nextStepMs = millis() + kNoDiskGapMs;
        _phase = Phase::settle;
        publish();
        return;
    }

    switch (_phase)
    {
    case Phase::settle:
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
        _spinner = (_spinner + 1) & 3;
        // Auto re-seek puts the head back every pass. With it off the head is
        // deliberately left where it is, which is what lets an adjustment show up
        // rather than being hidden by a fresh seek.
        if (_autoReseek)
            _recalPending = true;
        _nextStepMs = millis() + kPassGapMs;
        _phase = Phase::settle;
        publish();
        break;
    }
}

void XCopyHeadCalibration::readSide(uint8_t side)
{
    CalibrationResult result;
    bool ok = _floppy->calibrationRead(_cylinder, side, _recalPending, result);
    // The re-seek belongs to the pass, not to the side, so the first read of a
    // pass consumes it and the second stays on the cylinder it just reached.
    _recalPending = false;

    if (!ok)
    {
        _resultValid[side] = false;
        return;
    }

    _results[side] = result;
    _resultValid[side] = true;

    /*
       Accumulate. Saturating rather than wrapping: the four states the grid shows
       only care whether errors is none, some or all of passes, and a count that
       rolled over at 256 would turn a cylinder that has failed every read into one
       that looks perfect.
    */
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

void XCopyHeadCalibration::publish()
{
    drawSignals();
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
    paintSignals();
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
    sendResults();

    // Last, so the redraws above are finished with the SPI bus before a sample
    // starts streaming off the flash on it.
    playFeedback();
}

// SIGNALS

/*
   Six drive lines, sampled rather than latched from edges. The version this
   replaces toggled a flag on every CHANGE and painted the parity of it, which
   shows the right answer only if no edge is ever missed. Three of its six boxes
   did not work at all: the index interrupt was commented out, and write protect
   and the sixth box were painted white unconditionally.

   Read in one place because the TFT strip and the console panel both show them
   and must agree. The drive stays selected all session, so the status lines are
   genuinely driven rather than floating on the ribbon pull-ups.
*/
void XCopyHeadCalibration::readSignals(bool *out) const
{
    out[0] = digitalRead(_floppy->driveSelectPin()) == LOW; // active low
    out[1] = _floppy->getMotorStatus();
    out[2] = _floppy->readRPM() > 0.0f;                     // index pulses arriving
    out[3] = _floppy->readDiskChangeLine();                 // a disk is present
    out[4] = _floppy->getWriteProtect();
    out[5] = _floppy->readTrack0Line();
}

void XCopyHeadCalibration::drawSignals()
{
    bool state[SIGNAL_COUNT];
    readSignals(state);

    for (uint8_t i = 0; i < SIGNAL_COUNT; i++)
    {
        uint16_t colour = state[i] ? ST7735_GREEN : ST7735_RED;
        // Write protect is a fact about the disk rather than a fault, so it does
        // not get the same red as a line that should be asserted and is not.
        if (i == 4 && state[i])
            colour = ST7735_YELLOW;
        _graphics->getTFT()->fillRect(SIGNAL_X[i], ROW_SIGNAL_BOX, SIGNAL_W, SIGNAL_H, colour);
    }
}

// SETTINGS

const char *XCopyHeadCalibration::headName() const
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

void XCopyHeadCalibration::drawSettings()
{
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
            snprintf(value, sizeof(value), "%d", _cylinder);
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
            snprintf(value, sizeof(value), "%s", _paused ? "On" : "Off");
            break;
        }

        _graphics->getTFT()->fillRect(slot.valueX, slot.y, slot.valueW, 10, ST7735_BLACK);
        _graphics->drawText(slot.valueX, slot.y, ST7735_YELLOW, value);
    }
}

// RESULTS

char XCopyHeadCalibration::glyph(uint8_t verdict)
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

//! The console half of the pair above. Kept immediately beside it so the panel
//! and the screen cannot end up colouring the same verdict differently.
static const char *glyphConsoleColour(uint8_t verdict)
{
    switch (verdict)
    {
    case sectorOK:
        return HC_GREEN;
    case sectorCylLow:
    case sectorCylHigh:
        return HC_YELLOW;
    case sectorHeadWrong:
        return HC_MAGENTA;
    case sectorBadCheck:
        return HC_WHITE;
    default:
        return HC_RED;
    }
}

void XCopyHeadCalibration::drawResults()
{
    for (uint8_t side = 0; side < 2; side++)
    {
        uint8_t y = side == 0 ? ROW_HEAD0 : ROW_HEAD1;
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);

        _graphics->getTFT()->fillRect(0, y, 160, 10, ST7735_BLACK);

        char label[4];
        snprintf(label, sizeof(label), "%d:", side);
        _graphics->drawText(0, y, ST7735_WHITE, label);

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
   The one status sentence, written once and rendered twice.

   The TFT and the console panel are two views of the same pass; letting each
   compose its own wording is how they end up disagreeing about a drive somebody
   is in the middle of adjusting.
*/
const char *XCopyHeadCalibration::statusText(char *out, size_t size, uint16_t &tftColour) const
{
    if (_paused)
    {
        snprintf(out, size, "Paused - nothing is being read");
        tftColour = ST7735_CYAN;
        return HC_CYAN;
    }

    if (!_diskPresent)
    {
        snprintf(out, size, "No disk in drive");
        tftColour = ST7735_RED;
        return HC_RED;
    }

    // The most useful single line of the lot: the head is somewhere else
    // entirely, and this says where.
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
        snprintf(out, size, "Cyl %d seen as %d", _cylinder, seen);
        tftColour = ST7735_YELLOW;
        return HC_YELLOW;
    }

    if (any && allGood)
    {
        snprintf(out, size, "Aligned, all sectors okay");
        tftColour = ST7735_GREEN;
        return HC_GREEN;
    }

    snprintf(out, size, "Adjust for all sectors okay");
    tftColour = ST7735_WHITE;
    return HC_WHITE;
}

/*
   The pass counter, the spinner and the speed.

   Split out of publish() because the two do not always go together: a paused
   session takes no passes but the disk is still turning, and a speed readout that
   froze with the counter would be reporting a number that is no longer true.
*/
void XCopyHeadCalibration::drawPassLine()
{
    // The counter and the spinner move every pass whether or not anything the
    // operator cares about changed, so they are drawn rather than gated.
    char line[32];
    snprintf(line, sizeof(line), "%c pass %lu", SPINNER[_spinner], (unsigned long)_passes);
    _graphics->getTFT()->fillRect(0, ROW_PASS, 88, 10, ST7735_BLACK);
    _graphics->drawText(0, ROW_PASS, ST7735_WHITE, line);

    // Formatted by hand: printf() on this core has no float support linked in, so
    // "%.1f" prints nothing at all.
    float rpm = _floppy->readRPM();
    if (rpm > 0.0f)
        snprintf(line, sizeof(line), "%d.%d RPM", (int)rpm, ((int)(rpm * 10)) % 10);
    else
        snprintf(line, sizeof(line), "-- RPM");
    _graphics->getTFT()->fillRect(90, ROW_PASS, 70, 10, ST7735_BLACK);
    _graphics->drawText(90, ROW_PASS, ST7735_WHITE, line);
}

void XCopyHeadCalibration::drawStatusLine()
{
    char text[40];
    uint16_t colour = ST7735_WHITE;
    statusText(text, sizeof(text), colour);
    _graphics->drawText(0, ROW_STATUS, colour, text, true);
}

void XCopyHeadCalibration::drawStatic()
{
    _graphics->clearScreen();

    for (uint8_t i = 0; i < SIGNAL_COUNT; i++)
        _graphics->drawText(SIGNAL_X[i], ROW_SIGNAL_TEXT, ST7735_WHITE, SIGNAL_LABEL[i]);

    _graphics->drawText(0, ROW_TITLE, ST7735_CYAN, "HEAD CALIBRATION");
    _graphics->drawText(0, ROW_HELP1, ST7735_BLUE, "PUSH:field UP/DN:adjust");
    _graphics->drawText(0, ROW_HELP2, ST7735_BLUE, "RIGHT:re-seek LEFT:exit");

    _drawnStatic = true;
    drawSignals();
    drawSettings();
    drawResults();
    drawStatusLine();
}

// CONTROLS

void XCopyHeadCalibration::settingsChanged()
{
    _recalPending = true;
    if (_drawnStatic)
    {
        drawSettings();
        drawResults();
    }
    paintSettings();
    paintHead(0);
    paintHead(1);
    paintStatus();
    sendConfig();
}

void XCopyHeadCalibration::setCylinder(int cylinder)
{
    if (cylinder < 0)
        cylinder = 0;
    if (cylinder > kMaxCylinder)
        cylinder = kMaxCylinder;
    if ((uint8_t)cylinder == _cylinder)
        return;

    const uint8_t previous = _cylinder;
    _cylinder = (uint8_t)cylinder;
    // The results on screen describe somewhere the head no longer is.
    _resultValid[0] = false;
    _resultValid[1] = false;

    // Only the two cells that gained or lost the marker are repainted.
    if (_markedCylinder != 0xff)
        paintTallyCylinder(previous);
    paintTallyCylinder(_cylinder);
    _markedCylinder = _cylinder;

    settingsChanged();
}

void XCopyHeadCalibration::nudgeCylinder(int delta) { setCylinder((int)_cylinder + delta); }

void XCopyHeadCalibration::setHead(HeadSel head)
{
    if (head == _head)
        return;
    _head = head;
    settingsChanged();
}

void XCopyHeadCalibration::cycleHead()
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

void XCopyHeadCalibration::setAutoReseek(bool on)
{
    if (on == _autoReseek)
        return;
    _autoReseek = on;
    if (_drawnStatic)
        drawSettings();
    paintSettings();
    sendConfig();
}

void XCopyHeadCalibration::toggleAutoReseek() { setAutoReseek(!_autoReseek); }

void XCopyHeadCalibration::setSound(bool on)
{
    if (on == _sound)
        return;
    _sound = on;
    if (_drawnStatic)
        drawSettings();
    paintSettings();
    sendConfig();
}

void XCopyHeadCalibration::toggleSound() { setSound(!_sound); }

void XCopyHeadCalibration::setPaused(bool on)
{
    if (on == _paused)
        return;
    _paused = on;

    /*
       Resuming re-seeks and throws away the rows on screen. The head has had as
       long as the pause lasted to be moved by hand - that is usually why it was
       paused - so the first pass after it has to go and find the cylinder again
       rather than assume the last read left the head where it thinks.
    */
    if (!_paused)
    {
        _recalPending = true;
        _resultValid[0] = false;
        _resultValid[1] = false;
        _phase = Phase::settle;
        _nextStepMs = millis();
    }

    if (_drawnStatic)
    {
        drawSettings();
        drawResults();
        drawStatusLine();
    }
    paintSettings();
    paintHead(0);
    paintHead(1);
    paintStatus();
    sendConfig();
}

void XCopyHeadCalibration::togglePause() { setPaused(!_paused); }

/*
   One short sample a pass, describing the pass.

   Blocking, and knowingly so: playFile() forces its wait flag and returns only
   when the sample has finished, because the samples are read from SerialFlash on
   the same SPI bus the TFT sits on. It is called at the end of publish() for that
   reason - every redraw this pass is already done, so nothing is competing for
   the bus, and the capture for the next pass has not started.

   Three outcomes rather than two, because "reading the wrong cylinder entirely"
   and "reading this one badly" want different actions from whoever is holding the
   screwdriver.
*/
void XCopyHeadCalibration::playFeedback()
{
    if (!_sound || _audio == nullptr)
        return;

    if (!_diskPresent)
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

void XCopyHeadCalibration::setStepSize(uint8_t size)
{
    // 1, 10 and 40 are the three ATK offers that matter on a stroke this short.
    // Cycling covers all of them with two joystick directions.
    if (size != 1 && size != 10 && size != 40)
        return;
    if (size == _stepSize)
        return;
    _stepSize = size;
    if (_drawnStatic)
        drawSettings();
    paintSettings();
    sendConfig();
}

void XCopyHeadCalibration::reseek()
{
    _recalPending = true;
    _resultValid[0] = false;
    _resultValid[1] = false;
    if (_drawnStatic)
        drawResults();
    paintHead(0);
    paintHead(1);
    paintStatus();
}

void XCopyHeadCalibration::nextField()
{
    _field = (Field)(((uint8_t)_field + 1) % (uint8_t)Field::count);
    if (_drawnStatic)
        drawSettings();
}

void XCopyHeadCalibration::adjustField(int direction)
{
    switch (_field)
    {
    case Field::cylinder:
        nudgeCylinder(direction > 0 ? _stepSize : -_stepSize);
        break;
    case Field::step:
        if (direction > 0)
            setStepSize(_stepSize == 1 ? 10 : (_stepSize == 10 ? 40 : 1));
        else
            setStepSize(_stepSize == 40 ? 10 : (_stepSize == 10 ? 1 : 40));
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
        togglePause();
        break;
    }
}

bool XCopyHeadCalibration::handleKey(char key)
{
    switch (key)
    {
    case 'r':
    case 'R':
        reseek();
        break;
    case '}':
        nudgeCylinder(40);
        break;
    case '{':
        nudgeCylinder(-40);
        break;
    case ']':
        nudgeCylinder(10);
        break;
    case '[':
        nudgeCylinder(-10);
        break;
    case '+':
    case '=':
        nudgeCylinder(1);
        break;
    case '-':
    case '_':
        nudgeCylinder(-1);
        break;
    case 'h':
    case 'H':
        cycleHead();
        break;
    case 'a':
    case 'A':
        toggleAutoReseek();
        break;
    case 's':
    case 'S':
        toggleSound();
        break;
    case 'p':
    case 'P':
        togglePause();
        break;
    case 'q':
    case 'Q':
    case 0x1B: // Esc
    case 0x03: // Ctrl-C
        return false;
    default:
        break;
    }
    return true;
}

// CONSOLE PANEL
//
// Everything below writes straight to Serial, never through Log. Log mirrors each
// write to the browser and sleeps 6ms doing it, and copies the whole String on the
// way; a panel repainted twice a second for as long as somebody is adjusting a
// drive would be a permanent tax on both the loop and the few KB of heap left. The
// browser gets structured websocket messages instead, so nothing is lost - and it
// would only see the escape sequences anyway.

void XCopyHeadCalibration::repeat(char character, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
        Serial.write(character);
}

void XCopyHeadCalibration::solidRow(char left, char right)
{
    Serial.write(left);
    repeat('-', INNER);
    Serial.write(right);
    Serial.print("\r\n");
}

void XCopyHeadCalibration::joinRow()
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

void XCopyHeadCalibration::textRow(const char *text)
{
    char buffer[TEXT + 1];
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print("| ");
    Serial.print(buffer);
    Serial.print(" |\r\n");
}

char XCopyHeadCalibration::columnLabel(uint8_t col)
{
    return (col < 10) ? (char)('0' + col) : (char)('A' + (col - 10));
}

/**
 * @brief Park the cursor on a panel cell.
 *
 * Moves are relative (cursor up) so they survive the terminal scrolling when the
 * panel is printed at the bottom of the window; the column is absolute.
 *
 * @param line line number within the panel, 0 being the top border
 * @param column 1 based terminal column
 */
void XCopyHeadCalibration::moveTo(uint8_t line, uint8_t column)
{
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "\033[s\033[%uA\033[%uG", (unsigned)(LINES - line), (unsigned)column);
    Serial.print(buffer);
}

void XCopyHeadCalibration::restore()
{
    Serial.print("\033[u");
}

//! One full width field, moved to and restored from as a pair.
//!
//! moveTo() saves the cursor as its first act, so a second one before the restore
//! overwrites the saved position with a spot inside the table - and everything
//! printed afterwards, the prompt included, lands in the middle of the panel.
//! Pairing them is what keeps the cursor parked below the frame.
void XCopyHeadCalibration::paintTextRow(uint8_t line, const char *colour, const char *text)
{
    moveTo(line, 3);
    Serial.print(colour);
    Serial.printf("%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print(HC_RESET);
    restore();
}

void XCopyHeadCalibration::paintSettings()
{
    if (!_panelActive)
        return;

    char buffer[TEXT + 1];
    char rpmText[16];

    float rpm = _floppy->readRPM();
    if (rpm > 0.0f)
        snprintf(rpmText, sizeof(rpmText), "%d.%d RPM", (int)rpm, ((int)(rpm * 10)) % 10);
    else
        snprintf(rpmText, sizeof(rpmText), "no index");

    snprintf(buffer, sizeof(buffer), "CYLINDER  : %-8d STEP  : %d", _cylinder, _stepSize);
    paintTextRow(LINE_SETTINGS, HC_WHITE, buffer);

    snprintf(buffer, sizeof(buffer), "HEAD(S)   : %-8s AUTO  : %-4s SOUND : %s",
             headName(), _autoReseek ? "On" : "Off", _sound ? "On" : "Off");
    paintTextRow(LINE_SETTINGS + 1, HC_WHITE, buffer);

    snprintf(buffer, sizeof(buffer), "PASS      : %-8lu SPEED : %s",
             (unsigned long)_passes, rpmText);
    paintTextRow(LINE_SETTINGS + 2, HC_WHITE, buffer);
}

void XCopyHeadCalibration::paintSignals()
{
    if (!_panelActive)
        return;

    bool state[SIGNAL_COUNT];
    readSignals(state);

    moveTo(LINE_SIGNALS, 3);

    // The visible width is accumulated alongside the coloured output, because the
    // escapes are not printing characters and would otherwise throw the padding out.
    uint8_t visible = 0;
    Serial.print(HC_WHITE);
    Serial.print("SIGNALS   : ");
    visible += 12;

    for (uint8_t i = 0; i < SIGNAL_COUNT; i++)
    {
        if (i > 0)
        {
            Serial.write(' ');
            visible++;
        }
        // Write protect is a fact about the disk rather than a fault, so it does
        // not get the same red as a line that should be asserted and is not.
        Serial.print(state[i] ? (i == 4 ? HC_YELLOW : HC_GREEN) : HC_GREY);
        Serial.print(SIGNAL_LABEL[i]);
        visible += strlen(SIGNAL_LABEL[i]);
    }

    Serial.print(HC_RESET);
    if (visible < TEXT)
        repeat(' ', TEXT - visible);
    restore();
}

/*
   The realtime half: what the head is reading this revolution.

   Kept beside the accumulated grid rather than replaced by it, because the two
   answer different questions. This one says which sectors are being missed right
   now, and moves the instant the drive is touched; the grid below says whether a
   cylinder has ever been read cleanly, which is what tells a misaligned head from
   a marginal one.
*/
void XCopyHeadCalibration::paintHead(uint8_t side)
{
    if (!_panelActive || side > 1)
        return;

    const uint8_t line = LINE_HEAD + side;
    bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);

    char prefix[HEADPREFIX + 8];
    snprintf(prefix, sizeof(prefix), "%d (%s)", side, side == 0 ? "Lower" : "Upper");
    moveTo(line, 3);
    Serial.print(HC_WHITE);
    Serial.printf("%-*.*s", (int)HEADPREFIX, (int)HEADPREFIX, prefix);
    Serial.print(HC_RESET);
    restore();

    // Glyph field. Eleven sectors on a two column pitch is exactly GLYPHFIELD; an
    // HD track is capped there rather than reflowing the panel for a disk almost
    // nobody has, and the count beside it still reports the whole track.
    moveTo(line, 3 + HEADPREFIX);
    uint8_t visible = 0;
    if (!wanted || !_resultValid[side])
    {
        const char *text = !wanted ? "not selected" : (_diskPresent ? "no read" : "no disk");
        Serial.print(!wanted ? HC_GREY : HC_RED);
        Serial.print(text);
        Serial.print(HC_RESET);
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
        Serial.print(HC_RESET);
    }
    if (visible < GLYPHFIELD)
        repeat(' ', GLYPHFIELD - visible);
    restore();

    moveTo(line, 3 + HEADPREFIX + GLYPHFIELD + 1);
    char result[RESULTFIELD + 1];
    if (!wanted || !_resultValid[side])
        snprintf(result, sizeof(result), "%s", "-");
    else
        // Right aligned, so the two heads stack and a count that drops by one is
        // seen as a digit changing rather than the whole field shifting.
        snprintf(result, sizeof(result), "%2d/%d okay", _results[side].valid, _results[side].sectorCount);

    if (wanted && _resultValid[side] && _results[side].valid == _results[side].sectorCount)
        Serial.print(HC_GREEN);
    else if (wanted && _resultValid[side])
        Serial.print(HC_YELLOW);
    else
        Serial.print(HC_GREY);
    Serial.printf("%-*.*s", (int)RESULTFIELD, (int)RESULTFIELD, result);
    Serial.print(HC_RESET);
    restore();
}

void XCopyHeadCalibration::paintStatus()
{
    if (!_panelActive)
        return;

    char text[TEXT + 1];
    uint16_t ignored = 0;
    const char *colour = statusText(text, sizeof(text), ignored);

    // The spinner rides on the status row, so a panel with nothing changing on it
    // still shows that the drive is being read.
    char line[TEXT + 1];
    snprintf(line, sizeof(line), "%c %s", SPINNER[_spinner], text);
    paintTextRow(LINE_STATUS, colour, line);
}

// ACCUMULATED TALLY

XCopyHeadCalibration::TallyState XCopyHeadCalibration::tallyState(uint8_t cylinder, uint8_t side) const
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
        return HC_GREEN;
    case 2:
        return HC_YELLOW;
    case 3:
        return HC_RED;
    default:
        return HC_GREY;
    }
}

void XCopyHeadCalibration::paintTallyCell(uint8_t cylinder, uint8_t side)
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
    if (cylinder == _cylinder)
        Serial.print("\033[7m");
    Serial.print(tallyColour(state));
    Serial.write(tallyGlyph(state));
    Serial.print(HC_RESET);
    restore();
}

void XCopyHeadCalibration::paintTallyCylinder(uint8_t cylinder)
{
    paintTallyCell(cylinder, 0);
    paintTallyCell(cylinder, 1);
}

void XCopyHeadCalibration::panelBegin()
{
    char buffer[TEXT + 1];

    _panelActive = true;

    Serial.print("\r\n");

    solidRow('.', '.');
    textRow("CONTINUOUS HEAD CALIBRATION TEST");
    textRow("Use an AmigaDOS disk from a calibrated drive.");
    solidRow('|', '|');

    // Placeholders. Every one of these is overwritten in place by the painters
    // before the first pass finishes.
    textRow("");
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
        Serial.print(HC_RESET);
        Serial.write(' ');
        Serial.print(legendName[i]);
        Serial.print("  ");
        visible += width;
    }
    repeat(' ', TEXT - visible);
    Serial.print(" |\r\n");

    /*
       Two rows rather than one. The single row this replaces read

           r seek +/-1 []10 {}40 h head a auto s snd q quit

       which is every key and no hint: the one thing an operator does constantly is
       step the head, and "+/-1 []10 {}40" does not say that is what those are for.
       Stepping gets a row of its own, spelled out, and the rest follow on the next.

       Both are sized to TEXT - see textRow(), which truncates rather than wraps, so
       a row that outgrows the frame loses its tail silently.
    */
    textRow("STEP  -/+ one cyl   [/] ten   {/} forty");
    textRow("KEYS  p pause   r re-seek   h head");
    textRow("      a auto    s sound     q quit");
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
            Serial.print(HC_GREY);
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
            Serial.print(HC_RESET);
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
    paintTallyCylinder(_cylinder);
    _markedCylinder = _cylinder;
}

void XCopyHeadCalibration::panelEnd()
{
    if (!_panelActive)
        return;

    _panelActive = false;
    // The cursor has been parked below the bottom border all along, so a prompt
    // printed after this lands clear of the panel rather than inside it.
    Serial.print("\r\n");
}

// WEB

void XCopyHeadCalibration::sendConfig()
{
    if (_esp == nullptr)
        return;

    /*
       Speed goes out in tenths of an RPM rather than as text. printf() on this
       core has no float support linked in, so "%.1f" would print nothing at all -
       which is why the two panels above format it by hand - and an integer is the
       one shape that survives the trip and can still be laid out by the browser.
       Zero means no index pulses, the same thing "-- RPM" says on the TFT.
    */
    const float rpm = _floppy->readRPM();
    const int rpmTenths = rpm > 0.0f ? (int)((rpm * 10.0f) + 0.5f) : 0;

    char message[96];
    snprintf(message, sizeof(message), "broadcast headCalConfig,%d,%d,%d,%d,%d,%lu,%d,%d,%d\r\n",
             _cylinder, _stepSize, (int)_head, _autoReseek ? 1 : 0, _active ? 1 : 0,
             (unsigned long)_passes, _sound ? 1 : 0, _paused ? 1 : 0, rpmTenths);
    _esp->print(message);
}

void XCopyHeadCalibration::sendClosed()
{
    if (_esp == nullptr)
        return;
    _esp->print("broadcast headCalConfig,0,1,2,0,0,0,0,0,0\r\n");
}

void XCopyHeadCalibration::sendResults()
{
    if (_esp == nullptr)
        return;

    char glyphs[24];
    char message[96];

    // Fixed buffers rather than concatenated Strings, for the same reason
    // drawFlux() uses them: this goes out a few times a second, all session.
    for (uint8_t side = 0; side < 2; side++)
    {
        // The accumulated counts ride along with the realtime row rather than going
        // out as a message of their own: the browser needs both to redraw one
        // cylinder, and they are always produced together.
        const uint16_t track = (_cylinder * 2) + side;
        const TrackTally &t = _tally[track < MAX_TRACKS ? track : 0];

        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
        if (!wanted || !_resultValid[side])
        {
            // Carries the tally even so. Sending zeroes here told the browser the
            // cylinder had never been tested, so selecting one head wiped from the
            // grid the very history it exists to keep for the other.
            snprintf(message, sizeof(message), "broadcast headCal,%d,%d,0,0,-,%d,%d\r\n",
                     _cylinder, side, t.passes, t.errors);
            _esp->print(message);
            continue;
        }

        const CalibrationResult &r = _results[side];
        uint8_t cap = (uint8_t)(sizeof(glyphs) - 1);
        uint8_t n = r.sectorCount > cap ? cap : r.sectorCount;
        for (uint8_t i = 0; i < n; i++)
            glyphs[i] = glyph(r.status[i]);
        glyphs[n] = 0;

        snprintf(message, sizeof(message), "broadcast headCal,%d,%d,%d,%d,%s,%d,%d\r\n",
                 _cylinder, side, r.valid, r.sectorCount, glyphs, t.passes, t.errors);
        _esp->print(message);
    }

    sendConfig();
}
