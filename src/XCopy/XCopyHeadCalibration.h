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
    Field field() const { return _field; }
    uint32_t passes() const { return _passes; }

    //! Repaint the whole TFT screen. Called once on entry.
    void drawStatic();
    //! Push every setting to the browser, so its controls cannot disagree with us.
    void sendConfig();
    //! Tell the browser the session has closed.
    void sendClosed();
    //! The ATK style preamble, printed once to the serial console on entry.
    void printBanner();

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
    void printResults();
    void sendResults();
    void settingsChanged();
    const char *headName() const;

    XCopyGraphics *_graphics = nullptr;
    XCopyAudio *_audio = nullptr;
    XCopyESP8266 *_esp = nullptr;
    XCopyFloppy *_floppy = nullptr;

    bool _active = false;
    uint8_t _cylinder = kDefaultCylinder;
    uint8_t _stepSize = 1;
    HeadSel _head = HeadSel::both;
    bool _autoReseek = false;
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
    //! Set when something worth telling the operator about changed.
    bool _dirty = true;
    //! Passes since the console last printed, so a stable drive still shows a
    //! sign of life without scrolling.
    uint8_t _sinceConsole = 0;

    // The TFT is repainted in place, so the static furniture is drawn once and
    // these say whether the cells over it have ever been filled in.
    bool _drawnStatic = false;

    //! Gap between passes. Long enough that the joystick, the console and the ESP
    //! all get polled between captures; short enough to feel continuous.
    static const uint32_t kPassGapMs = 120;
    //! How long to wait before looking again when the drive is empty.
    static const uint32_t kNoDiskGapMs = 400;
    //! Console heartbeat, in passes, when nothing has changed.
    static const uint8_t kConsoleHeartbeat = 8;
};

#endif // XCOPYHEADCALIBRATION_H
