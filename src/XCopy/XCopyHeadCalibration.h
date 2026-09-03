#ifndef XCOPYHEADCALIBRATION_H
#define XCOPYHEADCALIBRATION_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyGeometry.h"
#include "XCopyAudio.h"
#include "XCopyGraphics.h"
#include "XCopyFloppy.h"
#include "XCopyESP8266.h"

/*
   Continuous head calibration, after Amiga Test Kit's test of the same name.

   The drive reads one cylinder of a known good AmigaDOS disk over and over while
   the operator adjusts the head, and every pass says how many of the eleven
   sectors came back clean and - the part that makes it a calibration tool rather
   than a read test - whether the ones that did not came from the cylinder either
   side. A head sitting between two cylinders reads a mixture, and the mixture is
   what tells you which way to turn the screw.

   One session object drives all three interfaces. The TFT, the serial console and
   the browser are three renderings of this state, never three copies of it, so a
   cylinder changed from the joystick is a cylinder changed everywhere.

   It is deliberately built as a phase machine polled from the main loop rather
   than a loop of its own. Its predecessor here ran "while (!_cancelOperation)",
   which starved the serial and ESP pumps for the whole session and is the reason
   that feature could never be anything but TFT only.
*/
class XCopyHeadCalibration
{
public:
    //! Which head(s) a pass reads.
    enum class HeadSel : uint8_t
    {
        lower = 0,
        upper = 1,
        both = 2
    };

    //! The TFT's focused control. The console and the browser address the
    //! settings directly and never use this.
    enum class Field : uint8_t
    {
        cylinder = 0,
        step,
        head,
        autoReseek,
        sound,
        paused,
        count
    };

    void begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp,
               XCopyFloppy *floppy, uint8_t cylinder = kDefaultCylinder);
    //! Stops the drive and detaches the index interrupt. Must be reached on every
    //! exit path - see XCopy::exitHeadCalibration(), which is the only caller.
    void end();
    bool active() const { return _active; }

    //! One bounded unit of work plus whatever redraw it earned. At most one
    //! side's capture happens per call, so the main loop keeps servicing the
    //! joystick, the console and the ESP link between them.
    void update();

    // Controls. Every interface calls these and only these.
    void setCylinder(int cylinder);
    void nudgeCylinder(int delta);
    void setHead(HeadSel head);
    void cycleHead();
    void setAutoReseek(bool on);
    void toggleAutoReseek();
    void setSound(bool on);
    void toggleSound();
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

    // TFT only.
    void nextField();
    void adjustField(int direction);

    //! A keystroke from the USB console or the browser's terminal. Returns false
    //! if the key asked to leave, which the caller turns into an exit.
    bool handleKey(char key);

    // State, for anyone that needs to render it.
    uint8_t cylinder() const { return _cylinder; }
    uint8_t stepSize() const { return _stepSize; }
    HeadSel head() const { return _head; }
    bool autoReseek() const { return _autoReseek; }
    bool sound() const { return _sound; }
    bool paused() const { return _paused; }
    Field field() const { return _field; }
    uint32_t passes() const { return _passes; }

    //! Repaint the whole TFT screen. Called once on entry.
    void drawStatic();
    //! Push every setting to the browser, so its controls cannot disagree with us.
    void sendConfig();
    //! Tell the browser the session has closed.
    void sendClosed();

    /**
     * @brief Draw the console panel and take ownership of the terminal.
     *
     * Same table the help screen and the disk map are drawn with, updated in
     * place by cursor addressing rather than reprinted. A calibration session
     * runs for as long as somebody is turning a screw, and a scrolling log of
     * near identical lines is far harder to read a change out of than a fixed
     * panel with one field moving.
     *
     * As with XCopyTrackMap, nothing else may print to Serial while this is up
     * or the table scrolls out from under the cursor moves. The raw key hook
     * takes the console for the duration, so nothing echoes.
     */
    void panelBegin();
    //! Park the cursor clear of the table so a prompt lands below it.
    void panelEnd();

    //! '.' okay  'X' missing  '-' cyl-low  '+' cyl-high  'h' wrong side  'c' bad checksum
    static char glyph(uint8_t verdict);

    //! What ATK opens on: wherever the head already is, which after a boot seek is
    //! cylinder 0. Costs no seek to show the first pass.
    static const uint8_t kDefaultCylinder = 0;
    static const uint8_t kMaxCylinder = MAX_CYLINDERS - 1;

private:
    //! Where a pass has got to. Only ever advances one step per update().
    enum class Phase : uint8_t
    {
        settle,
        readLower,
        readUpper
    };

    void readSide(uint8_t side);
    void publish();
    void drawSignals();
    void drawSettings();
    void drawResults();
    void drawStatusLine();
    //! The pass counter, the spinner and the speed. Its own painter because a
    //! paused session still has a speed to show, but no new pass to show it with.
    void drawPassLine();
    void sendResults();
    void settingsChanged();
    //! One short sample per pass describing how it went, if sound is on.
    void playFeedback();
    const char *headName() const;

    //! The one status sentence, so the TFT, the console panel and anything else
    //! that shows it cannot end up saying different things about the same pass.
    //! Returns the ANSI colour; @p tftColour takes the matching TFT one.
    const char *statusText(char *out, size_t size, uint16_t &tftColour) const;

    //! Reads the six drive lines into @p out, in SIGNAL_LABEL order.
    void readSignals(bool *out) const;

    /*
       Console panel, drawn at exactly XCopyTrackMap's geometry.

       Deliberately the same table the disk map and the help screen are drawn
       with, down to the column widths, so the tally grid at the bottom is
       recognisably the same picture of a disk that every other operation paints.

       Drawn once by panelBegin() and then only ever updated in place, so the
       frame stays put and just the fields move. The line numbers are counted from
       the top border and have to agree with what panelBegin() actually prints,
       which is why they live here beside it rather than spread through the
       painters.
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

    // panelBegin() prints, in order: top border, two text rows, a rule, three
    // settings rows, the signals row, a rule, two head rows, the status row, a
    // rule, the sector legend, three key rows, a rule, the tally caption, a join,
    // two header rows, a join, ROWS grid rows, a join, the tally legend and the
    // bottom border.
    static const uint8_t LINE_SETTINGS = 4;
    static const uint8_t LINE_SIGNALS = 7;
    static const uint8_t LINE_HEAD = 9;
    static const uint8_t LINE_STATUS = 11;
    static const uint8_t LINE_TALLY = 23;                //!< first row of cylinders
    //! Grid rows, then a join, the tally legend and the bottom border.
    static const uint8_t LINES = LINE_TALLY + ROWS + 3;

    // drawing primitives, same shapes XCopyTrackMap uses
    static void repeat(char character, uint8_t count);
    static void solidRow(char left, char right);
    static void joinRow();
    static void textRow(const char *text);
    static void moveTo(uint8_t line, uint8_t column);
    static void restore();
    static char columnLabel(uint8_t col);

    void paintTextRow(uint8_t line, const char *colour, const char *text);
    void paintSettings();
    void paintSignals();
    void paintHead(uint8_t side);
    void paintStatus();
    //! Repaint one cylinder of the accumulated grid, both the glyph and the
    //! marker showing where the head currently is.
    void paintTallyCell(uint8_t cylinder, uint8_t side);
    void paintTallyCylinder(uint8_t cylinder);

    //! True between panelBegin() and panelEnd(), while the table owns the console.
    bool _panelActive = false;

    XCopyGraphics *_graphics = nullptr;
    XCopyAudio *_audio = nullptr;
    XCopyESP8266 *_esp = nullptr;
    XCopyFloppy *_floppy = nullptr;

    bool _active = false;
    uint8_t _cylinder = kDefaultCylinder;
    uint8_t _stepSize = 1;
    HeadSel _head = HeadSel::both;
    bool _autoReseek = false;
    /*
       Audible feedback, off unless asked for.

       The point of it is that calibration is done with both hands on the drive
       and both eyes on the screwdriver, not on any of the three screens. A tick
       that changes as the head comes into alignment is worth more than a display
       nobody can look at.

       Off by default because it is not free: XCopyAudio::playFile() forces its
       wait flag true and blocks until the sample finishes. The samples stream
       from SerialFlash over SPI, which is the same bus the TFT is on, so playing
       one concurrently with a redraw is the conflict that hack exists to prevent.
       A pass therefore costs a sample length more while this is on.
    */
    bool _sound = false;
    /*
       Set by any of the three interfaces. The session stays open and the drive
       stays spinning; only the passes stop. See setPaused().
    */
    bool _paused = false;
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

    CalibrationResult _results[2];
    bool _resultValid[2] = {false, false};

    /*
       The accumulated half of the display.

       The sector row above is one revolution: what the head is reading right now.
       This is every cylinder the session has visited and how it behaved over all
       of them, which is the difference between a head that is misaligned and one
       that is merely marginal - a cylinder that reads clean four times out of
       five looks perfect in any single pass.

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
    //! rather than the whole grid. 0xff before the first pass.
    uint8_t _markedCylinder = 0xff;

    //! How a cylinder has behaved across the whole session.
    enum class TallyState : uint8_t
    {
        untested,     //!< never visited
        clean,        //!< every pass read every sector
        intermittent, //!< some passes failed, some did not
        failing       //!< no pass has ever read it cleanly
    };
    TallyState tallyState(uint8_t cylinder, uint8_t side) const;

    // The TFT is repainted in place, so the static furniture is drawn once and
    // these say whether the cells over it have ever been filled in.
    bool _drawnStatic = false;

    //! Gap between passes. Long enough that the joystick, the console and the ESP
    //! all get polled between captures; short enough to feel continuous.
    static const uint32_t kPassGapMs = 120;
    //! How long to wait before looking again when the drive is empty.
    static const uint32_t kNoDiskGapMs = 400;
    //! And between refreshes while paused. Nothing is being read, so this only
    //! paces the drive lines, the speed readout and the browser heartbeat.
    static const uint32_t kPausedGapMs = 250;
};

#endif // XCOPYHEADCALIBRATION_H
