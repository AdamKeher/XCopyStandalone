#include "XCopyAction.h"

XCopyState stateForAction(XCopyAction action)
{
    switch (action)
    {
    case XCopyAction::copyADFToDisk:                 return copyADFToDisk;
    case XCopyAction::copyDiskToADF:                 return copyDiskToADF;
    case XCopyAction::copyDiskToSCP:                 return copyDiskToSCP;
    case XCopyAction::copyDiskToDisk:                return copyDiskToDisk;
    case XCopyAction::copyDiskToFlash:               return copyDiskToFlash;
    case XCopyAction::copyFlashToDisk:               return copyFlashToDisk;
    case XCopyAction::testDisk:                      return testDisk;
    case XCopyAction::formatDisk:                    return formatDisk;
    case XCopyAction::fluxDisk:                      return fluxDisk;
    case XCopyAction::scanBlocks:                    return scanBlocks;
    case XCopyAction::diskSearch:                    return diskSearch;
    case XCopyAction::modSearch:                     return modSearch;
    case XCopyAction::testDrive:                     return testDrive;
    case XCopyAction::directorySelection:            return directorySelection;
    case XCopyAction::about:                         return about;
    case XCopyAction::showTime:                      return showTime;
    case XCopyAction::debuggingTempFile:             return debuggingTempFile;
    case XCopyAction::debuggingSDFLash:              return debuggingSDFLash;
    case XCopyAction::debuggingEraseCopy:            return debuggingEraseCopy;
    case XCopyAction::debuggingEraseFlash:           return debuggingEraseFlash;
    case XCopyAction::debuggingFaultFind:            return debuggingFaultFind;
    case XCopyAction::debuggingCompareFlashToSDCard: return debuggingCompareFlashToSDCard;
    case XCopyAction::debuggingFlashDetails:         return debuggingFlashDetails;
    case XCopyAction::liveStream:                    return liveStream;

    // Programming mode is the passthrough state with the ESP held in its bootloader;
    // the caller raises _espProgMode and drops the baud rate around it.
    case XCopyAction::debuggingSerialPassThrough:
    case XCopyAction::debuggingSerialPassThroughProg:
        return debuggingSerialPassThrough;

    // Everything else -- the settings, the resets, the SD listing -- is finished by
    // the time it returns, so the machine is back in the menus.
    default:
        return menus;
    }
}
