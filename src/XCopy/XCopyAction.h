#ifndef XCOPYACTION_H
#define XCOPYACTION_H

#include <stdint.h>
#include "XCopyState.h"

/*
   What was asked for: every menu item carries one, and every command that arrives
   from the web UI turns into one.

   Scoped, so an action can never be assigned to XCopyState (or compared with one)
   by accident, and so the members that name the same thing as a state can keep the
   same name. It is not called XCopyCommand because that name belongs to the console
   command line in XCopyCommand.h.

   Only some actions start something the machine then stays in -- stateForAction()
   is the whole of that mapping. The rest are done by the time navigateSelect()
   returns, which is why they have no state.

   These values are never sent anywhere, so unlike XCopyState they are free to be
   renumbered.
*/
enum class XCopyAction : uint8_t {
  // Menu headings and blank spacers. Selecting one opens a submenu or does nothing.
  none,

  // Start an operation, and leave the machine in the matching XCopyState.
  copyADFToDisk,
  copyDiskToADF,
  copyDiskToDisk,
  copyDiskToFlash,
  copyFlashToDisk,
  testDisk,
  formatDisk,
  fluxDisk,
  scanBlocks,
  diskSearch,
  modSearch,
  testDrive,
  directorySelection,
  about,
  showTime,
  debuggingTempFile,
  debuggingSDFLash,
  debuggingEraseCopy,
  debuggingEraseFlash,
  debuggingFaultFind,
  debuggingCompareFlashToSDCard,
  debuggingFlashDetails,
  debuggingSerialPassThrough,
  debuggingSerialPassThroughProg,

  // Run to completion where they stand and leave the machine in the menus.
  setVerify,
  setRetry,
  setVolume,
  setDiskDelay,
  setTimeZone,
  setSSID,
  setPassword,
  resetESP,
  resetDevice,
  getSdFiles
};

/**
 * @brief The state an action leaves the machine in.
 *
 * @param action the action being started
 * @result its state, or menus for an action that finishes where it stands and so
 *         never becomes a state
 */
XCopyState stateForAction(XCopyAction action);

#endif // XCOPYACTION_H
