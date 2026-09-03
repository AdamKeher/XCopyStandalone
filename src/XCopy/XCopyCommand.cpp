#include "XCopyCommand.h"
#include "XCopyScratch.h"
#include <Streaming.h>
#include <SerialFlash.h>

/*
   The console.

   Everything the interpreter knows about the commands themselves lives in
   XCopyCommandTable.cpp. This file is the plumbing around it: read a line, find
   the command, let XCopyArgs check the arguments against the table, and call the
   one handler. What it is not any more is a 45 arm chain of string compares with
   the argument parsing, the disk and card checks, and a second hand written copy
   of the help all folded into it.
*/

// Frame geometry for the help table. The same 88 column box XCopyTrackMap and the
// head calibration panel are drawn in, so the console looks like one program.
static const uint8_t HELP_LEFT = 31;
static const uint8_t HELP_RIGHT = 52;
static const uint8_t HELP_INNER = 86;

static String pad(const String &text, uint8_t width)
{
    String out = text;
    // Truncate rather than let a long entry push the right hand edge out. Nothing
    // in the table is close, but a row that breaks the frame is how a table stops
    // being readable at all.
    if (out.length() > width)
        out.remove(width);
    while (out.length() < width)
        out += ' ';

    return out;
}

static void helpRow(const String &left, const String &right)
{
    Log << "| " + pad(left, HELP_LEFT) + "| " + pad(right, HELP_RIGHT) + "|\r\n";
}

static void helpWide(const String &text)
{
    Log << "| " + pad(text, HELP_INNER - 1) + "|\r\n";
}

static void helpRule(char left, char fill, char joint, char right)
{
    String line;
    line.reserve(90);
    line += left;
    for (uint8_t i = 0; i < HELP_LEFT + 1; i++)
        line += fill;
    line += joint;
    for (uint8_t i = 0; i < HELP_RIGHT + 1; i++)
        line += fill;
    line += right;
    line += "\r\n";
    Log << line;
}

/*
   The two things the editor and the completer are handed instead of reaching for
   them, so that neither has to include the log or the SD card. See XCopyConsoleIO.h.
*/
static void writeConsole(const String &text)
{
    Log << text;
}

static void listSdDirectory(const String &directory, XCopyDirVisit visit, void *context)
{
    XCopySDCard sdcard;

    if (!sdcard.cardDetect() || !sdcard.begin() || !sdcard.open(directory))
        return;

    while (sdcard.next())
    {
        const XCopyFile &file = sdcard.getfile();
        visit(context, file.filename, file.isDirectory);
    }
}

XCopyCommandLine::XCopyCommandLine(String version, XCopyESP8266 *esp, XCopyConfig *config, XCopyDisk* disk, XCopyFloppy* floppy)
{
    _version = version;
    _esp = esp;
    _config = config;
    _disk = disk;
    _floppy = floppy;

    _editor.begin(this, onEditorLine, onEditorComplete, writeConsole);
    _completer.begin(listSdDirectory);
}

void XCopyCommandLine::onEditorLine(void *caller, const String &line)
{
    XCopyCommandLine *self = (XCopyCommandLine *)caller;
    self->doCommand(line);
    self->printPrompt();
}

void XCopyCommandLine::onEditorComplete(void *caller, uint8_t presses)
{
    XCopyCommandLine *self = (XCopyCommandLine *)caller;
    self->_completer.complete(self->_editor, presses);
}

// DISPATCH

/**
 * @brief Mount the SD card, reporting the reason if it will not.
 *
 * Was copy-pasted into eight handlers, each with its own spelling of the same
 * four checks, and two of them called begin() twice.
 */
static bool mountSdCard()
{
    XCopySDCard sdcard;

    if (!sdcard.cardDetect() || !sdcard.begin())
    {
        Log << sdcard.getError() << "\r\n";
        return false;
    }

    return true;
}

void XCopyCommandLine::doCommand(String command)
{
    // Arduino's char/char replace substitutes bytes without changing the length, so
    // the previous replace((char)10, (char)0) pair embedded NULs instead of removing
    // anything. Replacing with an empty String actually shortens it.
    command.replace("\r", "");
    command.replace("\n", "");
    command.trim();

    if (command.length() == 0)
        return;

    // The command word, and the rest of the line for XCopyArgs. Split here rather
    // than in the tokenizer so a raw tail command keeps its spacing exactly.
    String name = command;
    String tail = "";
    const int space = command.indexOf(' ');
    if (space > 0)
    {
        name = command.substring(0, space);
        tail = command.substring(space + 1);
    }

    const XCopyCommandDef *def = xcopyFindCommand(name);
    if (def == nullptr)
    {
        Log << "Unknown command: '" << name << "'. Type 'help' for a list.\r\n";
        return;
    }

    String error;
    XCopyArgs args;
    if (!args.parse(def, tail, error))
    {
        Log << XCopyConsole::error(error) << "\r\n";
        Log << "Try 'help " << def->name << "'\r\n";
        return;
    }

    /*
       The two preconditions that used to open most handlers. Checked from the
       command's flags, before the handler runs, so there is one wording of each
       and no handler can forget one - "writeflash" never had the disk check that
       every other write command does.
    */
    if ((def->flags & XCOPY_NEEDS_DISK) && !_floppy->diskChange())
    {
        Log << F("Disk not inserted into floppy\r\n");
        return;
    }

    if ((def->flags & XCOPY_NEEDS_SD) && !mountSdCard())
        return;

    dispatch(def, args);
}

void XCopyCommandLine::dispatch(const XCopyCommandDef *command, const XCopyArgs &args)
{
    switch (command->id)
    {
    case XCopyCmd::help:       cmdHelp(args);       break;
    case XCopyCmd::version:    cmdVersion();        break;
    case XCopyCmd::clear:      cmdClear();          break;
    case XCopyCmd::reboot:     cmdReboot();         break;
    case XCopyCmd::config:     cmdConfig();         break;
    case XCopyCmd::mem:        cmdMem();            break;

    case XCopyCmd::dir:        cmdDir(args);        break;
    case XCopyCmd::cat:        cmdCat(args);        break;
    case XCopyCmd::rm:         cmdRm(args);         break;
    case XCopyCmd::md5:        cmdMd5(args);        break;

    case XCopyCmd::readadf:    cmdReadAdf(args);    break;
    case XCopyCmd::writeadf:   cmdWriteAdf(args);   break;
    case XCopyCmd::readscp:    cmdReadScp(args);    break;
    case XCopyCmd::writeflash: cmdWriteFlash();     break;
    case XCopyCmd::writebin:   cmdWriteBin(args);   break;
    case XCopyCmd::live:       cmdLive();           break;
    case XCopyCmd::testdisk:   cmdTestDisk();       break;
    case XCopyCmd::scanblocks: cmdScanBlocks();     break;
    case XCopyCmd::search:     cmdSearch(args);     break;
    case XCopyCmd::modsearch:  cmdModSearch();      break;
    case XCopyCmd::modrip:     cmdModRip(args);     break;

    case XCopyCmd::boot:       cmdBoot(args);       break;
    case XCopyCmd::hist:       cmdHist();           break;
    case XCopyCmd::rpm:        cmdRpm(args);        break;
    case XCopyCmd::headcal:    cmdHeadCal(args);    break;
    case XCopyCmd::name:       cmdName();           break;
    case XCopyCmd::print:      cmdPrint();          break;
    case XCopyCmd::read:       cmdRead(args);       break;
    case XCopyCmd::dump:       cmdDump(args);       break;
    case XCopyCmd::vol:        cmdVol(args);        break;
    case XCopyCmd::weak:       cmdWeak();           break;

    case XCopyCmd::time:       cmdTime();           break;
    case XCopyCmd::settime:    cmdSetTime(args);    break;
    case XCopyCmd::timezone:   cmdTimeZone(args);   break;

    case XCopyCmd::connect:    cmdConnect(args);    break;
    case XCopyCmd::clearwifi:  cmdClearWifi();      break;
    case XCopyCmd::status:     espQuery("status");  break;
    case XCopyCmd::ip:         espQuery("ip");      break;
    case XCopyCmd::mac:        espQuery("mac");     break;
    case XCopyCmd::ssid:       espQuery("ssid");    break;
    case XCopyCmd::websocket:  cmdWebsocket(args);  break;
    case XCopyCmd::scan:       cmdScan();           break;
    case XCopyCmd::ping:       espQuery("ping", 5000); break;
    case XCopyCmd::pass:       cmdPass();           break;

    default:
        break;
    }
}

// HELP

String XCopyCommandLine::helpSignature(const XCopyCommandDef *command)
{
    String line = command->name;

    if (command->alias != nullptr)
    {
        line += " | ";
        line += command->alias;
    }

    if (command->subject != XCopyArgKind::none && command->subjectName != nullptr)
    {
        line += " <";
        line += command->subjectName;
        line += ">";
    }

    // Not the options themselves: "readscp -cyls <text> -revs <n> -file <path>" is
    // wider than the column and would be truncated into a lie. This says there are
    // some, and "help readscp" lists them.
    if (command->optionCount > 0)
        line += " [-options]";

    return line;
}

void XCopyCommandLine::printHelp()
{
    helpRule('.', '-', '-', '.');
    helpWide("X-Copy Standalone " + _version);
    helpRule('|', '-', '-', '|');
    helpRow("Command", "Description");

    for (uint8_t category = 0; category < (uint8_t)XCopyCat::count; category++)
    {
        helpRule('|', '-', '+', '|');

        bool heading = false;
        for (uint8_t i = 0; i < XCOPY_COMMAND_COUNT; i++)
        {
            const XCopyCommandDef &entry = XCOPY_COMMANDS[i];
            if ((uint8_t)entry.category != category)
                continue;

            // Printed here rather than before the loop, so a category with nothing
            // in it leaves no empty heading behind.
            if (!heading)
            {
                helpWide(xcopyCategoryName(entry.category));
                helpRule('|', '-', '+', '|');
                heading = true;
            }

            helpRow(helpSignature(&entry), entry.help);
        }
    }

    helpRule('`', '-', '-', '\'');
    Log << F("Options are named: readscp -cyls 0-83 -revs 3 -file \"my disk.scp\"\r\n");
    Log << F("'help <command>' lists the options of one command.\r\n");
}

void XCopyCommandLine::printCommandHelp(const XCopyCommandDef *command)
{
    Log << XCopyConsole::bold_white() << command->name << XCopyConsole::reset();
    if (command->alias != nullptr)
        Log << " | " << command->alias;
    Log << F(" - ") << command->help << F("\r\n\r\n");

    Log << F("  usage: ") << helpSignature(command) << F("\r\n");

    if (command->subject != XCopyArgKind::none && command->subjectName != nullptr)
    {
        Log << F("    <") << command->subjectName << F("> ")
            << ((command->flags & XCOPY_SUBJECT_REQUIRED) ? F("(required) ") : F("(optional) "))
            << xcopyKindName(command->subject) << F("\r\n");
    }

    for (uint8_t i = 0; i < command->optionCount; i++)
    {
        const XCopyOption &option = command->options[i];
        String line = "    -";
        line += option.name;
        if (option.kind != XCopyArgKind::flag)
        {
            line += " ";
            line += (option.kind == XCopyArgKind::choice) ? String(option.choices) : String(xcopyKindName(option.kind));
        }
        Log << pad(line, 24) << option.help << F("\r\n");
    }

    if (command->flags & XCOPY_NEEDS_DISK)
        Log << F("  needs a disk in the drive\r\n");
    if (command->flags & XCOPY_NEEDS_SD)
        Log << F("  needs an SD card\r\n");
}

void XCopyCommandLine::cmdHelp(const XCopyArgs &args)
{
    if (!args.hasSubject())
    {
        printHelp();
        return;
    }

    const XCopyCommandDef *command = xcopyFindCommand(args.subject());
    if (command == nullptr)
    {
        Log << "Unknown command: '" << args.subject() << "'\r\n";
        return;
    }

    printCommandHelp(command);
}

// GENERAL

void XCopyCommandLine::cmdVersion()
{
    Log << F("Version: ") << _version << F("\r\n");
}

void XCopyCommandLine::cmdClear()
{
    Log << XCopyConsole::clearscreen() << XCopyConsole::home();
}

void XCopyCommandLine::cmdReboot()
{
    _callback(_caller, "rebootDevice");
}

void XCopyCommandLine::cmdConfig()
{
    _config->dumpConfig();
}

void XCopyCommandLine::cmdMem()
{
    uint32_t stackTop;
    uint32_t heapTop;

    // current position of the stack.
    stackTop = (uint32_t) &stackTop;

    // current position of heap.
    void* hTop = malloc(1);
    heapTop = (uint32_t) hTop;
    free(hTop);

    char sStackTop[12];
    char sHeapTop[12];

    sprintf(sStackTop, "0x%08" PRIx32, stackTop);
    sprintf(sHeapTop, "0x%08" PRIx32,  heapTop);

    // The difference is (approximately) the free, available ram.
    Log << F("Stack Top: ") << sStackTop << F("\r\n");
    Log << F("Heap Top: ") << sHeapTop << F("\r\n");
    Log << F("Free: ") << (stackTop - heapTop) << F(" bytes free\r\n");
}

// SD CARD

void XCopyCommandLine::cmdDir(const XCopyArgs &args)
{
    setBusy(true);
    printDirectory(args.subject());
    setBusy(false);
}

void XCopyCommandLine::cmdCat(const XCopyArgs &args)
{
    const String &path = args.subject();

    FatFile file;
    if (!file.open(path.c_str()))
    {
        Log << F("unable to open: '") + path + F("'\r\n");
        return;
    }

    const size_t bufferSize = 512;
    XCopyScratch::Guard scratch("command.dump", bufferSize);
    if (!scratch.valid())
    {
        Log << F("track buffer busy\r\n");
        file.close();
        return;
    }
    char *buffer = (char *)scratch.get();

    // read() returns -1 on error; the old loop tested readsize only after using
    // it, so a failed read appended garbage and then looped forever on (size_t)-1.
    int readsize = 0;
    while ((readsize = file.read(buffer, bufferSize)) > 0)
    {
        String line = "";
        for (int i = 0; i < readsize; i++)
        {
            line.append(buffer[i]);
        }
        Log << line;
    }

    file.close();

    Log << F("[-- eof]\r\n");
}

void XCopyCommandLine::cmdRm(const XCopyArgs &args)
{
    XCopySDCard sdcard;
    const String &path = args.subject();

    if (sdcard.deleteFile(path))
        Log << "'" + path + F("' deleted\r\n");
    else
        Log << F("unable to delete: '") + path + F("'\r\n");
}

void XCopyCommandLine::cmdMd5(const XCopyArgs &args)
{
    /*
       One of the two, never both and never neither. This was a positional that
       meant a filename unless it happened to spell "flash", so a file called
       flash could not be hashed and a typo hashed the wrong thing silently.
    */
    if (args.has("flash"))
    {
        Log << _disk->flashToMD5() + "\r\n";
        return;
    }

    if (!args.hasSubject())
    {
        Log << F("md5 needs a file, or -flash\r\n");
        return;
    }

    if (!mountSdCard())
        return;

    XCopySDCard sdcard;
    const String &path = args.subject();

    if (!sdcard.fileExists(path))
    {
        Log << "File not found: '" + path + "'\r\n";
        return;
    }

    Log << _disk->adfToMD5(path) + "\r\n";
}

// FLOPPY DISK

void XCopyCommandLine::cmdReadAdf(const XCopyArgs &args)
{
    // No filename given: diskToADF() names the file after the disk label and
    // the current date & time, the same as the menu and the web UI do.
    String path = args.text("file");

    if (path != "")
    {
        // On a copy: toLowerCase() mutates in place and path is the destination
        // path, which has to keep the case the user typed.
        String extension = path;
        if (!extension.toLowerCase().endsWith(".adf"))
        {
            Log << F("The file must be an ADF file\r\n");
            return;
        }

        // A bare filename lands in the ADF folder alongside the generated ones;
        // diskToADF() creates that directory if it is missing.
        if (path.indexOf("/") == -1)
            path = "/" + String(SD_ADF_PATH) + "/" + path;
    }

    _callback(_caller, "copyDiskToADF," + path);
}

void XCopyCommandLine::cmdWriteAdf(const XCopyArgs &args)
{
    XCopySDCard sdcard;
    const String &path = args.subject();

    if (!sdcard.fileExists(path))
    {
        Log << F("file does not exist\r\n");
        return;
    }

    String extension = path;
    if (!extension.toLowerCase().endsWith(".adf"))
    {
        Log << F("The file must be an ADF file\r\n");
        return;
    }

    _callback(_caller, "writeADFFile," + path);
}

void XCopyCommandLine::cmdReadScp(const XCopyArgs &args)
{
    uint8_t firstCylinder = 0;
    uint8_t lastCylinder = 0;
    uint8_t revolutions = 0;

    /*
       The range is the one value that is still parsed by shape, because "0-83" is
       one value and not two. Everything around it that used to be sniffed - is
       this token a revolution count, or the first word of a filename with spaces
       in it - is now named, so the sniffing cannot reach the filename any more.
    */
    const String range = args.text("cyls");
    if (range != "")
    {
        const int dash = range.indexOf('-');
        bool valid = dash > 0 && dash < (int)range.length() - 1;
        for (unsigned int i = 0; valid && i < range.length(); i++)
            if ((int)i != dash && !isDigit(range.charAt(i)))
                valid = false;

        if (!valid)
        {
            Log << F("-cyls must be a range, <first>-<last>\r\n");
            return;
        }

        firstCylinder = (uint8_t)range.substring(0, dash).toInt();
        lastCylinder = (uint8_t)range.substring(dash + 1).toInt();

        if (lastCylinder >= MAX_CYLINDERS || firstCylinder > lastCylinder)
        {
            Log << F("Cylinder range must be within 0-") << (MAX_CYLINDERS - 1)
                << F(", lowest first\r\n");
            return;
        }
    }

    if (args.has("revs"))
    {
        revolutions = (uint8_t)args.number("revs", 0);

        if (revolutions < 1 || revolutions > SCP_MAX_REVS)
        {
            Log << F("Revolutions must be 1-") << SCP_MAX_REVS << F("\r\n");
            return;
        }
    }

    String filename = args.text("file");

    if (filename != "")
    {
        // On a copy: toLowerCase() mutates in place and filename is the
        // destination path, which has to keep the case the user typed.
        String extension = filename;
        if (!extension.toLowerCase().endsWith(".scp"))
        {
            Log << F("The file must be an SCP file\r\n");
            return;
        }

        // A bare filename lands in the SCP folder alongside the generated ones;
        // diskToSCP() creates that directory if it is missing.
        if (filename.indexOf("/") == -1)
            filename = "/" + String(SD_SCP_PATH) + "/" + filename;
    }

    // "<first>-<last>,<revs>,<path>", with the path last because a filename may
    // contain a comma and the two numeric fields never can. Zero means "not
    // given" and the saved setting applies.
    _callback(_caller, "copyDiskToSCP," + String(firstCylinder) + "-" + String(lastCylinder) +
                           "," + String(revolutions) + "," + filename);
}

void XCopyCommandLine::cmdWriteFlash()
{
    // onWebCommand() matches "copyDiskToFlash"; the old "copyDisktoFlash"
    // spelling fell through every branch and the command did nothing.
    _callback(_caller, "copyDiskToFlash");
}

void XCopyCommandLine::cmdWriteBin(const XCopyArgs &args)
{
    if (!args.has("file") || !args.has("block"))
    {
        Log << F("writebin needs -file and -block\r\n");
        return;
    }

    setBusy(true);
    _disk->writeFileToBlocks(args.text("file"), (int)args.number("block", 0), _config->getRetryCount());
    setBusy(false);
}

void XCopyCommandLine::cmdLive()
{
    /*
       Hands this USB session to XCopyLive, which switches it to the binary protocol
       in shared/XCopyLiveProtocol.h. Everything after the banner XCopyLive prints
       is frames, so nothing may print here on the way in.
    */
    _callback(_caller, "liveStream");
}

void XCopyCommandLine::cmdTestDisk()
{
    _callback(_caller, "testDisk");
}

void XCopyCommandLine::cmdScanBlocks()
{
    _callback(_caller, "scanBlocks");
}

void XCopyCommandLine::cmdSearch(const XCopyArgs &args)
{
    _callback(_caller, "asciiSearch," + args.subject());
}

void XCopyCommandLine::cmdModSearch()
{
    _callback(_caller, "modSearch");
}

void XCopyCommandLine::cmdModRip(const XCopyArgs &args)
{
    if (!args.has("block") || !args.has("offset") || !args.has("size"))
    {
        Log << F("modrip needs -block, -offset and -size\r\n");
        return;
    }

    const int block = (int)args.number("block", 0);
    const int offset = (int)args.number("offset", 0);
    const int size = (int)args.number("size", 0);

    // Checked before setBusy(), not after. The old order took the busy pin and
    // then returned on each of these without giving it back, so a mistyped modrip
    // left the whole device wedged as busy.
    if (block > 1759)
    {
        Log << F("Error: -block must be less than 1759.\r\n");
        return;
    }

    if (offset >= 512)
    {
        Log << F("Error: -offset must be less than 512 bytes.\r\n");
        return;
    }

    if (size <= 0)
    {
        Log << F("Error: -size must be greater than 0 bytes.\r\n");
        return;
    }

    setBusy(true);

    DiskLocation dl;
    dl.setBlock(block);
    if (_disk->modRip(dl, offset, size, _config->getRetryCount()))
        Log << F("Mod file written successfully\r\n");
    else
        Log << F("Writing mod file failed\r\n");

    setBusy(false);
}

// ANALYSIS

void XCopyCommandLine::readTrackFromFlash(uint16_t track)
{
    SerialFlashFile flashFile = SerialFlash.open("DISKCOPY.TMP");
    flashFile.seek(track * 11 * 512);

    for (uint8_t i = 0; i < 11; i++)
    {
        // Straight into the sector. The old code read into a 512 byte local and
        // then byte copied it across, which cost a frame this deep in doCommand()
        // and bought nothing - the destination is already contiguous and exactly
        // this size.
        struct Sector *aSec = (Sector *)&_floppy->getTrack()[i].sector[0];
        flashFile.read(aSec->data, sizeof(aSec->data));
    }
    _floppy->setSectorCnt(11);
}

void XCopyCommandLine::cmdBoot(const XCopyArgs &args)
{
    Log.printf("Reading Track %d\r\n", 0);

    /*
       "boot -flash" is what "bootf" was supposed to be. What bootf actually did
       was rewrite its own command and parameter and then return, so it read
       nothing and printed nothing - it has been listed in the help and broken for
       as long as it has existed.
    */
    if (args.has("flash"))
    {
        readTrackFromFlash(0);
    }
    else
    {
        _floppy->gotoLogicTrack(0);
        // int, not uint8_t: readTrack() signals failure with -1.
        int errors = _floppy->readTrack(false);
        if (errors != -1)
        {
            Log << F("Sectors found: ") << _floppy->getSectorCnt() << F(" Errors found: ");
            Serial.print(errors, BIN);
            Log << F(" Track expected: 0 Track found: ") << _floppy->getTrackInfo()
                << F(" bitCount: ") << _floppy->getBitCount() << F(" (Read OK)\r\n");
        }
        else
        {
            Log << F("bitCount: ") << _floppy->getBitCount() << F(" (Read failed!)\r\n");
        }
    }

    _floppy->printBootSector();

    Log << F("\r\nScanning boot block for match ...\r\n");

    uint32_t crc32 = _floppy->bootSectorCRC32();
    Track *track = _floppy->getTrack();
    struct Sector *block0 = (Sector *)&track[0].sector;
    struct Sector *block1 = (Sector *)&track[0].sector;
    XCopyBrainFile::identifyBootblock(block0->data, block1->data, crc32);
}

void XCopyCommandLine::cmdHist()
{
    _floppy->analyseHist(false);
    _floppy->printHist();
}

void XCopyCommandLine::cmdRpm(const XCopyArgs &args)
{
    uint32_t interval = (uint32_t)args.number("interval", 1000);
    if (interval < 100) interval = 100;

    setBusy(true);
    _floppy->motorOn();
    // The newline that submitted this command is still queued, so the
    // wait-for-keypress loop below would exit before taking a reading.
    while (Serial.available()) Serial.read();
    _floppy->beginRPM();

    Log << F("Measuring drive speed, press any key to stop ...\r\n");

    uint32_t lastReading = millis() - interval;
    while (!Serial.available())
    {
        // motorTimeout() shuts the drive down after motorMaxTick idle
        // seconds; keep resetting the tick or it stops mid measurement
        _floppy->motorOn();

        if (millis() - lastReading < interval) continue;
        lastReading = millis();

        float rpm = _floppy->readRPM();
        if (rpm == 0.0f)
            Log << XCopyConsole::error(F("No index signal detected\r\n"));
        else
            Log << F("Drive speed: ") << String(rpm, 2) << F(" RPM\r\n");
    }
    while (Serial.available()) Serial.read();

    _floppy->endRPM();
    _floppy->motorOff();
    setBusy(false);
}

void XCopyCommandLine::cmdHeadCal(const XCopyArgs &args)
{
    // Starts the state and returns, like testdisk. Deliberately not the rpm
    // pattern of owning the console in a loop: that would starve the ESP link
    // and the TFT for as long as somebody was adjusting the drive.
    _callback(_caller, "headCalibration," + args.subject());
}

void XCopyCommandLine::cmdName()
{
    Log << F("Diskname: ") << _floppy->getName() << F("\r\n");
}

void XCopyCommandLine::cmdPrint()
{
    _floppy->printTrack();
    Log << F("OK\r\n");
}

void XCopyCommandLine::cmdRead(const XCopyArgs &args)
{
    const int track = (int)args.subjectNumber(0);

    // "read <n> -flash" is what "readf <n>" was. Same eleven sectors, out of the
    // flash disk image instead of off the drive.
    if (args.has("flash"))
    {
        readTrackFromFlash((uint16_t)track);
        return;
    }

    Log.printf("Reading Track %2d:\r\n", track);
    _floppy->gotoLogicTrack(track);
    // int, not uint8_t: readTrack() signals failure with -1.
    int errors = _floppy->readTrack(false);
    if (errors != -1)
    {
        Log << F("Sectors found: ") << _floppy->getSectorCnt() << F(" Errors found: ");
        Log << String(errors, BIN);
        Log << F(" Track expected: ") + String(track) + F(" Track found: ") + String(_floppy->getTrackInfo()) + F(" bitCount: ") + String(_floppy->getBitCount()) + F(" (Read OK)\r\n");
    }
    else
    {
        Log << F("bitCount: ") + String(_floppy->getBitCount()) + F(" (Read failed!)\r\n");
    }
}

/*
   Open an image, describe it, and hand back the open device.

   Shared by "vol" and "dump", which differ only in whether they go on to list the
   root directory. Both open read only: neither changes anything, and an image
   opened read only cannot be damaged by a mistake in either.

   @result an open, mounted device the caller must adfDevClose(), or nullptr - in
           which case the reason has already been printed.
*/
struct AdfDevice *XCopyCommandLine::openImageForReading(const String &path)
{
    XCopyAdf::begin();
    XCopyAdf::clearErrors();

    // adfDevOpenWithDriver rather than adfDevOpen: the driver that can read a file
    // off the card is known here, and letting the library guess from the path would
    // only give it a chance to guess wrong.
    struct AdfDevice *dev = adfDevOpenWithDriver("sd", path.c_str(),
                                                 ADF_ACCESS_MODE_READONLY);
    if (dev == NULL)
    {
        Log << XCopyConsole::error("unable to open '" + path + "'") << F("\r\n");
        return nullptr;
    }

    XCopyAdfView::printDevice(dev);

    if (adfDevMount(dev) != ADF_RC_OK)
    {
        // A device that will not mount is still worth describing, which is why
        // printDevice() ran first: "1760 blocks, unknown type" answers most of the
        // questions somebody asks of a file that will not open.
        Log << XCopyConsole::error("no filesystem found on '" + path + "'") << F("\r\n");
        adfDevClose(dev);
        return nullptr;
    }

    return dev;
}

void XCopyCommandLine::cmdVol(const XCopyArgs &args)
{
    const String path = args.subject();

    struct AdfDevice *dev = openImageForReading(path);
    if (dev == nullptr)
        return;

    for (int i = 0; i < dev->nVol; i++)
    {
        struct AdfVolume *vol = adfVolMount(dev, i, ADF_ACCESS_MODE_READONLY);
        if (vol == NULL)
        {
            Log << XCopyConsole::error("unable to mount volume " + String(i)) << F("\r\n");
            continue;
        }

        XCopyAdfView::printVolume(vol);
        adfVolUnMount(vol);
    }

    adfDevClose(dev);
}

void XCopyCommandLine::cmdDump(const XCopyArgs &args)
{
    const String path = args.subject();

    struct AdfDevice *dev = openImageForReading(path);
    if (dev == nullptr)
        return;

    for (int i = 0; i < dev->nVol; i++)
    {
        struct AdfVolume *vol = adfVolMount(dev, i, ADF_ACCESS_MODE_READONLY);
        if (vol == NULL)
        {
            Log << XCopyConsole::error("unable to mount volume " + String(i)) << F("\r\n");
            continue;
        }

        XCopyAdfView::printVolume(vol);
        Log << F("Directory:\r\n");
        XCopyAdfView::printListHeader();

        const uint16_t listed = XCopyAdfView::printDirectory(vol, vol->rootBlock);
        Log << listed << (listed == 1 ? F(" entry\r\n") : F(" entries\r\n"));

        adfVolUnMount(vol);
    }

    adfDevClose(dev);
}

void XCopyCommandLine::cmdWeak()
{
    Log << _floppy->getWeakTrack() << F("\r\n");
}

// DATE AND TIME

void XCopyCommandLine::cmdTime()
{
    int timeZone = _config->getTimeZone();
    Log.printf("%02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
}

void XCopyCommandLine::cmdSetTime(const XCopyArgs &args)
{
    int timeZone = _config->getTimeZone();
    Log << F("Current Time: ") << XCopyTime::getTime() << F(" (epoch)");
    Log.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);

    time_t time = args.has("epoch") ? (time_t)args.number("epoch", 0) : _esp->getTime();

    time = time + (timeZone * 60 * 60);
    XCopyTime::syncTime(false);
    XCopyTime::setTime(time);
    delay(2000);
    XCopyTime::syncTime(true);
    delay(2000);
    Log << F("Updated Time: ") << time << F(" (epoch)");
    Log.printf(" | %02d:%02d:%02d %02d/%02d/%04d %s%02d\r\n", hour(), minute(), second(), day(), month(), year(), timeZone >= 0 ? "+" : "", timeZone);
}

void XCopyCommandLine::cmdTimeZone(const XCopyArgs &args)
{
    if (args.hasSubject())
    {
        int timeZone = (int)args.subjectNumber(0);
        if (timeZone > 12) timeZone = 12;
        if (timeZone < -12) timeZone = -12;
        _config->setTimeZone(timeZone);
    }

    Log << F("Time Zone: ") << _config->getTimeZone() << F("\r\n");
}

// NETWORK

void XCopyCommandLine::espQuery(const char *command, uint32_t timeout)
{
    String answer = timeout == 0 ? _esp->sendCommand(command, true)
                                 : _esp->sendCommand(command, true, timeout);
    Log << answer << F("\r\n");
}

void XCopyCommandLine::cmdConnect(const XCopyArgs &args)
{
    /*
       Named, so an SSID or a password containing a space finally works. The old
       form split the parameter at its first space, which made those unreachable
       from the console without any message saying so.
    */
    if (!args.has("ssid") || !args.has("pass"))
    {
        Log << F("connect needs -ssid and -pass\r\n");
        return;
    }

    setBusy(true);

    const String ssid = args.text("ssid");
    const String password = args.text("pass");

    _config->setSSID(ssid);
    _config->setPassword(password);
    _config->writeConfig();

    if (_esp->connect(ssid, password, 20000))
        Log << F("Connected to '") << ssid << F("'\r\n");
    else
        Log << F("Error: Connection to '") << ssid << F("' failed\r\n");

    setBusy(false);
}

void XCopyCommandLine::cmdScan()
{
    setBusy(true);
    Log << F("Scanning: \r\n");
    espQuery("scan", 5000);
    setBusy(false);
}

void XCopyCommandLine::cmdClearWifi()
{
    _config->setSSID("");
    _config->setPassword("");
    _config->writeConfig();
    _config->dumpConfig();
    // The ESP keeps its own copy now, so that it can rejoin the network after
    // a reset without the Teensy. Clearing only this side would leave it
    // reconnecting to a network the device no longer thinks it has.
    _esp->sendCommand("forget", true);
    Log << F("WiFi settings cleared\r\n");
}

void XCopyCommandLine::cmdWebsocket(const XCopyArgs &args)
{
    _esp->sendWebSocket(args.subject());
    Log << F("broadcast: '") << args.subject() << F("'\r\n");
}

void XCopyCommandLine::cmdPass()
{
    // The old handler had no return, so it fell out of the if-chain and printed
    // "Unknown command: 'pass'" every time it worked.
    _callback(_caller, "debuggingSerialPassThrough");
}

// CONSOLE

void XCopyCommandLine::printPrompt()
{
    _editor.prompt();
}

bool XCopyCommandLine::printDirectory(String directory, bool color) {
    XCopySDCard *_sdcard = new XCopySDCard();

    if (!_sdcard->cardDetect()) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    if (!_sdcard->begin()) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    if (directory == "")
        directory = "/";

    if (!_sdcard->open(directory)) {
        Log << _sdcard->getError() << "\r\n";
        delete _sdcard;
        return false;
    }

    Log << "Directory: " << directory << "\r\n";

    uint16_t _count = 0;
    while (_sdcard->next()) {
        _count++;

        const XCopyFile &file = _sdcard->getfile();

        String filesize = String(file.size);
        while (filesize.length() < 9)
            filesize = " " + filesize;

        String filename = file.filename;
        if (file.isDirectory) {
            filename.append("/");
            if (color) {
                filename = XCopyConsole::high_yellow() + filename + XCopyConsole::reset();
            }
        } else if (file.isADF && color) {
            filename = XCopyConsole::high_green() + filename + XCopyConsole::reset();
        }

        // One buffer sized up front rather than a chain of + temporaries, each of
        // which allocated and copied the whole line so far.
        String line;
        line.reserve(file.date.length() + file.time.length() + filename.length() + 16);
        line += file.date;
        line += " ";
        line += file.time;
        line += " ";
        line += filesize;
        line += " ";
        line += filename;
        line += "\r\n";
        Log << line;
    }

    Log << "file count: " + String(_count) + "\r\n";

    delete _sdcard;

    return true;
}

void XCopyCommandLine::setCallBack(void* caller, OnWebCommand function)
{
    _caller = caller;
    _callback = function;
}

void XCopyCommandLine::setRawKeys(void* caller, OnRawKey function)
{
    _rawCaller = caller;
    _rawKeys = function;
}

void XCopyCommandLine::processKey(char key) {
    // A key screen has the console for the duration. Nothing below this runs, so
    // the line the editor holds is left exactly as the operator had it before they
    // started.
    if (_rawKeys != nullptr) {
        _rawKeys(_rawCaller, key);
        return;
    }

    _editor.key(key);
}

void XCopyCommandLine::processKeys(String keys) {
    keys.replace("\033[^M", "\r");
    keys.replace("\033[^J", "\n");
    keys.replace("\033[^H", char(0x08));

    /*
       The cursor keys used to be dropped here, whole sequence at a time, which is
       why the browser terminal had no history and no way back into a line. They go
       through now: the editor reassembles them from the characters, the same way
       it has to for the USB console, which delivers them one read at a time.
    */
    for(size_t i = 0; i < keys.length(); i++) {
        processKey(keys[i]);
    }
}

void XCopyCommandLine::setBusy(bool state) {
    String sstate = state ? "true" : "false";
    _callback(_caller, "setBusy," + sstate);
}

void XCopyCommandLine::Update()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        processKey(inChar);
    }
}
