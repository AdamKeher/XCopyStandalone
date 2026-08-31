#ifndef XCOPYTRACKMAP_H
#define XCOPYTRACKMAP_H

#include <Arduino.h>
#include "XCopyGeometry.h"

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
 * The map mirrors the block grid drawn on the TFT: MAX_CYLINDERS cylinders laid
 * out GRID_COLS to a row, side 0 on the left and side 1 on the right. Cells are
 * repainted with
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
  /*
     Grid geometry, all derived rather than tabulated. The line numbers in particular
     have to agree with what begin() actually prints or every in place update lands on
     the wrong row, and that is far too easy to get wrong by hand when the number of
     grid rows changes.
  */
  static const uint8_t COLS = GRID_COLS;  //!< cylinders per row
  static const uint8_t ROWS = GRID_ROWS;  //!< rows of cylinders
  static const uint8_t LABELWIDTH = 6;    //!< width of the track label column
  static const uint8_t SIDEWIDTH = (COLS * 2) + 1; //!< width of one side's grid column
  static const uint8_t INNER = LABELWIDTH + 1 + SIDEWIDTH + 1 + SIDEWIDTH;
  static const uint8_t TEXT = INNER - 2;  //!< INNER less the padding space either side

  // Line numbers within the table, counted from the top border. begin() prints, in
  // order: top border, two text rows, a join, two header rows, a join, ROWS grid
  // rows, a join, the status row, a solid rule, two legend rows and the bottom
  // border - which is where each of these comes from.
  static const uint8_t LINE_GRID = 7;             //!< first row of cylinder blocks
  static const uint8_t LINE_STATUS = 8 + ROWS;    //!< the single line status field
  static const uint8_t LEGEND_ROWS = 2;
  static const uint8_t LINES = 11 + ROWS + LEGEND_ROWS; //!< top to bottom border inclusive

  // drawing primitives
  static void repeat(char character, uint8_t count);
  static void solidRow(char left, char right);
  static void joinRow();
  static void textRow(const char *text);
  static void gridRow(uint8_t row);
  static void headerRows();
  //! Single character column label. Runs 0-9 then A-Z, so a wide grid still fits the
  //! two characters per cell the map is drawn on.
  static char columnLabel(uint8_t col);

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
