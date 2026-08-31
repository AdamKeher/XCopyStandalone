#include "XCopyTrackMap.h"

// Colours are deliberately paired one to one with the TFT block colours so the
// console and the screen describe the same disk in the same language.
#define MAP_RESET   "\033[0m"
#define MAP_GREY    "\033[0;90m"
#define MAP_WHITE   "\033[1;37m"
#define MAP_GREEN   "\033[0;32m"
#define MAP_YELLOW  "\033[0;33m"
#define MAP_ORANGE  "\033[0;93m"
#define MAP_RED     "\033[1;31m"
#define MAP_MAGENTA "\033[0;35m"
#define MAP_DARKRED "\033[0;31m"
#define MAP_CYAN    "\033[0;36m"

char XCopyTrackMap::glyph(XCopyTrackState state, uint8_t attempt)
{
    switch (state)
    {
    case trackBusy:        return '>';
    case trackOK:          return '#';
    case trackWeak:        return '+';
    case trackRetried:     return '*';
    // A failing track shows which attempt it is on, so a disk that is merely
    // marginal reads differently to one that is dead.
    case trackError:       return (attempt > 0 && attempt < 10) ? (char)('0' + attempt) : 'X';
    case trackVerifyError: return '!';
    case trackSkipped:     return '-';
    default:               return '.';
    }
}

const char *XCopyTrackMap::colour(XCopyTrackState state)
{
    switch (state)
    {
    case trackBusy:        return MAP_WHITE;
    case trackOK:          return MAP_GREEN;
    case trackWeak:        return MAP_YELLOW;
    case trackRetried:     return MAP_ORANGE;
    case trackError:       return MAP_RED;
    case trackVerifyError: return MAP_MAGENTA;
    case trackSkipped:     return MAP_DARKRED;
    default:               return MAP_GREY;
    }
}

const char *XCopyTrackMap::name(XCopyTrackState state)
{
    switch (state)
    {
    case trackBusy:        return "active";
    case trackOK:          return "ok";
    case trackWeak:        return "weak";
    case trackRetried:     return "retried";
    case trackError:       return "error";
    case trackVerifyError: return "verify";
    case trackSkipped:     return "skipped";
    default:               return "pending";
    }
}

void XCopyTrackMap::repeat(char character, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) Serial.write(character);
}

void XCopyTrackMap::solidRow(char left, char right)
{
    Serial.write(left);
    repeat('-', INNER);
    Serial.write(right);
    Serial.print("\r\n");
}

void XCopyTrackMap::joinRow()
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

void XCopyTrackMap::textRow(const char *text)
{
    char buffer[TEXT + 1];
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)TEXT, (int)TEXT, text);
    Serial.print("| ");
    Serial.print(buffer);
    Serial.print(" |\r\n");
}

void XCopyTrackMap::gridRow(uint8_t row)
{
    // Sized well past the six characters this produces: the compiler cannot see
    // that row * COLS never exceeds two digits and warns on an exact fit.
    char label[16];
    snprintf(label, sizeof(label), "  %02u  ", (unsigned)(row * COLS));

    Serial.write('|');
    Serial.print(label);
    for (uint8_t side = 0; side < 2; side++)
    {
        Serial.print("| ");
        Serial.print(MAP_GREY);
        for (uint8_t col = 0; col < COLS; col++)
        {
            if (col > 0) Serial.write(' ');
            Serial.write(glyph(trackPending, 0));
        }
        Serial.print(MAP_RESET);
        Serial.write(' ');
    }
    Serial.print("|\r\n");
}

/**
 * @brief Park the cursor on a table cell.
 *
 * Moves are relative (cursor up) so they survive the terminal scrolling when the
 * table is printed at the bottom of the window; the column is absolute.
 *
 * @param line line number within the table, 0 being the top border
 * @param column 1 based terminal column
 */
void XCopyTrackMap::moveTo(uint8_t line, uint8_t column)
{
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "\033[s\033[%uA\033[%uG", (unsigned)(LINES - line), (unsigned)column);
    Serial.print(buffer);
}

void XCopyTrackMap::restore()
{
    Serial.print("\033[u");
}

void XCopyTrackMap::paintCell(uint8_t logicalTrack, XCopyTrackState state, uint8_t attempt)
{
    const uint8_t cylinder = logicalTrack / 2;
    const uint8_t side = logicalTrack % 2;

    // Column 1 is the left border, columns 2 to LABELWIDTH + 1 are the label and
    // column LABELWIDTH + 2 is its separator, so the first cell of side 0 sits at
    // LABELWIDTH + 4 - one past the separator and the pad space that follows it.
    const uint8_t column = (side == 0 ? LABELWIDTH + 4 : LABELWIDTH + SIDEWIDTH + 5) + ((cylinder % COLS) * 2);

    moveTo(LINE_GRID + (cylinder / COLS), column);
    Serial.print(colour(state));
    Serial.write(glyph(state, attempt));
    Serial.print(MAP_RESET);
    restore();
}

void XCopyTrackMap::statusRow(const char *textColour, const char *text)
{
    char buffer[TEXT + 1];
    snprintf(buffer, sizeof(buffer), "%-*.*s", (int)TEXT, (int)TEXT, text);

    moveTo(LINE_STATUS, 3);
    Serial.print(textColour);
    Serial.print(buffer);
    Serial.print(MAP_RESET);
    restore();
}

/**
 * @brief Draw the empty map and take ownership of the console.
 *
 * @param operation name of the operation, shown in the header
 * @param diskName volume or file name the operation is working on
 */
void XCopyTrackMap::begin(const String &operation, const String &diskName)
{
    char buffer[TEXT + 1];

    _active = true;

    Serial.print("\r\n");

    solidRow('.', '.');

    snprintf(buffer, sizeof(buffer), "OPERATION : %s", operation.c_str());
    textRow(buffer);
    snprintf(buffer, sizeof(buffer), "DISK      : %s", diskName.length() ? diskName.c_str() : "(unnamed)");
    textRow(buffer);

    joinRow();

    Serial.print("| TRK  | SIDE 0              | SIDE 1              |\r\n");
    Serial.print("|      | 0 1 2 3 4 5 6 7 8 9 | 0 1 2 3 4 5 6 7 8 9 |\r\n");

    joinRow();

    for (uint8_t row = 0; row < ROWS; row++) gridRow(row);

    joinRow();

    textRow("");

    solidRow('|', '|');

    // Legend. The visible width is accumulated alongside the coloured output so
    // the colour escapes do not throw the padding out.
    static const XCopyTrackState legend[] = { trackPending, trackBusy, trackOK, trackWeak,
                                              trackRetried, trackError, trackVerifyError, trackSkipped };
    uint8_t visible = 0;
    Serial.print("| ");
    for (uint8_t i = 0; i < sizeof(legend) / sizeof(legend[0]); i++)
    {
        const uint8_t width = 4 + strlen(name(legend[i])); // glyph, space, name, two trailing spaces
        if (visible + width > TEXT)
        {
            repeat(' ', TEXT - visible);
            Serial.print(" |\r\n| ");
            visible = 0;
        }

        Serial.print(colour(legend[i]));
        Serial.write(glyph(legend[i], 0));
        Serial.print(MAP_RESET);
        Serial.write(' ');
        Serial.print(name(legend[i]));
        Serial.print("  ");
        visible += width;
    }
    repeat(' ', TEXT - visible);
    Serial.print(" |\r\n");

    solidRow('`', '\'');

    statusRow(MAP_CYAN, "waiting for drive");
}

/**
 * @brief Repaint one track and describe it on the status line.
 *
 * @param logicalTrack logical track number, 0 - 159
 * @param state state to paint the track in
 * @param attempt attempt number, shown in the cell while a track is failing
 * @param verify flags that this is the verify pass over the track
 */
void XCopyTrackMap::setTrack(uint8_t logicalTrack, XCopyTrackState state, uint8_t attempt, bool verify)
{
    if (!_active || logicalTrack > 159) return;

    paintCell(logicalTrack, state, attempt);

    char tail[16] = "";
    if (attempt > 0) snprintf(tail, sizeof(tail), " (try %u)", (unsigned)attempt);

    char text[TEXT + 1];
    snprintf(text, sizeof(text), "CYL %02u SIDE %u  TRK %03u  %s%s%s",
             (unsigned)(logicalTrack / 2), (unsigned)(logicalTrack % 2), (unsigned)logicalTrack,
             verify ? "VERIFY " : "", name(state), tail);
    statusRow(colour(state), text);
}

/**
 * @brief Repaint every track from fromTrack to the end of the disk.
 *
 * @param fromTrack first logical track to repaint
 * @param state state to paint the tracks in
 */
void XCopyTrackMap::setRange(uint8_t fromTrack, XCopyTrackState state)
{
    if (!_active) return;

    for (uint16_t track = fromTrack; track < 160; track++)
        paintCell((uint8_t)track, state, 0);
}

/**
 * @brief Replace the status line without touching the grid.
 *
 * @param text status text, truncated to the width of the table
 */
void XCopyTrackMap::status(const String &text)
{
    if (!_active) return;
    statusRow(MAP_CYAN, text.c_str());
}

/**
 * @brief Write a closing status and release the console.
 *
 * @param summary final status text
 */
void XCopyTrackMap::end(const String &summary)
{
    if (!_active) return;

    statusRow(MAP_CYAN, summary.c_str());
    _active = false;
    Serial.print("\r\n");
}
