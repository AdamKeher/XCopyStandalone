#ifndef XCOPYLINEEDITOR_H
#define XCOPYLINEEDITOR_H

#include <Arduino.h>
#include "XCopyConsoleIO.h"

/**
 * @brief The line being typed, and everything that can be done to it.
 *
 * What this replaces understood three keys - a printable character, backspace and
 * Enter - and threw the arrow keys away outright, so a typo three characters back
 * meant deleting to it and there was no way to run a command twice.
 *
 * Fed one character at a time, which is what makes it work from either terminal
 * without knowing which it is: the USB console delivers ESC [ A as three separate
 * reads and the browser delivers it as one string that is split per character
 * before it gets here, so the escape sequences have to be reassembled by state
 * either way.
 *
 * Output is deliberately whole strings. XCopyLog broadcasts to the ESP and then
 * sleeps 6ms on every call, so a redraw emitted a character at a time would cost
 * a tenth of a second and a websocket frame per character. One edit is one write.
 */
class XCopyLineEditor
{
public:
    //! Enter. The line is handed over before the buffer is cleared.
    typedef void (*OnLine)(void *caller, const String &line);
    /**
     * @brief Tab.
     *
     * @param presses how many times Tab has been pressed without anything else in
     *                between, so a completer can list the candidates on the second
     *                press rather than the first.
     */
    typedef void (*OnComplete)(void *caller, uint8_t presses);

    /**
     * @param writer where the editor draws. See XCopyConsoleIO.h for why this is
     *               handed in rather than reached for.
     */
    void begin(void *caller, OnLine onLine, OnComplete onComplete, XCopyWriter writer);

    //! The terminal the editor is drawing on, for anything printing alongside it.
    void write(const String &text);
    XCopyWriter writer() const { return _write; }

    //! One key, from either terminal.
    void key(char c);

    const String &line() const { return _line; }
    uint16_t cursor() const { return _cursor; }

    /**
     * @brief Swap the characters in [start, end) for @p text.
     *
     * How a completion is applied: it knows which token it is replacing and this
     * puts the cursor at the end of what it wrote.
     */
    void replace(uint16_t start, uint16_t end, const String &text);

    //! Prompt and an empty line, for the start of a session.
    void prompt();
    //! Prompt, line and cursor, in one write. Use after printing over the line.
    void redraw();
    //! End the current line without running it, leaving what was typed on screen.
    void breakLine();

    //! Clear the buffer without printing anything.
    void reset();

private:
    // Long enough for any command in the table with a full path in it, and short
    // enough that eight of them in the history is a few hundred bytes.
    static const uint8_t kHistory = 8;

    enum class State : uint8_t
    {
        normal,
        escape, //!< ESC seen
        csi     //!< ESC [ seen, collecting parameters
    };

    String _line;
    uint16_t _cursor = 0;

    State _state = State::normal;
    String _csi;

    uint8_t _tabPresses = 0;

    String _history[kHistory];
    uint8_t _historyCount = 0; //!< how many slots are filled, up to kHistory
    uint8_t _historyHead = 0;  //!< the next slot to write
    int8_t _historyIndex = -1; //!< -1 while editing a fresh line
    String _stash;             //!< the fresh line, kept while walking back

    void *_caller = nullptr;
    OnLine _onLine = nullptr;
    OnComplete _onComplete = nullptr;
    XCopyWriter _write = nullptr;

    void insert(char c);
    void backspace();
    void deleteForward();
    void moveTo(uint16_t position);
    void submit();
    void cancel();

    void handleCsi(char final);
    void historyUp();
    void historyDown();
    void pushHistory(const String &line);
    const String &historyAt(int8_t index) const;
    void setLine(const String &text);

    static const char *promptText() { return ">> "; }
    static uint8_t promptWidth() { return 3; }
};

#endif // XCOPYLINEEDITOR_H
