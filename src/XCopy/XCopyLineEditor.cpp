#include "XCopyLineEditor.h"

void XCopyLineEditor::begin(void *caller, OnLine onLine, OnComplete onComplete, XCopyWriter writer)
{
    _caller = caller;
    _onLine = onLine;
    _onComplete = onComplete;
    _write = writer;
}

void XCopyLineEditor::write(const String &text)
{
    if (_write != nullptr)
        _write(text);
}

void XCopyLineEditor::reset()
{
    _line = "";
    _cursor = 0;
    _state = State::normal;
    _csi = "";
    _tabPresses = 0;
    _historyIndex = -1;
    _stash = "";
}

void XCopyLineEditor::prompt()
{
    write(promptText());
}

void XCopyLineEditor::redraw()
{
    /*
       One write, and one websocket frame. Carriage return to the left margin, the
       prompt and the whole line, erase to end of line for whatever the line used
       to be longer by, then put the cursor where it belongs. Columns are 1 based.
    */
    String out;
    out.reserve(_line.length() + 24);
    out += '\r';
    out += promptText();
    out += _line;
    out += "\033[K\033[";
    out += String(promptWidth() + _cursor + 1);
    out += 'G';

    write(out);
}

void XCopyLineEditor::breakLine()
{
    write("\r\n");
    reset();
}

void XCopyLineEditor::key(char c)
{
    // Tab is the one key whose meaning depends on the key before it, so the count
    // is cleared here for everything else rather than in eight places below.
    if (c != 0x09)
        _tabPresses = 0;

    switch (_state)
    {
    case State::escape:
        // Only CSI sequences are of interest. Anything else - an Alt-key, a lone
        // Escape - is dropped rather than being typed into the line.
        _state = (c == '[') ? State::csi : State::normal;
        _csi = "";
        return;

    case State::csi:
        // Parameters first, then one final byte in 0x40..0x7e ends the sequence.
        if (c >= 0x20 && c <= 0x3f)
        {
            _csi += c;
            return;
        }
        _state = State::normal;
        handleCsi(c);
        return;

    default:
        break;
    }

    switch (c)
    {
    case 0x1b: // ESC
        _state = State::escape;
        _csi = "";
        return;

    case 0x09: // Tab
        if (_tabPresses < 255)
            _tabPresses++;
        if (_onComplete != nullptr)
            _onComplete(_caller, _tabPresses);
        return;

    case 0x03: // Ctrl-C
        cancel();
        return;

    case 0x01: // Ctrl-A, start of line - the reflex on any unix console
        moveTo(0);
        return;

    case 0x05: // Ctrl-E, end of line
        moveTo((uint16_t)_line.length());
        return;

    case 0x08: // Backspace
    case 0x7f: // DEL, which is what most real terminals send for backspace
        backspace();
        return;

    case 0x0d:
    case 0x0a:
        submit();
        return;

    default:
        break;
    }

    // Printable only. A stray control character used to be appended to the line
    // and echoed, which left the terminal and the buffer disagreeing about what
    // was on screen.
    if (c >= 0x20)
        insert(c);
}

void XCopyLineEditor::handleCsi(char final)
{
    switch (final)
    {
    case 'A':
        historyUp();
        return;
    case 'B':
        historyDown();
        return;
    case 'C':
        if (_cursor < _line.length())
            moveTo(_cursor + 1);
        return;
    case 'D':
        if (_cursor > 0)
            moveTo(_cursor - 1);
        return;
    case 'H':
        moveTo(0);
        return;
    case 'F':
        moveTo((uint16_t)_line.length());
        return;
    case '~':
        // The numbered forms. xterm sends these where a vt100 sends the letters
        // above, and the browser terminal is one of them.
        if (_csi == "1" || _csi == "7")
            moveTo(0);
        else if (_csi == "4" || _csi == "8")
            moveTo((uint16_t)_line.length());
        else if (_csi == "3")
            deleteForward();
        return;
    default:
        return;
    }
}

void XCopyLineEditor::insert(char c)
{
    const bool atEnd = _cursor >= _line.length();

    if (atEnd)
        _line += c;
    else
    {
        String rebuilt = _line.substring(0, _cursor);
        rebuilt += c;
        rebuilt += _line.substring(_cursor);
        _line = rebuilt;
    }
    _cursor++;

    // Typing at the end is the common case by a long way, and echoing the one
    // character is a fifth of the bytes a redraw would send.
    if (atEnd)
        write(String(c));
    else
        redraw();
}

void XCopyLineEditor::backspace()
{
    if (_cursor == 0)
        return;

    const bool atEnd = _cursor >= _line.length();

    String rebuilt = _line.substring(0, _cursor - 1);
    rebuilt += _line.substring(_cursor);
    _line = rebuilt;
    _cursor--;

    if (atEnd)
        write("\033[1D \033[1D");
    else
        redraw();
}

void XCopyLineEditor::deleteForward()
{
    if (_cursor >= _line.length())
        return;

    String rebuilt = _line.substring(0, _cursor);
    rebuilt += _line.substring(_cursor + 1);
    _line = rebuilt;

    redraw();
}

void XCopyLineEditor::moveTo(uint16_t position)
{
    if (position > _line.length())
        position = (uint16_t)_line.length();
    if (position == _cursor)
        return;

    _cursor = position;

    // Just the cursor. No point redrawing text that has not changed.
    write("\033[" + String(promptWidth() + _cursor + 1) + "G");
}

void XCopyLineEditor::replace(uint16_t start, uint16_t end, const String &text)
{
    if (start > _line.length())
        start = (uint16_t)_line.length();
    if (end > _line.length())
        end = (uint16_t)_line.length();
    if (end < start)
        end = start;

    String rebuilt = _line.substring(0, start);
    rebuilt += text;
    rebuilt += _line.substring(end);

    _line = rebuilt;
    _cursor = start + (uint16_t)text.length();

    redraw();
}

void XCopyLineEditor::submit()
{
    write("\r\n");

    const String line = _line;

    // Cleared before the handler runs, not after. A command that prints - which is
    // most of them - would otherwise be printing over a line the editor still
    // believes is on screen, and the first keystroke afterwards would redraw it.
    _line = "";
    _cursor = 0;
    _historyIndex = -1;
    _stash = "";

    pushHistory(line);

    if (_onLine != nullptr)
        _onLine(_caller, line);
}

void XCopyLineEditor::cancel()
{
    // Shown rather than silently dropped, so the abandoned line stays in the
    // scrollback as a record of what was not run.
    write("^C\r\n");
    _line = "";
    _cursor = 0;
    _historyIndex = -1;
    _stash = "";
    prompt();
}

// HISTORY

void XCopyLineEditor::pushHistory(const String &line)
{
    if (line.length() == 0)
        return;

    // Holding a key down or running the same thing twice should not fill the ring
    // with copies of one command.
    if (_historyCount > 0 && historyAt(0) == line)
        return;

    _history[_historyHead] = line;
    _historyHead = (uint8_t)((_historyHead + 1) % kHistory);
    if (_historyCount < kHistory)
        _historyCount++;
}

const String &XCopyLineEditor::historyAt(int8_t index) const
{
    // 0 is the most recent. The ring is written forwards, so this walks back from
    // the slot before the head.
    const uint8_t slot = (uint8_t)((_historyHead + kHistory - 1 - (uint8_t)index) % kHistory);
    return _history[slot];
}

void XCopyLineEditor::setLine(const String &text)
{
    _line = text;
    _cursor = (uint16_t)_line.length();
    redraw();
}

void XCopyLineEditor::historyUp()
{
    if (_historyCount == 0 || _historyIndex + 1 >= (int8_t)_historyCount)
        return;

    // The half typed line is kept, so walking up and back down again returns what
    // was actually being written rather than an empty prompt.
    if (_historyIndex < 0)
        _stash = _line;

    _historyIndex++;
    setLine(historyAt(_historyIndex));
}

void XCopyLineEditor::historyDown()
{
    if (_historyIndex < 0)
        return;

    _historyIndex--;
    setLine(_historyIndex < 0 ? _stash : historyAt(_historyIndex));
}
