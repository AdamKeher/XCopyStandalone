#ifndef XCOPYSDFAT_H
#define XCOPYSDFAT_H

#include <SdFat.h>

/*
   The one SdFat instance for the whole program.

   SdFat is not a handle to a card, it *is* the FatVolume. Two things follow:

   - FatFile::open(path, oflag) resolves against FatFile::m_cwd, a static that
     SdFat::begin() repoints at whichever instance called it last. Every bare
     SdFile / FatFile / File in this codebase therefore belongs to that instance,
     whether or not the code that opened it knows which one it is.

   - If the instance that called begin() last is a local or a heap object that is
     later freed, that static is left dangling. adf_nativ.cpp used to construct an
     SdFat on the stack and begin() it once per 512 byte sector, so a "dump" left
     m_cwd pointing into a dead stack frame for every later file open.

   Everything that touches the card goes through xcopySd(). Nothing else should
   construct an SdFat.
*/
SdFat &xcopySd();

/**
 * @brief Mount the shared card.
 *
 * Safe to call repeatedly: the card can be swapped at any time, so each entry
 * point that is about to use it re-mounts rather than assuming.
 *
 * @result true if the card initialised
 */
bool xcopySdBegin();

#endif // XCOPYSDFAT_H
