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
static const uint8_t ROW_HEAD0 = 58;
static const uint8_t ROW_HEAD1 = 68;
static const uint8_t ROW_PASS = 80;
static const uint8_t ROW_HELP1 = 92;
static const uint8_t ROW_HELP2 = 102;
static const uint8_t ROW_STATUS = 114;

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
    _field = Field::cylinder;
    _phase = Phase::settle;
    _passes = 0;
    _spinner = 0;
    _sinceConsole = 0;
    _recalPending = true;
    _resultValid[0] = false;
    _resultValid[1] = false;
    _dirty = true;
    _drawnStatic = false;
    _nextStepMs = millis();

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
        _dirty = true;
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
        if (_resultValid[side])
            _dirty = true;
        _resultValid[side] = false;
        return;
    }

    if (!_resultValid[side] ||
        _results[side].valid != result.valid ||
        _results[side].cylinderSeen != result.cylinderSeen ||
        memcmp(_results[side].status, result.status, sizeof(result.status)) != 0)
    {
        _dirty = true;
    }

    _results[side] = result;
    _resultValid[side] = true;
}

void XCopyHeadCalibration::publish()
{
    drawSignals();
    drawResults();
    drawStatusLine();

    // The pass counter and the spinner move every pass whether or not anything the
    // operator cares about changed, so they are drawn here rather than gated.
    char line[32];
    snprintf(line, sizeof(line), "%c pass %lu", SPINNER[_spinner], (unsigned long)_passes);
    _graphics->getTFT()->fillRect(0, ROW_PASS, 88, 10, ST7735_BLACK);
    _graphics->drawText(0, ROW_PASS, ST7735_WHITE, line);

    float rpm = _floppy->readRPM();
    if (rpm > 0.0f)
        snprintf(line, sizeof(line), "%d.%d RPM", (int)rpm, ((int)(rpm * 10)) % 10);
    else
        snprintf(line, sizeof(line), "-- RPM");
    _graphics->getTFT()->fillRect(90, ROW_PASS, 70, 10, ST7735_BLACK);
    _graphics->drawText(90, ROW_PASS, ST7735_WHITE, line);

    _sinceConsole++;
    if (_dirty || _sinceConsole >= kConsoleHeartbeat)
    {
        printResults();
        sendResults();
        _sinceConsole = 0;
    }

    _dirty = false;
}

// SIGNALS

void XCopyHeadCalibration::drawSignals()
{
    /*
       Six drive lines, sampled rather than latched from edges. The version this
       replaces toggled a flag on every CHANGE and painted the parity of it, which
       shows the right answer only if no edge is ever missed. Three of its six
       boxes did not work at all: the index interrupt was commented out, and write
       protect and the sixth box were painted white unconditionally.
    */
    bool state[SIGNAL_COUNT];
    state[0] = digitalRead(_floppy->driveSelectPin()) == LOW; // active low
    state[1] = _floppy->getMotorStatus();
    state[2] = _floppy->readRPM() > 0.0f;                     // index pulses arriving
    state[3] = _floppy->readDiskChangeLine();                 // a disk is present
    state[4] = _floppy->getWriteProtect();
    state[5] = _floppy->readTrack0Line();

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
        default:
            snprintf(value, sizeof(value), "%s", _autoReseek ? "On" : "Off");
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

void XCopyHeadCalibration::drawStatusLine()
{
    char text[40];
    uint16_t colour = ST7735_WHITE;

    if (!_diskPresent)
    {
        snprintf(text, sizeof(text), "No disk in drive");
        colour = ST7735_RED;
    }
    else
    {
        // The most useful single line on the screen: the head is somewhere else
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
            snprintf(text, sizeof(text), "Cyl %d seen as %d", _cylinder, seen);
            colour = ST7735_YELLOW;
        }
        else if (any && allGood)
        {
            snprintf(text, sizeof(text), "Aligned, all sectors okay");
            colour = ST7735_GREEN;
        }
        else
        {
            snprintf(text, sizeof(text), "Adjust for all sectors okay");
        }
    }

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
    _dirty = true;
    if (_drawnStatic)
    {
        drawSettings();
        drawResults();
    }
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

    _cylinder = (uint8_t)cylinder;
    // The results on screen describe somewhere the head no longer is.
    _resultValid[0] = false;
    _resultValid[1] = false;
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
    _dirty = true;
    if (_drawnStatic)
        drawSettings();
    sendConfig();
}

void XCopyHeadCalibration::toggleAutoReseek() { setAutoReseek(!_autoReseek); }

void XCopyHeadCalibration::setStepSize(uint8_t size)
{
    // 1, 10 and 40 are the three ATK offers that matter on a stroke this short.
    // Cycling covers all of them with two joystick directions.
    if (size != 1 && size != 10 && size != 40)
        return;
    if (size == _stepSize)
        return;
    _stepSize = size;
    _dirty = true;
    if (_drawnStatic)
        drawSettings();
    sendConfig();
}

void XCopyHeadCalibration::reseek()
{
    _recalPending = true;
    _resultValid[0] = false;
    _resultValid[1] = false;
    _dirty = true;
    if (_drawnStatic)
        drawResults();
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
    default:
        toggleAutoReseek();
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

// CONSOLE

void XCopyHeadCalibration::printBanner()
{
    Serial.print("\r\n-- Continuous Head Calibration Test --\r\n");
    Serial.print(" r:re-seek  +/-:1  [ ]:10  { }:40  h:head  a:auto  q:quit\r\n");
    Serial.print(" Use an AmigaDOS disk written by a well calibrated drive.\r\n");
    Serial.print(" Adjust the drive until every sector is found on both sides.\r\n");
    Serial.print("   (.:okay  X:missing  -:cyl-low  +:cyl-high  h:wrong-side  c:bad-checksum)\r\n\r\n");
}

void XCopyHeadCalibration::printResults()
{
    /*
       Written straight to Serial from a fixed buffer, not through Log. Log mirrors
       every write to the browser and sleeps 6ms doing it, and copies the whole
       String on the way; at two lines a pass for as long as somebody is adjusting a
       drive that is a permanent tax on both the loop and the few KB of heap left.
       The browser gets a structured websocket message instead, so nothing is lost.
    */
    char glyphs[24];
    char line[96];

    if (!_diskPresent)
    {
        Serial.print("  No disk in drive\r\n");
        return;
    }

    for (uint8_t side = 0; side < 2; side++)
    {
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
        if (!wanted)
            continue;

        if (!_resultValid[side])
        {
            snprintf(line, sizeof(line), "%c Cyl %d Head %d (%s): read failed\r\n",
                     SPINNER[_spinner], _cylinder, side, side == 0 ? "Lower" : "Upper");
            Serial.print(line);
            continue;
        }

        const CalibrationResult &r = _results[side];
        uint8_t cap = (uint8_t)(sizeof(glyphs) - 1);
        uint8_t n = r.sectorCount > cap ? cap : r.sectorCount;
        for (uint8_t i = 0; i < n; i++)
            glyphs[i] = glyph(r.status[i]);
        glyphs[n] = 0;

        snprintf(line, sizeof(line), "%c Cyl %d Head %d (%s): %s  (%d/%d okay)\r\n",
                 SPINNER[_spinner], _cylinder, side, side == 0 ? "Lower" : "Upper",
                 glyphs, r.valid, r.sectorCount);
        Serial.print(line);

        if (r.cylinderSeen >= 0 && r.cylinderSeen != (int8_t)_cylinder)
        {
            snprintf(line, sizeof(line), "    head is reading cylinder %d\r\n", r.cylinderSeen);
            Serial.print(line);
        }
    }
}

// WEB

void XCopyHeadCalibration::sendConfig()
{
    if (_esp == nullptr)
        return;

    char message[80];
    snprintf(message, sizeof(message), "broadcast headCalConfig,%d,%d,%d,%d,%d,%lu\r\n",
             _cylinder, _stepSize, (int)_head, _autoReseek ? 1 : 0, _active ? 1 : 0,
             (unsigned long)_passes);
    _esp->print(message);
}

void XCopyHeadCalibration::sendClosed()
{
    if (_esp == nullptr)
        return;
    _esp->print("broadcast headCalConfig,0,1,2,0,0,0\r\n");
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
        bool wanted = (side == 0) ? (_head != HeadSel::upper) : (_head != HeadSel::lower);
        if (!wanted || !_resultValid[side])
        {
            snprintf(message, sizeof(message), "broadcast headCal,%d,%d,0,0,-\r\n", _cylinder, side);
            _esp->print(message);
            continue;
        }

        const CalibrationResult &r = _results[side];
        uint8_t cap = (uint8_t)(sizeof(glyphs) - 1);
        uint8_t n = r.sectorCount > cap ? cap : r.sectorCount;
        for (uint8_t i = 0; i < n; i++)
            glyphs[i] = glyph(r.status[i]);
        glyphs[n] = 0;

        snprintf(message, sizeof(message), "broadcast headCal,%d,%d,%d,%d,%s\r\n",
                 _cylinder, side, r.valid, r.sectorCount, glyphs);
        _esp->print(message);
    }

    sendConfig();
}
