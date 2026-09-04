#ifndef XCOPYSTATE_H
#define XCOPYSTATE_H

/*
   What the machine is currently doing.

   This is deliberately not the same thing as what the user asked for -- that is
   XCopyAction, and XCopyAction.h maps one to the other. The two used to share a
   single enum, so "set the volume" and "copying a disk to ADF" were the same kind
   of value and nothing stopped either being used where the other belonged. An
   action that runs to completion where it stands (every setting, reset ESP, reset
   device, list the SD card) has no state at all and no longer appears here.

   processState() dispatches on these; XCopy::_xcopyState holds one.

   The numbers are a wire format. XCopyESP8266::setState() sends them to the browser
   as "setState,<n>" and esp8266/data/scripts/websocket.js switches on 3, 4, 5, 13,
   18, 19, 24 and 25, so those may not be renumbered. The gaps are where values that
   turned out to be actions, not states, used to sit.
*/
enum XCopyState {
  menus = 1,
  idle = 2,
  copyDiskToADF = 3,
  testDisk = 4,
  copyADFToDisk = 5,
  showTime = 6,
  about = 7,
  debuggingTempFile = 8,
  debuggingSDFLash = 9,
  debuggingEraseCopy = 10,
  debuggingCompareFlashToSDCard = 12,
  copyDiskToDisk = 13,
  directorySelection = 14,
  copyDiskToFlash = 18,
  copyFlashToDisk = 19,
  debuggingFlashDetails = 23,
  fluxDisk = 24,
  formatDisk = 25,
  debuggingSerialPassThrough = 26,
  debuggingFaultFind = 31,
  debuggingEraseFlash = 32,
  scanBlocks = 39,
  diskSearch = 40,
  modSearch = 41,
  copyDiskToSCP = 42,
  // Serial console only: the USB port in binary mode, streaming to a host that is
  // doing the decoding. The web UI has no button for it and websocket.js does not
  // switch on it, so the number is free but is still never reused.
  liveStream = 43,
  // The continuous head calibration test. 34 was the drive signal monitor this
  // replaced and is retired rather than reused, like every other gap above.
  headCalibration = 44,
  // The drive toolkit: every interface line sampled, the safe outputs drivable
  // from the console and the browser. A far more capable descendant of what used
  // to sit at 34, which stays retired - this is a new number, not that one reused.
  driveToolkit = 45,
  // The Amiga track analyser behind the Disk Info tab. A new number for the
  // same reason as the two above: 46 has never been anything else.
  analyseDisk = 46
};

#endif // XCOPYSTATE_H
