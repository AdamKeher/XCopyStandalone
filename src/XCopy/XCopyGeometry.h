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

   Ten to a row, so a row is a group of ten cylinders and the labels down the side read
   0, 10, 20 ... 80. That is how XCopy itself laid a disk out and how Amiga imaging tools
   have ever since, and it is the layout a fault pattern can be read off without counting.

   This was briefly 12 x 7, because that is exactly MAX_CYLINDERS and it made the last row
   full instead of ragged. It also relabelled the columns 0-B and the rows 0-6, which
   matches nothing anyone recognises. The ragged row is the point rather than a defect:
   cylinders 80 to 83 are the out of band ones SCP can reach and AmigaDOS cannot, and a
   short last row is what says so at a glance.

   MAX_CYLINDERS is not a multiple of GRID_COLS, so every surface has to leave cylinders
   >= MAX_CYLINDERS blank rather than drawing a cell there. The three surfaces are mirror
   images of each other - change one and change all three, or the device stops agreeing
   with itself about what it is doing.
*/
static const uint8_t GRID_COLS = 10;
//! Rounded up, so the last row is short. See above - that is deliberate.
static const uint8_t GRID_ROWS = (MAX_CYLINDERS + GRID_COLS - 1) / GRID_COLS; // 9

#endif // XCOPYGEOMETRY_H
