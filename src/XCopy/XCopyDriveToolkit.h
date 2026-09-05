#ifndef XCOPYDRIVETOOLKIT_H
#define XCOPYDRIVETOOLKIT_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyGeometry.h"
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "XCopyFloppy.h"
#include "XCopyESP8266.h"

/*
   Bench instrument for a drive, in two layers over one session.

   The lower layer is the interface itself. A drive failure presents as one
   undifferentiated "read error": a drive that steps but never spins, a drive that
   spins but whose index sensor is dead, and a drive that is never selected in the
   first place all look identical from the outside, and each needs a different
   fix. Every line is sampled and shown, and the safe outputs are drivable one at
   a time so the drive can be provoked into saying which one it is. That layer is
   live from the moment the session opens and needs no disk.

   The upper layer is the continuous head calibration test, after Amiga Test Kit's
   of the same name. The drive reads one cylinder of a known good AmigaDOS disk
   over and over while the operator adjusts the head, and every pass says how many
   of the eleven sectors came back clean and - the part that makes it a
   calibration tool rather than a read test - whether the ones that did not came
   from the cylinder either side. A head sitting between two cylinders reads a
   mixture, and the mixture is what tells you which way to turn the screw.

   The two were built eleven commits apart as separate screens and are one thing.
   The diagnosis lives in the correlation between them: sectors missing while the
   index count has stopped is a dead index sensor, sectors missing while the index
   is fine is head alignment, and those are different faults with different fixes.
   Two screens could never show that, and the calibration screen had already grown
   a six box signal strip of its own trying to.

   WRITE DATA and WRITE ENABLE are deliberately not exposed. Nothing about
   diagnosing an index or aligning a head needs them, an asserted write gate over
   a spinning disk destroys a track in milliseconds, and leaving them out removes
   the whole interlock design problem along with the flash it would cost.

   One session object drives all three interfaces. The TFT, the serial console and
   the browser are three renderings of this state, never three copies of it, so a
   cylinder changed from the joystick is a cylinder changed everywhere. The line
   controls live on the console and the browser only: a joystick field list for
   eleven lines would cost a focus cycle and its layout code in the most expensive
   flash on the board, and the TFT spends its five inputs on the calibration
   settings, which is what somebody standing at the device with a screwdriver is
   actually adjusting.

   Built as a phase machine polled from the main loop rather than a loop of its
   own. The Test Drive both halves replaced ran a blocking inner loop that starved
   the serial and ESP pumps, which is why it could never be driven from anywhere
   but the TFT.
*/
class XCopyDriveToolkit
{
public:
    /*
       The lines shown, in display order: the six outputs, then the five inputs.

       One enum, so the strip, the console table, the browser payload and the
       sticky flags cannot drift apart about which line is which.
    */
    enum Sig : uint8_t
    {
        sigSel = 0,
        sigMot,
        sigDir,
        sigSide,
        sigDens,
        sigStep,
        sigIdx,
        sigRdata,
        sigT0,
        sigWp,
        sigChg,
        sigCount
    };

    //! The first output and the first input, so painters can rule between the two
    //! halves without hard coding an index that moves when a line is added.
    static const uint8_t kOutputCount = sigIdx;

    /*
       How a line reads, which is what picks its colour on every surface.

       Deliberately four states rather than a boolean. "Asserted" and "good" are
       both true and mean different things: one is us driving a line, the other is
       the drive answering. Painting them the same colour is how the retired Test
       Drive ended up with indicators that told you nothing.
    */
    enum class Level : uint8_t
    {
        rest = 0,  //!< idle, and that is fine
        asserted,  //!< an output we are deliberately driving
        good,      //!< an input saying what it should
        fault      //!< an input that should be doing something and is not
    };

    //! Which head(s) a calibration pass reads.
    enum class HeadSel : uint8_t
    {
        lower = 0,
        upper = 1,
        both = 2
    };

    /*
       What the read layer is doing.

       Three states rather than a running flag and a paused flag, because the TFT
       addresses it as one field and two booleans there would cost two of the six
       cells the screen has. It is also exactly what the browser's one
       Start/Pause/Resume button renders.
    */
    enum class Read : uint8_t
    {
        off = 0,  //!< not reading; the drive is only being watched
        run,      //!< taking passes
        hold      //!< paused: still spinning, still sampled, no passes taken
    };

    /*
       The TFT's focused control. The console and the browser address the settings
       directly and never use this.
    */
    enum class Field : uint8_t
    {
        cylinder = 0,
        step,
        head,
        autoReseek,
        sound,
        run,
        count
    };

    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp,
               XCopyFloppy *floppy, uint8_t cylinder = kDefaultCylinder,
               bool startReading = false);
    //! Releases the outputs, stops the drive and gives the index interrupt back.
    //! Must be reached on every exit path - see XCopy::exitDriveToolkit(), the
    //! only caller.
    void end();
    bool active() const { return _active; }

    /**
     * @brief One bounded unit of work plus whatever redraw it earned.
     *
     * Two clocks over one call. The sampler runs unconditionally on its own
     * interval so the interface stays live whether or not a read is running; the
     * reader is a layer on top of it that advances at most one side's capture per
     * visit. Neither blocks, so the main loop keeps servicing the joystick, the
     * console and the ESP link between them.
     */
    void update();

    // Line controls. Live at all times, including mid pass - see _tainted.
    void setSelect(bool on);
    void setMotor(bool on);
    void setDirection(bool inward);
    /**
     * @brief Drive the SIDE line to select head 0 or head 1.
     *
     * Named for the head it selects rather than for the level it drives.
     * XCopyFloppy::setSideFast() takes 0 or 1 and puts the line LOW for 1, and its
     * companion read back is called readSideLower() while returning true for head
     * one - so a flag called _upper here ended up meaning side zero, sitting in
     * the same class as a HeadSel::upper that means side one. One of the two
     * spellings had to go and it was not going to be the one the wire uses.
     */
    void setSideOne(bool one);
    void setDensity(bool high);
    void setSticky(bool on);
    void toggleSelect() { setSelect(!_sel); }
    void toggleMotor() { setMotor(!_mot); }
    void toggleDirection() { setDirection(!_inward); }
    void toggleSide() { setSideOne(!_sideOne); }
    void toggleDensity() { setDensity(!_high); }
    void toggleSticky() { setSticky(!_sticky); }

    //! One raw step pulse in the current direction. Moves the head whether or not
    //! the motor is running, which is exactly the point.
    void pulseStep();
    void nudgeCylinder(int delta);
    void seekCylinder(int cylinder);
    //! Step outward until TRACK 0 asserts, and adopt that as cylinder 0.
    void recalibrate();
    void clearCounters();
    //! Empty the accumulated grid without leaving the session.
    void clearTally();

    /**
     * @brief Put every output back and forget where the head is.
     *
     * The exit path calls this too, so there is one definition of "safe" rather
     * than two that can drift. The position is forgotten rather than kept because
     * nothing holds the head still once we stop driving it, and a remembered
     * cylinder that is no longer true is worse than an honest question mark.
     */
    void releaseOutputs();

    // Read layer controls.
    /**
     * @brief Start or stop taking calibration passes.
     *
     * Starting asserts select and motor, because a capture needs all three
     * together - the session itself deliberately does not, so that "does this
     * drive respond to MOTOR ON at all" stays askable. If the head position is
     * still unknown it recalibrates first: a pass that did not know which cylinder
     * it was on could not report the one thing this test exists to say.
     */
    void setReading(bool on);
    void toggleReading() { setReading(_read == Read::off); }
    void setHead(HeadSel head);
    void cycleHead();
    void setAutoReseek(bool on);
    void toggleAutoReseek() { setAutoReseek(!_autoReseek); }
    void setSound(bool on);
    void toggleSound() { setSound(!_sound); }
    /**
     * @brief Stop and restart the passes without leaving the session.
     *
     * The drive keeps spinning and the screen keeps its last pass, so this is a
     * pause rather than a stop: the operator gets a still picture to read, or a
     * quiet drive to put a screwdriver into, and picks up where they left off.
     * Resuming re-seeks, because the head has had a chance to be moved by hand
     * since the last read and a pass that assumed otherwise would lie.
     */
    void setPaused(bool on);
    void togglePause();
    void setStepSize(uint8_t size);
    //! Re-seek the current cylinder now, ATK's F1.
    void reseek();
    //! Cycle 1 / 10 / 40, the three ATK offers that matter on a stroke this short.
    void cycleStepSize(int direction);

    // TFT only.
    void nextField();
    void adjustField(int direction);

    //! A keystroke from the USB console or the browser's terminal. Returns false
    //! if the key asked to leave, which the caller turns into an exit.
    bool handleKey(char key);

    // State, for anyone rendering it.
    uint8_t cylinder() const { return _cylinder; }
    bool headKnown() const { return _headKnown; }
    bool sticky() const { return _sticky; }
    uint8_t stepSize() const { return _stepSize; }
    HeadSel head() const { return _head; }
    bool autoReseek() const { return _autoReseek; }
    bool sound() const { return _sound; }
    bool paused() const { return _read == Read::hold; }
    bool reading() const { return _read != Read::off; }
    Field field() const { return _field; }
    uint32_t passes() const { return _passes; }

    //! Repaint the whole TFT screen. Called once on entry.
    void drawStatic();

    /**
     * @brief Draw the console panel and take ownership of the terminal.
     *
     * Same table the help screen and the disk map are drawn with, updated in place
     * by cursor addressing rather than reprinted. A drive is debugged over minutes
     * of poking at it and a calibration runs for as long as somebody is turning a
     * screw, and a scrolling log of near identical lines is far harder to read a
     * change out of than a fixed panel with one field moving.
     *
     * As with XCopyTrackMap, nothing else may print to Serial while this is up or
     * the table scrolls out from under the cursor moves. The raw key hook takes
     * the console for the duration, so nothing echoes.
     */
    void panelBegin();
    //! Park the cursor clear of the table so a prompt lands below it.
    void panelEnd();

    //! Push the sampled interface state to the browser, which owns none of its own.
    void sendState();
    //! Push the read layer's settings, which change far less often than the lines.
    void sendCalConfig();
    //! Tell the browser the session has closed.
    void sendClosed();

    //! '.' okay  'X' missing  '-' cyl-low  '+' cyl-high  'h' wrong side  'c' bad checksum
    static char glyph(uint8_t verdict);

    //! What ATK opens on: wherever the head already is, which after a boot seek is
    //! cylinder 0. Costs no seek to show the first pass.
    static const uint8_t kDefaultCylinder = 0;
    static const uint8_t kMaxCylinder = MAX_CYLINDERS - 1;

private:
    /*
       Where a calibration pass has got to. Only ever advances one step per update().

       spinup exists because calibrationRead() opens with motorOn(), which blocks
       for motorSpinupMs - 600ms - whenever the motor flag is false. With the line
       controls live during a read, an operator releasing MOTOR mid pass would
       otherwise stall the main loop for that long on the very next capture. This
       takes the wait itself, on the phase clock, so the ESP and serial pumps keep
       running through it and motorOn() finds the flag already set.
    */
    enum class Phase : uint8_t
    {
        spinup,
        settle,
        readLower,
        readUpper
    };

    void sample();
    //! Fixed text for a line's current reading. Derived from the sampled booleans
    //! rather than re-read, so the three surfaces cannot disagree.
    const char *stateText(uint8_t sig) const;
    //! The count or rate column, empty for lines that have neither.
    void valueText(uint8_t sig, char *out, size_t length) const;

    void readSide(uint8_t side);
    void publish();
    void settingsChanged();
    /**
     * @brief Any line control, taken at any time.
     *
     * Marks the pass in flight as driven and forces the next one through seek0():
     * a raw step pulse moves the head under the read, so a pass that trusted
     * _currentTrack after one would be reading somewhere else and saying so
     * confidently.
     */
    void lineChanged();
    //! One short sample per pass describing how it went, if sound is on.
    void playFeedback();
    const char *headName() const;
    const char *readName() const;

    /**
     * @brief The one status sentence, so no two surfaces describe the same drive
     * differently.
     *
     * Returns the console colour; @p tftColour takes the TFT one. The interface
     * has the first word: a drive that is not spinning has nothing to say about
     * head alignment, and reporting a failed pass over a released interface as a
     * calibration result would invent the diagnostic the operator is about to act
     * on.
     */
    const char *statusText(char *out, size_t length, uint16_t &tftColour) const;

    // TFT
    void drawSignals();
    void drawSettings();
    void drawResults();
    void drawStatusLine();
    //! The pass counter, the spinner and the speed. Its own painter because a
    //! paused session still has a speed to show, but no new pass to show it with.
    void drawPassLine();

    // console
    void paintSettings();
    void paintSignals();
    void paintHead(uint8_t side);
    void paintStatus();
    //! Repaint one cylinder of the accumulated grid, both the glyph and the
    //! marker showing where the head currently is.
    void paintTallyCell(uint8_t cylinder, uint8_t side);
    void paintTallyCylinder(int cylinder);
    void paintTallyGrid();

    static void repeat(char character, uint8_t count);
    static void solidRow(char left, char right);
    static void joinRow();
    static void textRow(const char *text);
    static void moveTo(uint8_t line, uint8_t column);
    static void restore();
    static char columnLabel(uint8_t col);
    void paintTextRow(uint8_t line, const char *colour, const char *text);

    XCopyGraphics *_graphics = nullptr;
    XCopyAudio *_audio = nullptr;
    XCopyESP8266 *_esp = nullptr;
    XCopyFloppy *_floppy = nullptr;

    bool _active = false;
    //! True between panelBegin() and panelEnd(), while the table owns the console.
    bool _panelActive = false;
    // The TFT is repainted in place, so the static furniture is drawn once and
    // this says whether the cells over it have ever been filled in.
    bool _drawnStatic = false;

    // commanded output state
    bool _sel = false;
    bool _mot = false;
    bool _inward = false;
    bool _sideOne = false;
    bool _high = true;

    /*
       The commanded cylinder, and whether the head has actually been found.

       Nothing holds the head in place before a session opens, so the position is
       not known until a recalibration or a completed capture puts it somewhere
       real. Showing a confident 0 on entry would be exactly the kind of indicator
       that is not really sampled - so the number is always here for the read layer
       and the tally to index by, and the flag is what says whether to believe it.
       Every surface prints "?" while it is false.
    */
    uint8_t _cylinder = kDefaultCylinder;
    bool _headKnown = false;
    uint32_t _stepPulses = 0;

    /*
       Sticky latch, off by default.

       A line that pulses once between two refreshes is invisible to a sampled
       display, and a single index edge from a drive that is otherwise dead is the
       most interesting thing that can happen during a bring up. With this on, a
       line that has moved at all since the last clear stays marked.
    */
    bool _sticky = false;
    bool _moved[sigCount];
    bool _previous[sigCount];

    // sampled state, one pass per refresh so every surface agrees
    bool _raw[sigCount];
    Level _level[sigCount];
    float _rpm = 0.0f;
    uint32_t _idxEdges = 0;
    bool _rdataActive = false;

    uint32_t _nextSampleMs = 0;
    //! Long enough that the console, the ESP pump and the joystick all get
    //! serviced between samples; short enough that a line being toggled by hand
    //! looks immediate.
    static const uint32_t kSampleMs = 150;

    // read layer
    Read _read = Read::off;
    uint8_t _stepSize = 1;
    HeadSel _head = HeadSel::both;
    bool _autoReseek = false;
    /*
       Audible feedback, off unless asked for.

       The point of it is that calibration is done with both hands on the drive and
       both eyes on the screwdriver, not on any of the three screens. A tick that
       changes as the head comes into alignment is worth more than a display nobody
       can look at.

       Off by default because it is not free: XCopyAudio::playFile() forces its
       wait flag true and blocks until the sample finishes. The samples stream from
       SerialFlash over SPI, which is the same bus the TFT is on, so playing one
       concurrently with a redraw is the conflict that hack exists to prevent. A
       pass therefore costs a sample length more while this is on.
    */
    bool _sound = false;
    Field _field = Field::cylinder;

    Phase _phase = Phase::settle;
    uint32_t _nextStepMs = 0;
    uint32_t _passes = 0;
    uint8_t _spinner = 0;
    bool _diskPresent = false;
    /*
       Consumed by the first read of the next pass. Set whenever the cylinder or
       head changes, and every pass while auto re-seek is on, so the head is put
       back deliberately rather than being left where the last read happened to
       leave it.
    */
    bool _recalPending = true;

    /*
       Set by any line control taken while a pass is in flight.

       The line controls stay live during a read on purpose: provoking the drive
       mid pass is a real diagnostic, and locking them out would take away the
       thing the lower layer exists for. But a pass taken with select dropped
       halfway through is not a measurement of head alignment, and tallying it as
       one would invent exactly the fault the operator is about to chase.

       So the pass still shows what it read - those glyphs are real, the bytes
       really came off the disk - and the accumulated grid is not touched at all.
       Not a pass, not an error: incrementing errors would call a finger a head
       fault, and incrementing passes alone would call it a clean read.

       Cleared at the top of the pass, so a line touched anywhere between the start
       of one and its end taints both of its sides. motorOn() inside readSide(0)
       fights a release done during readSide(1) just as much.
    */
    bool _tainted = false;
    bool _resultTainted[2] = {false, false};
    //! How many passes the session has thrown away this way, for the pass line.
    uint16_t _taintedPasses = 0;

    CalibrationResult _results[2];
    bool _resultValid[2] = {false, false};

    /*
       The accumulated half of the display.

       The sector row above is one revolution: what the head is reading right now.
       This is every cylinder the session has visited and how it behaved over all
       of them, which is the difference between a head that is misaligned and one
       that is merely marginal - a cylinder that reads clean four times out of five
       looks perfect in any single pass.

       Two bytes a track, 336 for the disk. That is real on a part with a few KB
       free, and it is why the counts saturate at 255 rather than widening: the
       four states below are what gets rendered, and none of them needs more.
    */
    struct TrackTally
    {
        uint8_t passes;
        uint8_t errors;
    };
    TrackTally _tally[MAX_TRACKS];

    //! Where the head marker currently sits, so moving it repaints two cells
    //! rather than the whole grid. -1 before the head has been found.
    int _markedCylinder = -1;

    //! How a cylinder has behaved across the whole session.
    enum class TallyState : uint8_t
    {
        untested,     //!< never visited
        clean,        //!< every pass read every sector
        intermittent, //!< some passes failed, some did not
        failing       //!< no pass has ever read it cleanly
    };
    TallyState tallyState(uint8_t cylinder, uint8_t side) const;

    //! Gap between passes. Long enough that the joystick, the console and the ESP
    //! all get polled between captures; short enough to feel continuous.
    static const uint32_t kPassGapMs = 120;
    //! How long to wait before looking again when the drive is empty.
    static const uint32_t kNoDiskGapMs = 400;
    //! And between checks while paused. Nothing is being read; the sampler is what
    //! keeps the surfaces alive, so this only paces the phase machine itself.
    static const uint32_t kPausedGapMs = 250;

    /*
       Console panel, drawn at exactly XCopyTrackMap's geometry.

       Deliberately the same table the disk map and the help screen are drawn with,
       down to the column widths, so the tally grid at the bottom is recognisably
       the same picture of a disk that every other operation paints.

       Drawn once by panelBegin() and then only ever updated in place, so the frame
       stays put and just the fields move. The line numbers are counted from the
       top border and have to agree with what panelBegin() actually prints, which
       is why they live here beside it rather than spread through the painters.
    */
    static const uint8_t COLS = GRID_COLS;               //!< cylinders per grid row
    static const uint8_t ROWS = GRID_ROWS;
    static const uint8_t LABELWIDTH = 6;
    static const uint8_t SIDEWIDTH = (COLS * 2) + 1;     //!< one side of the grid
    static const uint8_t INNER = LABELWIDTH + 1 + SIDEWIDTH + 1 + SIDEWIDTH;
    static const uint8_t TEXT = INNER - 2;               //!< a full width text row

    // The realtime sector row is a plain text row rather than a grid row: eleven
    // sectors on a two character pitch is 21 columns, which is the whole of a side
    // column, leaving nowhere for the count to sit.
    static const uint8_t HEADPREFIX = 10;                //!< "0 (Lower) "
    static const uint8_t GLYPHFIELD = 21;                //!< 11 sectors, two apart
    static const uint8_t RESULTFIELD = TEXT - HEADPREFIX - GLYPHFIELD - 1;

    /*
       panelBegin() prints, in order: top border, two text rows, a rule, three
       settings rows, a rule, two signal name rows and their numbers, a rule, two
       head rows, the status row, a rule, the sector legend, five key rows, a rule,
       the tally caption, a join, two header rows, a join, ROWS grid rows, a join,
       the tally legend and the bottom border.

       The eleven line Signal / Pin / Dir / State / Count table is a web only
       rendering. Stacking it whole under the calibration panel came to forty five
       rows, taller than the terminal most of this is read in. Colour already
       carries each line's verdict, so two rows of names coloured exactly as
       paintSignal() coloured them say the same thing, and one row of numbers
       carries what colour cannot.
    */
    static const uint8_t LINE_SETTINGS = 4;
    static const uint8_t LINE_SIG_OUT = 8;
    static const uint8_t LINE_SIG_IN = 9;
    static const uint8_t LINE_SIG_NUM = 10;
    static const uint8_t LINE_HEAD = 12;
    static const uint8_t LINE_STATUS = 14;
    static const uint8_t LINE_TALLY = 28;                //!< first row of cylinders
    //! Grid rows, then a join, the tally legend and the bottom border.
    static const uint8_t LINES = LINE_TALLY + ROWS + 3;
};

#endif // XCOPYDRIVETOOLKIT_H
