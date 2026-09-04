#ifndef XCOPYCOMMAND_H
#define XCOPYCOMMAND_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyFloppy.h"
#include "GenericList.h"
#include "XCopyPins.h"
#include "XCopyLog.h"
#include "XCopyAdfHost.h"
#include "XCopyAdfView.h"
#include "XCopyAdfMount.h"
#include "XCopyAdfWalk.h"
#include "XCopyAdfFloppyDriver.h"
#include "XCopyAdfCopy.h"
#include "XCopyESP8266.h"
#include "XCopyConfig.h"
#include "XCopyTime.h"
#include "XCopySDCard.h"
#include "XCopyConsole.h"
#include "XCopyDisk.h"
#include "XCopyBrainFile.h"
#include "XCopyCommandTable.h"
#include "XCopyArgs.h"
#include "XCopyLineEditor.h"
#include "XCopyComplete.h"
#include <TimeLib.h>
#include <MD5.h>

typedef void (*OnWebCommand)(void* obj, const String command);

/*
   A sink for single keystrokes, for a screen that is driven by keys rather than
   by typed lines. doCommand() only fires on Enter, which is no use to something
   the operator is adjusting live.
*/
typedef void (*OnRawKey)(void* caller, char key);

class XCopyCommandLine
{
public:
  XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config, XCopyDisk* disk, XCopyFloppy* floppy);
  void doCommand(String command);
  void printPrompt();
  bool printDirectory(String directory, bool color = true);
  void processKey(char key);
  void processKeys(String keys);
  void Update();

  void setCallBack(void* caller, OnWebCommand function);

  /**
   * @brief Divert every keystroke to @p function instead of the line editor.
   *
   * Both entry points funnel through processKey(): the USB console via Update()
   * and the browser terminal via processKeys(), so one hook serves both and a
   * key screen is drivable from either without knowing which it is talking to.
   *
   * @param function nullptr restores normal line editing.
   */
  void setRawKeys(void* caller, OnRawKey function);

private:
  String _version;
  XCopyESP8266 *_esp;
  XCopyConfig *_config;
  XCopyDisk *_disk;
  XCopyFloppy *_floppy;

  void* _caller;
  OnWebCommand _callback;

  void* _rawCaller = nullptr;
  OnRawKey _rawKeys = nullptr;
  void setBusy(bool state);

  /*
     The line being typed, and what Tab does with it. Both entry points - the USB
     console through Update() and the browser through processKeys() - hand their
     keys to the same editor, so a line started in one terminal can be finished in
     the other, which was already true and is now true of the cursor too.
  */
  XCopyLineEditor _editor;
  XCopyCompleter _completer;

  //! The editor calls back through these; C function pointers, so they are static.
  static void onEditorLine(void *caller, const String &line);
  static void onEditorComplete(void *caller, uint8_t presses);

  /*
     One handler per command, in table order, each taking its arguments already
     parsed and validated against XCOPY_COMMANDS. What is not here any more is
     what every one of them used to open with: the four line SD card preamble and
     the "Disk not inserted into floppy" check, which doCommand() now does once
     from the command's XCOPY_NEEDS_ flags.
  */
  void dispatch(const XCopyCommandDef *command, const XCopyArgs &args);

  void cmdHelp(const XCopyArgs &args);
  void cmdVersion();
  void cmdClear();
  void cmdReboot();
  void cmdConfig();
  void cmdMem();

  void cmdDir(const XCopyArgs &args);
  void cmdCat(const XCopyArgs &args);
  void cmdRm(const XCopyArgs &args);
  void cmdMd5(const XCopyArgs &args);
  void cmdCp(const XCopyArgs &args);
  void cmdMkdir(const XCopyArgs &args);
  void cmdMount(const XCopyArgs &args);

  //! False, with the reason printed, when a slot is mounted read only.
  bool writableVolume(XCopyAdfMount::Slot &slot);
  //! False, with the reason printed, when @p openAdfFiles will not fit the heap.
  bool heapAllows(uint8_t openAdfFiles);

  /*
     The three copies that have one foot on the SD card. The fourth - between two
     mounted volumes - is adfCopyFile() in XCopyAdfCopy.h, which is plain C over
     ADFlib and is therefore tested on the host.
  */
  bool copyCardToVolume(const String &fromPath, XCopyAdfMount::Slot &toSlot,
                        const String &toDirectory, const String &toLeaf,
                        uint8_t *buffer, size_t bufferSize, unsigned long &copied);
  bool copyVolumeToCard(XCopyAdfMount::Slot &fromSlot, const String &fromPath,
                        const String &fromLeaf, const String &toPath,
                        uint8_t *buffer, size_t bufferSize, unsigned long &copied);
  bool copyCardToCard(const String &fromPath, const String &toPath,
                      uint8_t *buffer, size_t bufferSize, unsigned long &copied);
  void cmdUnmount(const XCopyArgs &args);

  //! "dir" when the path named a mounted image rather than the card.
  void listVolume(XCopyAdfMount::Slot &slot, const String &within);
  //! "cat" likewise.
  void catVolume(XCopyAdfMount::Slot &slot, const String &within);

  void cmdReadAdf(const XCopyArgs &args);
  void cmdWriteAdf(const XCopyArgs &args);
  void cmdReadScp(const XCopyArgs &args);
  void cmdWriteFlash();
  void cmdWriteBin(const XCopyArgs &args);
  void cmdLive();
  void cmdTestDisk();
  void cmdScanBlocks();
  void cmdSearch(const XCopyArgs &args);
  void cmdModSearch();
  void cmdModRip(const XCopyArgs &args);

  void cmdBoot(const XCopyArgs &args);
  void cmdHist();
  void cmdRpm(const XCopyArgs &args);
  void cmdHeadCal(const XCopyArgs &args);
  void cmdDriveToolkit();
  void cmdName();
  void cmdPrint();
  void cmdRead(const XCopyArgs &args);
  void cmdDump(const XCopyArgs &args);
  void cmdVol(const XCopyArgs &args);
  //! Open and mount an image read only, printing what it is. nullptr on failure.
  struct AdfDevice *openImageForReading(const String &path);
  void cmdWeak();

  void cmdTime();
  void cmdSetTime(const XCopyArgs &args);
  void cmdTimeZone(const XCopyArgs &args);

  void cmdConnect(const XCopyArgs &args);
  void cmdClearWifi();
  void cmdScan();
  void cmdWebsocket(const XCopyArgs &args);
  void cmdPass();

  //! The four commands that are one round trip to the ESP and a printed answer.
  void espQuery(const char *command, uint32_t timeout = 0);

  //! Read eleven sectors of a logical track out of the flash disk image.
  void readTrackFromFlash(uint16_t track);

  // Help, generated from the table rather than written out beside it.
  void printHelp();
  void printCommandHelp(const XCopyCommandDef *command);
  //! One entry as it appears in the command column: name, alias and subject.
  static String helpSignature(const XCopyCommandDef *command);
};

#endif // XCOPYCOMMAND
