#ifndef XCOPYTRACKMAP_H
#define XCOPYTRACKMAP_H

#include <Arduino.h>

/**
 * State of a single track in the map. The glyph is what makes the map readable
 * on a monochrome terminal, the colour is what makes it readable at a glance.
 */
enum XCopyTrackState
{
  trackPending = 0,   //!< not reached yet
  trackBusy,          //!< seeking / transferring
  trackOK,            //!< completed first time
  trackWeak,          //!< completed, but weak bits were seen
  trackRetried,       //!< completed after one or more retries
  trackError,         //!< failed
  trackVerifyError,   //!< completed, but the verify pass did not match
  trackSkipped        //!< never attempted - the operation was cancelled
};

/**
 * @brief Draws an ASCII map of both disk sides on the serial console and
 *        updates it in place as an operation progresses.
 *
 * The map mirrors the block grid drawn on the TFT: 80 cylinders laid out ten to
 * a row, side 0 on the left and side 1 on the right. Cells are repainted with
 * ANSI cursor addressing relative to the current cursor position, so nothing
 * else may write to Serial between begin() and end() or the table will scroll
 * away from under the updates.
 *
 * Output goes to Serial only - it is never mirrored to the WebUI log, which has
 * its own track grid and would only see the escape sequences.
 */
class XCopyTrackMap
{
public:
  void begin(const String &operation, const String &diskName);
  void setTrack(uint8_t logicalTrack, XCopyTrackState state, uint8_t attempt = 0, bool verify = false);
  void setRange(uint8_t fromTrack, XCopyTrackState state);
  void status(const String &text);
  void end(const String &summary);

  //! true between begin() and end(), while the table owns the console
  bool active() const { return _active; }

private:
  // grid geometry
  static const uint8_t COLS = 10;         //!< cylinders per row
  static const uint8_t ROWS = 8;          //!< rows of cylinders
  static const uint8_t LABELWIDTH = 6;    //!< width of the track label column
  static const uint8_t SIDEWIDTH = 21;    //!< width of one side's grid column
  static const uint8_t INNER = 50;        //!< LABELWIDTH + 1 + SIDEWIDTH + 1 + SIDEWIDTH
  static const uint8_t TEXT = 48;         //!< INNER less the padding space either side

  // line numbers within the table, counted from the top border
  static const uint8_t LINE_GRID = 7;     //!< first row of cylinder blocks
  static const uint8_t LINE_STATUS = 16;  //!< the single line status field
  static const uint8_t LINES = 21;        //!< lines from top border to bottom border inclusive

  // drawing primitives
  static void repeat(char character, uint8_t count);
  static void solidRow(char left, char right);
  static void joinRow();
  static void textRow(const char *text);
  static void gridRow(uint8_t row);

  // in place updates
  static void moveTo(uint8_t line, uint8_t column);
  static void restore();
  static void paintCell(uint8_t logicalTrack, XCopyTrackState state, uint8_t attempt);
  static void statusRow(const char *textColour, const char *text);

  static char glyph(XCopyTrackState state, uint8_t attempt);
  static const char *colour(XCopyTrackState state);
  static const char *name(XCopyTrackState state);

  bool _active = false;
};

#endif // XCOPYTRACKMAP_H
