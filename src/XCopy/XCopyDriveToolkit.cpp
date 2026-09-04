#include "XCopyDriveToolkit.h"
#include "XCopyLog.h"

/*
   Screen layout, 160 x 128 landscape.

   Two rows of indicators rather than one: the six outputs sit above the five
   inputs, because "what am I driving" and "what is the drive saying" are the two
   halves of every question asked here, and a single strip of eleven would put
   them in one undifferentiated line. Same 25 pixel pitch the calibration screen
   uses, so a box is a box across both screens.

   Rows are tabulated rather than open coded into every draw call because the TFT
   is repainted in place: a cell is cleared and rewritten on its own, and a y that
   disagreed between the clear and the write would leave debris.
*/
static const uint8_t ROW_OUT_BOX = 0;
static const uint8_t ROW_OUT_TEXT = 10;
static const uint8_t ROW_IN_BOX = 24;
static const uint8_t ROW_IN_TEXT = 34;
static const uint8_t ROW_TITLE = 50;
static const uint8_t ROW_POS = 64;
static const uint8_t ROW_SPEED = 76;
static const uint8_t ROW_STATUS = 92;
static const uint8_t ROW_HELP1 = 108;
static const uint8_t ROW_HELP2 = 118;

static const uint8_t BOX_W = 20;
static const uint8_t BOX_H = 8;
static const uint8_t BOX_X[6] = {0, 25, 50, 75, 100, 125};

//! No ST7735_GREY in the library, and a line at rest has to be visibly different
//! from one that is asserted without being alarming. Mid grey in RGB565.
static const uint16_t DT_GREY = 0x4208;

// Console colours, paired one to one with the TFT colours below so the panel and
// the screen describe the same drive in the same language. Same set XCopyTrackMap
// and the calibration panel use, for the same reason.
#define DT_RESET  "\033[0m"
#define DT_DIM    "\033[0;90m"
#define DT_WHITE  "\033[1;37m"
#define DT_GREEN  "\033[0;32m"
#define DT_YELLOW "\033[0;33m"
#define DT_RED    "\033[1;31m"
#define DT_CYAN   "\033[0;36m"

/*
   Four characters at most, so one table serves the console and the TFT both. The
   TFT draws these under a 20 pixel box on a 25 pixel pitch, and a fifth character
   would overrun into its neighbour.
*/
static const char *const SIG_LABEL[XCopyDriveToolkit::sigCount] = {
    "SEL", "MOT", "DIR", "SIDE", "DENS", "STEP", "IDX", "RDAT", "T0", "WP", "CHG"};

//! Connector pin, shown so the table can be read against a drive's own manual.
static const uint8_t SIG_PIN[XCopyDriveToolkit::sigCount] = {
    12, 16, 18, 32, 2, 20, 8, 30, 26, 28, 34};

const char *XCopyDriveToolkit::label(uint8_t sig) { return SIG_LABEL[sig]; }
uint8_t XCopyDriveToolkit::idcPin(uint8_t sig) { return SIG_PIN[sig]; }

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

void XCopyDriveToolkit::begin(XCopyGraphics *graphics, XCopyESP8266 *esp, XCopyFloppy *floppy)
{
    _graphics = graphics;
    _esp = esp;
    _floppy = floppy;

    /*
       Opens with every output released.

       A diagnostic that started by asserting things would destroy the first
       measurement it is asked for: the question is what this drive does when a
       line is driven, and that needs a known quiet starting point to be a
       question at all.
    */
    _sel = false;
    _mot = false;
    _inward = false;
    _upper = true;
    _high = true;
    _sticky = false;
    _cylinder = -1;
    _stepPulses = 0;
    _rpm = 0.0f;
    _idxEdges = 0;
    _rdataActive = false;
    _panelActive = false;

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
       The idle timeout would stop the motor five seconds into a session where the
       whole point may be watching a spindle that has been asked to run. The
       toolkit owns the motor line for the duration and puts this back in end().
    */
    _floppy->setMotorIdleOff(false);
    /*
       beginRPM() owns the index interrupt and endRPM() gives it back, so the index
       indicator, the edge count and the speed readout all cost no interrupt of our
       own - and there is nothing left for an exit path to forget to detach. The
       feature this replaces attached three interrupts in begin() and detached none
       of them.
    */
    _floppy->clearIndexEdges();
    _floppy->beginRPM();

    _nextSampleMs = millis();
    _active = true;
    sample();
}

void XCopyDriveToolkit::end()
{
    if (!_active)
        return;

    releaseOutputs();
    _floppy->endRPM();
    _floppy->setMotorIdleOff(true);
    _active = false;
}

void XCopyDriveToolkit::update()
{
    if (!_active)
        return;

    if ((int32_t)(millis() - _nextSampleMs) < 0)
        return;
    _nextSampleMs = millis() + kSampleMs;

    sample();
    drawSignals();
    drawReadouts();
    paintAll();
    sendState();
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
       real verdict on the strip: red specifically when the motor has been asked
       to run and nothing has arrived, which is the fault being hunted. With the
       motor off there is nothing to expect, so it rests rather than alarms.
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
        if (_cylinder < 0)
            snprintf(out, length, "cyl ?");
        else
            snprintf(out, length, "cyl %d", _cylinder);
        break;
    default:
        out[0] = '\0';
        break;
    }
}

const char *XCopyDriveToolkit::statusText(char *out, size_t length, uint16_t &tftColour) const
{
    if (!_raw[sigChg])
    {
        tftColour = ST7735_YELLOW;
        snprintf(out, length, "No disk in the drive");
        return DT_YELLOW;
    }

    if (!_mot)
    {
        tftColour = DT_GREY;
        snprintf(out, length, _sel ? "Selected, motor off" : "Outputs released");
        return DT_DIM;
    }

    if (_rpm > 0.0f)
    {
        char speed[16];
        formatRpm(_rpm, speed, sizeof(speed));
        tftColour = ST7735_GREEN;
        snprintf(out, length, "Spinning at %s", speed);
        return DT_GREEN;
    }

    if (_idxEdges > 0)
    {
        tftColour = ST7735_YELLOW;
        snprintf(out, length, "%lu edges, no rate yet", (unsigned long)_idxEdges);
        return DT_YELLOW;
    }

    /*
       The whole reason the tool exists, so it says the next thing to try rather
       than just naming the symptom. Stepping works with the spindle stopped, so
       "motor on and no index" almost always means the drive is not taking its
       motor command from where we are giving it.
    */
    tftColour = ST7735_RED;
    snprintf(out, length, "MOTOR ON, no index");
    return DT_RED;
}

// TFT

void XCopyDriveToolkit::drawSignals()
{
    if (_graphics == nullptr)
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

void XCopyDriveToolkit::drawReadouts()
{
    if (_graphics == nullptr)
        return;

    char buffer[40];
    char speed[16];

    if (_cylinder < 0)
        snprintf(buffer, sizeof(buffer), "CYL ?   %s", _raw[sigSide] ? "HEAD 1" : "HEAD 0");
    else
        snprintf(buffer, sizeof(buffer), "CYL %-3d %s", _cylinder,
                 _raw[sigSide] ? "HEAD 1" : "HEAD 0");
    _graphics->drawText(0, ROW_POS, ST7735_WHITE, buffer, true);

    formatRpm(_rpm, speed, sizeof(speed));
    snprintf(buffer, sizeof(buffer), "%s  IDX %lu", speed, (unsigned long)_idxEdges);
    _graphics->drawText(0, ROW_SPEED, ST7735_WHITE, buffer, true);

    uint16_t colour = ST7735_WHITE;
    statusText(buffer, sizeof(buffer), colour);
    _graphics->drawText(0, ROW_STATUS, colour, buffer, true);
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

    _graphics->drawText(0, ROW_TITLE, ST7735_CYAN, "DRIVE TOOLKIT");
    // Says plainly that the joystick does nothing here, so an operator does not
    // spend a minute pushing it before working that out.
    _graphics->drawText(0, ROW_HELP1, ST7735_BLUE, "Display only. Drive from");
    _graphics->drawText(0, ROW_HELP2, ST7735_BLUE, "the web or the console.");

    drawSignals();
    drawReadouts();
}

// CONSOLE

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

void XCopyDriveToolkit::textRow(const char *text)
{
    char buffer[TEXT + 1];
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print("| ");
    Serial.print(buffer);
    Serial.print(" |\r\n");
}

/**
 * @brief Park the cursor on a table cell.
 *
 * Moves are relative (cursor up) so they survive the terminal scrolling when the
 * table is printed at the bottom of the window; the column is absolute.
 *
 * moveTo() saves the cursor as its first act, so a second one before the restore
 * overwrites the saved position with a spot inside the table - and everything
 * printed afterwards, the prompt included, lands in the middle of it. Always
 * pair them.
 *
 * @param line line number within the table, 0 being the top border
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

void XCopyDriveToolkit::panelBegin()
{
    char buffer[TEXT + 1];

    _panelActive = true;
    Serial.print("\r\n");

    solidRow('.', '.');
    textRow("DRIVE TOOLKIT");
    textRow("Every line sampled. Outputs drive one at a time.");
    solidRow('|', '|');

    snprintf(buffer, sizeof(buffer), " %-8s %3s  %-3s   %-*s %s",
             "SIGNAL", "PIN", "DIR", (int)WIDTH_STATE, "STATE", "COUNT / RATE");
    textRow(buffer);

    // Placeholders. Every one is overwritten in place by paintSignal() before the
    // first refresh finishes.
    for (uint8_t i = 0; i < sigCount; i++)
        textRow("");

    solidRow('|', '|');
    textRow("");
    solidRow('|', '|');

    /*
       Three rows rather than one. The keys divide into what drives a line, what
       moves the head, and what manages the session, and a single run of them
       reads as a wall of letters with no shape.

       All three are sized to TEXT - see textRow(), which truncates rather than
       wraps, so a row that outgrows the frame loses its tail silently.
    */
    textRow("LINES s sel   m mot   d dir   h side   n dens");
    textRow("HEAD  SPC step   -/+ one cyl   [/] ten   0 recal");
    textRow("KEYS  c clear   k sticky   r release   q quit");
    solidRow('\'', '\'');

    paintAll();
}

void XCopyDriveToolkit::panelEnd()
{
    if (!_panelActive)
        return;

    _panelActive = false;
    // The cursor has been parked below the bottom border all along, so a prompt
    // printed after this lands clear of the table rather than inside it.
    Serial.print("\r\n");
}

void XCopyDriveToolkit::paintSignal(uint8_t sig)
{
    if (!_panelActive)
        return;

    const char *colour = DT_DIM;
    switch (_level[sig])
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

    const uint8_t line = LINE_SIGNAL0 + sig;
    char buffer[32];

    // Name, pin and direction never change, so they are painted once here with the
    // row and then left alone. Only the two right hand fields are repainted.
    moveTo(line, 3);
    Serial.print(DT_WHITE);
    snprintf(buffer, sizeof(buffer), " %-8s %3u  %-3s", SIG_LABEL[sig],
             (unsigned)SIG_PIN[sig], sig < kOutputCount ? "OUT" : "IN");
    Serial.print(buffer);
    Serial.print(DT_RESET);
    restore();

    moveTo(line, COL_STATE);
    Serial.print(colour);
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)WIDTH_STATE, (int)WIDTH_STATE, stateText(sig));
    Serial.print(buffer);
    Serial.print(DT_RESET);
    restore();

    char value[24];
    valueText(sig, value, sizeof(value));
    moveTo(line, COL_VALUE);
    Serial.print(DT_DIM);
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)WIDTH_VALUE, (int)WIDTH_VALUE, value);
    Serial.print(buffer);
    Serial.print(DT_RESET);
    restore();
}

void XCopyDriveToolkit::paintStatus()
{
    if (!_panelActive)
        return;

    char text[48];
    uint16_t ignored = 0;
    const char *colour = statusText(text, sizeof(text), ignored);

    char buffer[TEXT + 1];
    // The visible width is accumulated separately from the coloured output,
    // because the escapes are not printing characters and would otherwise throw
    // the padding - and with it the right hand frame - out.
    snprintf(buffer, sizeof(buffer), " %-*.*s", (int)(TEXT - 1), (int)(TEXT - 1), text);

    moveTo(LINE_STATUS, 3);
    Serial.print(colour);
    Serial.print(buffer);
    Serial.print(DT_RESET);
    restore();
}

void XCopyDriveToolkit::paintAll()
{
    if (!_panelActive)
        return;

    for (uint8_t i = 0; i < sigCount; i++)
        paintSignal(i);
    paintStatus();
}

// CONTROLS

void XCopyDriveToolkit::setSelect(bool on)
{
    _sel = on;
    _floppy->setSelectLine(on);
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
}

void XCopyDriveToolkit::setDirection(bool inward)
{
    _inward = inward;
    _floppy->setDirFast(inward ? 1 : 0);
}

void XCopyDriveToolkit::setSideUpper(bool upper)
{
    _upper = upper;
    _floppy->setSideFast(upper ? 0 : 1);
}

void XCopyDriveToolkit::setDensity(bool high)
{
    _high = high;
    _floppy->setDensityLine(high);
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

    if (_cylinder >= 0)
    {
        _cylinder += _inward ? 1 : -1;
        if (_cylinder < 0)
            _cylinder = 0;
        if (_cylinder > (int)(MAX_CYLINDERS - 1))
            _cylinder = MAX_CYLINDERS - 1;
    }
}

void XCopyDriveToolkit::nudgeCylinder(int delta)
{
    if (delta == 0)
        return;

    // Nothing sensible to seek relative to until the head has been found.
    if (_cylinder < 0)
    {
        recalibrate();
        if (_cylinder < 0)
            return;
    }
    seekCylinder(_cylinder + delta);
}

void XCopyDriveToolkit::seekCylinder(int cylinder)
{
    if (cylinder < 0)
        cylinder = 0;
    if (cylinder > (int)(MAX_CYLINDERS - 1))
        cylinder = MAX_CYLINDERS - 1;

    if (_cylinder < 0)
    {
        recalibrate();
        if (_cylinder < 0)
            return;
    }

    _floppy->gotoLogicTrack((cylinder * 2) + (_upper ? 0 : 1));
    _cylinder = cylinder;
}

void XCopyDriveToolkit::recalibrate()
{
    _floppy->seek0();
    // seek0() steps outward until TRACK 0 asserts. If the line never comes up the
    // head is not where the drive says it is, and claiming cylinder 0 would be a
    // guess dressed as a measurement.
    _cylinder = _floppy->readTrack0Line() ? 0 : -1;
}

void XCopyDriveToolkit::clearCounters()
{
    _floppy->clearIndexEdges();
    _stepPulses = 0;
    memset(_moved, 0, sizeof(_moved));
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
    _cylinder = -1;
}

bool XCopyDriveToolkit::handleKey(char key)
{
    switch (key)
    {
    case 's':
    case 'S':
        toggleSelect();
        break;
    case 'm':
    case 'M':
        toggleMotor();
        break;
    case 'd':
    case 'D':
        toggleDirection();
        break;
    case 'h':
    case 'H':
        toggleSide();
        break;
    case 'n':
    case 'N':
        toggleDensity();
        break;
    case ' ':
        pulseStep();
        break;
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
    case '0':
        recalibrate();
        break;
    case 'c':
    case 'C':
        clearCounters();
        break;
    case 'k':
    case 'K':
        toggleSticky();
        break;
    case 'r':
    case 'R':
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

    // Repaint from the same sample the change produced, so a key press looks
    // immediate rather than waiting up to kSampleMs for the next refresh.
    sample();
    drawSignals();
    drawReadouts();
    paintAll();
    sendState();
    return true;
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
       stateText() has and mirrors that one mapping, the way headcal.js already
       mirrors the sector glyph vocabulary.

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

    char message[128];
    snprintf(message, sizeof(message),
             "broadcast dtState,%d,%d,%d,%lu,%lu,%d,%s,%s\r\n",
             _active ? 1 : 0, _cylinder, rpmTenths,
             (unsigned long)_idxEdges, (unsigned long)_stepPulses,
             _sticky ? 1 : 0, levels, raws);
    _esp->print(message);
}

void XCopyDriveToolkit::sendClosed()
{
    if (_esp == nullptr)
        return;
    _esp->print("broadcast dtState,0,-1,0,0,0,0,00000000000,00000000000\r\n");
}
