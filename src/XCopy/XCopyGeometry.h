#ifndef XCOPYGEOMETRY_H
#define XCOPYGEOMETRY_H

#include <stdint.h>

/*
   How many cylinders and tracks there are, depending on who is asking.

   These used to be the literal 160 repeated through every whole-disk loop, every
   progress grid and every bounds check. That was fine while ADF was the only format:
   AmigaDOS is 80 cylinders and nothing else existed. SCP addresses 84, so the two
   numbers had to stop being the same number.

   A "logical track" is cylinder * 2 + side, contiguous across both sides - the same
   numbering XCopyFloppy::gotoLogicTrack(), XCopyTrackMap and the web grid all use, and
   the same one SCP puts in a track data header.
*/

//! AmigaDOS geometry. Every ADF operation is exactly this and cannot be anything else.
static const uint8_t ADF_CYLINDERS = 80;
static const uint8_t ADF_TRACKS = ADF_CYLINDERS * 2; // 160

/*
   The SCP addressing limit, and therefore what the progress grids are sized for.

   Most drives will seek past 80 - the extra cylinders are where long-track and
   out-of-band protections hide - but few reach 84 and none are obliged to. Capture
   defaults to ADF_CYLINDERS and only goes further when asked, so an ordinary read is
   never slowed down hunting for tracks that do not exist.
*/
static const uint8_t MAX_CYLINDERS = 84;
static const uint8_t MAX_TRACKS = MAX_CYLINDERS * 2; // 168

/*
   Progress grid layout, shared by the TFT blocks (XCopyGraphics::drawTrack), the serial
   console map (XCopyTrackMap) and the browser table (diskcopy.js).

   12 x 7 is exactly MAX_CYLINDERS, so the last row is full rather than ragged. The
   three surfaces are deliberately mirror images of each other - change one and change
   all three, or the device stops agreeing with itself about what it is doing.
*/
static const uint8_t GRID_COLS = 12;
static const uint8_t GRID_ROWS = 7;

#endif // XCOPYGEOMETRY_H
