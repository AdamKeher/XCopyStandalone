#ifndef XCOPYSTATE_H
#define XCOPYSTATE_H

enum XCopyState {
  undefined = 0,
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
  setVerify = 15,
  setRetry = 16,
  setVolume = 17,
  copyDiskToFlash = 18,
  copyFlashToDisk = 19,
  debuggingFlashDetails = 23,
  fluxDisk = 24,
  formatDisk = 25,
  debuggingSerialPassThrough = 26,
  debuggingSerialPassThroughProg = 27,
  setSSID = 28,
  setPassword = 29,
  resetESP = 30,
  debuggingFaultFind = 31,
  debuggingEraseFlash = 32,
  setDiskDelay = 33,
  testDrive = 34,
  resetDevice = 35,
  getSdFiles = 36,
  setTimeZone = 37,
  sendBlock = 38,
  scanBlocks = 39
};

#endif // XCOPYSTATE_H