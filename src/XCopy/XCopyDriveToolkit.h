#ifndef XCOPYDRIVETOOLKIT_H
#define XCOPYDRIVETOOLKIT_H

#include <Arduino.h>
#include "XCopyGeometry.h"
#include "XCopyGraphics.h"
#include "XCopyFloppy.h"
#include "XCopyESP8266.h"

/*
   Bench instrument for an unknown drive mechanism.

   A drive failure currently presents as one undifferentiated "read error". A
   drive that steps but never spins, a drive that spins but whose index sensor is
   dead, and a drive that is never selected in the first place all look identical
   from the outside, and each needs a different fix. This makes each of them
   separately observable: every interface line sampled and shown, and the safe
   outputs drivable one at a time so the drive can be provoked into telling you
   which one it is.

   The immediate question it exists to answer is why a drive that steps correctly
   produces no index pulses. Stepping works with the spindle stopped, so a drive
   that seeks convincingly can be completely stationary - which is invisible
   unless you can assert MOTOR ON by itself and watch for edges.

   WRITE DATA and WRITE ENABLE are deliberately not exposed. Nothing about
   diagnosing an index needs them, an asserted write gate over a spinning disk
   destroys a track in milliseconds, and leaving them out removes the whole
   interlock design problem along with the flash it would cost.

   One session object drives all three interfaces. The TFT, the serial console and
   the browser are three renderings of this state, never three copies of it. The
   TFT is display only - the joystick would cost a field list and its layout code
   in the most expensive flash on the board - and the console and the browser are
   where the controls live.

   Built as a phase machine polled from the main loop rather than a loop of its
   own, like XCopyHeadCalibration and for the same reason: the predecessor to both
   ran a blocking inner loop that starved the serial and ESP pumps, which is why
   it could never be driven from anywhere but the TFT.
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

    void begin(XCopyGraphics *graphics, XCopyESP8266 *esp, XCopyFloppy *floppy);
    //! Releases the outputs and gives the index interrupt back. Must be reached on
    //! every exit path - see XCopy::exitDriveToolkit(), the only caller.
    void end();
    bool active() const { return _active; }

    //! One bounded sample plus whatever redraw it earned. Never blocks for longer
    //! than one read data burst, so the main loop keeps servicing the console and
    //! the ESP link between refreshes.
    void update();

    // Controls. Every surface calls these and only these.
    void setSelect(bool on);
    void setMotor(bool on);
    void setDirection(bool inward);
    void setSideUpper(bool upper);
    void setDensity(bool high);
    void setSticky(bool on);
    void toggleSelect() { setSelect(!_sel); }
    void toggleMotor() { setMotor(!_mot); }
    void toggleDirection() { setDirection(!_inward); }
    void toggleSide() { setSideUpper(!_upper); }
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

    /**
     * @brief Put every output back and forget where the head is.
     *
     * The exit path calls this too, so there is one definition of "safe" rather
     * than two that can drift. The position is forgotten rather than kept because
     * nothing holds the head still once we stop driving it, and a remembered
     * cylinder that is no longer true is worse than an honest question mark.
     */
    void releaseOutputs();

    //! A keystroke from the USB console or the browser terminal. False means the
    //! key asked to leave, which the caller turns into an exit.
    bool handleKey(char key);

    //! Repaint the whole TFT. Called once on entry.
    void drawStatic();

    /**
     * @brief Draw the console table and take ownership of the terminal.
     *
     * Same frame the disk map and the help screen are drawn with, updated in place
     * by cursor addressing rather than reprinted. A drive is debugged over minutes
     * of poking at it, and a scrolling log of near identical lines is far harder to
     * read a change out of than a fixed table with one field moving.
     *
     * As with XCopyTrackMap, nothing else may print to Serial while this is up or
     * the table scrolls out from under the cursor moves.
     */
    void panelBegin();
    //! Park the cursor clear of the table so a prompt lands below it.
    void panelEnd();

    //! Push the whole sampled state to the browser, which owns no state of its own.
    void sendState();
    //! Tell the browser the session has closed.
    void sendClosed();

    // State, for anyone rendering it.
    int cylinder() const { return _cylinder; }
    bool sticky() const { return _sticky; }

private:
    void sample();
    //! Fixed text for a line's current reading. Derived from the sampled booleans
    //! rather than re-read, so the three surfaces cannot disagree.
    const char *stateText(uint8_t sig) const;
    //! The count or rate column, empty for lines that have neither.
    void valueText(uint8_t sig, char *out, size_t length) const;
    static const char *label(uint8_t sig);
    static uint8_t idcPin(uint8_t sig);
    //! The one status sentence, so no two surfaces describe the same drive
    //! differently. Returns the console colour; @p tftColour takes the TFT one.
    const char *statusText(char *out, size_t length, uint16_t &tftColour) const;

    // TFT
    void drawSignals();
    void drawReadouts();

    // console
    void paintSignal(uint8_t sig);
    void paintStatus();
    void paintAll();
    static void repeat(char character, uint8_t count);
    static void solidRow(char left, char right);
    static void textRow(const char *text);
    static void moveTo(uint8_t line, uint8_t column);
    static void restore();

    XCopyGraphics *_graphics = nullptr;
    XCopyESP8266 *_esp = nullptr;
    XCopyFloppy *_floppy = nullptr;

    bool _active = false;
    bool _panelActive = false;

    // commanded output state
    bool _sel = false;
    bool _mot = false;
    bool _inward = false;
    bool _upper = true;
    bool _high = true;

    /*
       Where the head is, or -1 for "not known".

       Nothing holds the head in place before a session opens, so the toolkit has
       no idea where it is until a recalibration puts it somewhere known. Showing
       a confident 0 on entry would be exactly the kind of indicator that is not
       really sampled.
    */
    int _cylinder = -1;
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

    /*
       Console table, drawn at exactly XCopyTrackMap's geometry.

       The same frame the disk map and the help screen use, down to the column
       widths, so the toolkit reads as part of the same device rather than as some
       other program. Line numbers are counted from the top border and have to
       agree with what panelBegin() actually prints, which is why they live here
       beside it rather than spread through the painters.
    */
    static const uint8_t COLS = GRID_COLS;
    static const uint8_t LABELWIDTH = 6;
    static const uint8_t SIDEWIDTH = (COLS * 2) + 1;
    static const uint8_t INNER = LABELWIDTH + 1 + SIDEWIDTH + 1 + SIDEWIDTH;
    static const uint8_t TEXT = INNER - 2; //!< a full width text row

    // panelBegin() prints, in order: top border, two text rows, a rule, the column
    // header, sigCount signal rows, a rule, the status row, a rule, three key rows
    // and the bottom border.
    static const uint8_t LINE_HEADER = 4;
    static const uint8_t LINE_SIGNAL0 = 5;
    static const uint8_t LINE_STATUS = LINE_SIGNAL0 + sigCount + 1;
    /*
       Total rows printed, not the index of the last one.

       moveTo() walks up from where the cursor is parked, which is the line after
       the bottom border, so this has to be one more than the bottom border's
       index or every cell is addressed a line low and the table is written
       through its own frame. After the status row come a rule, three key rows and
       the bottom border - five more lines, and the count is one past that.
    */
    static const uint8_t LINES = LINE_STATUS + 6;

    //! Content offsets within a TEXT row. A row is printed as "| " plus TEXT plus
    //! " |", so a field at offset k lands on terminal column 3 + k.
    static const uint8_t COL_STATE = 3 + 21;
    static const uint8_t COL_VALUE = 3 + 34;
    static const uint8_t WIDTH_STATE = 12;
    static const uint8_t WIDTH_VALUE = 13;
};

#endif // XCOPYDRIVETOOLKIT_H
