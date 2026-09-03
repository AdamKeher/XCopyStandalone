#include "XCopyCommandTable.h"

/*
   Option lists.

   One array per command that has options, named for it, so the table below reads
   as a list of commands rather than a list of arrays. Commands with no options
   point at nullptr and carry a count of zero.

   Everything here is const, which on this part means it is in flash rather than
   RAM - .data is under a kilobyte for the whole firmware - so the table costs
   program space, of which there is some, and not the 6KB of headroom the stack
   and the heap are sharing.
*/

static const XCopyOption OPT_CP[] = {
    {"to", XCopyArgKind::path, nullptr, "where it goes; a trailing / keeps the name"},
};

static const XCopyOption OPT_MOUNT[] = {
    {"slot", XCopyArgKind::choice, "adf0|adf1", "which image slot to use, the first free one otherwise"},
    {"rw", XCopyArgKind::flag, nullptr, "allow writing; read only unless asked"},
};

static const XCopyOption OPT_MD5[] = {
    {"flash", XCopyArgKind::flag, nullptr, "hash the flash image instead of a file"},
};

static const XCopyOption OPT_READADF[] = {
    {"file", XCopyArgKind::path, nullptr, "destination, named after the disk if left out"},
};

static const XCopyOption OPT_READSCP[] = {
    {"cyls", XCopyArgKind::text, nullptr, "cylinder range, <first>-<last>"},
    {"revs", XCopyArgKind::number, nullptr, "revolutions to capture per track"},
    {"file", XCopyArgKind::path, nullptr, "destination, named after the disk if left out"},
};

static const XCopyOption OPT_WRITEBIN[] = {
    {"file", XCopyArgKind::path, nullptr, "binary file to write"},
    {"block", XCopyArgKind::number, nullptr, "first block to write it to"},
};

static const XCopyOption OPT_MODRIP[] = {
    {"block", XCopyArgKind::number, nullptr, "block the module starts in"},
    {"offset", XCopyArgKind::number, nullptr, "byte offset within that block"},
    {"size", XCopyArgKind::number, nullptr, "total bytes to rip"},
};

static const XCopyOption OPT_BOOT[] = {
    {"flash", XCopyArgKind::flag, nullptr, "read the boot block from flash, not the disk"},
};

static const XCopyOption OPT_READ[] = {
    {"flash", XCopyArgKind::flag, nullptr, "read the track from flash, not the disk"},
};

static const XCopyOption OPT_RPM[] = {
    {"interval", XCopyArgKind::number, nullptr, "milliseconds between readings"},
};

static const XCopyOption OPT_SETTIME[] = {
    {"epoch", XCopyArgKind::number, nullptr, "set from this epoch value instead of NTP"},
};

static const XCopyOption OPT_CONNECT[] = {
    {"ssid", XCopyArgKind::text, nullptr, "network name"},
    {"pass", XCopyArgKind::text, nullptr, "network password"},
};

#define OPTS(a) a, (uint8_t)(sizeof(a) / sizeof(a[0]))
#define NOOPTS nullptr, 0

/*
   The table. Order is the order help prints within a category, so it is grouped by
   what an operator is doing rather than alphabetically.
*/
const XCopyCommandDef XCOPY_COMMANDS[] = {
    // id                    name          alias  category            options          subject                 subject name  flags                                          help
    {XCopyCmd::help, "help", "?", XCopyCat::general, NOOPTS, XCopyArgKind::text, "command", 0, "this help, or the options of one command"},
    {XCopyCmd::version, "version", "ver", XCopyCat::general, NOOPTS, XCopyArgKind::none, nullptr, 0, "XCopy version number"},
    {XCopyCmd::clear, "clear", "cls", XCopyCat::general, NOOPTS, XCopyArgKind::none, nullptr, 0, "clear screen"},
    {XCopyCmd::reboot, "reboot", nullptr, XCopyCat::general, NOOPTS, XCopyArgKind::none, nullptr, 0, "restart the device"},
    {XCopyCmd::config, "config", nullptr, XCopyCat::general, NOOPTS, XCopyArgKind::none, nullptr, 0, "show config settings"},
    {XCopyCmd::mem, "mem", nullptr, XCopyCat::general, NOOPTS, XCopyArgKind::none, nullptr, 0, "show memory stats"},

    /*
       dir, cat and mount lost XCOPY_NEEDS_SD when DF0: arrived: all three now
       work on a mounted floppy with no card in the machine at all. Nothing is
       lost by dropping it - each of them fails cleanly and says why, because the
       SD paths already check the card themselves and the ADF paths do not need it.
    */
    {XCopyCmd::dir, "dir", "ls", XCopyCat::files, NOOPTS, XCopyArgKind::path, "directory", 0, "list a directory, on the card or in a mounted image"},
    {XCopyCmd::cat, "cat", nullptr, XCopyCat::files, NOOPTS, XCopyArgKind::path, "file", XCOPY_SUBJECT_REQUIRED, "write the contents of a file to the terminal"},
    {XCopyCmd::rm, "rm", nullptr, XCopyCat::files, NOOPTS, XCopyArgKind::path, "file", XCOPY_NEEDS_SD | XCOPY_SUBJECT_REQUIRED, "delete a file, from the card or from a mounted image"},
    {XCopyCmd::md5, "md5", nullptr, XCopyCat::files, OPTS(OPT_MD5), XCopyArgKind::path, "file", 0, "md5 hash of a file on the SD card, or of the flash"},
    {XCopyCmd::cp, "cp", nullptr, XCopyCat::files, OPTS(OPT_CP), XCopyArgKind::path, "file", XCOPY_SUBJECT_REQUIRED, "copy a file, between the card and any mounted image"},
    {XCopyCmd::mkdir, "mkdir", nullptr, XCopyCat::files, NOOPTS, XCopyArgKind::path, "directory", XCOPY_SUBJECT_REQUIRED, "make a directory, on the card or in a mounted image"},
    {XCopyCmd::mount, "mount", nullptr, XCopyCat::files, OPTS(OPT_MOUNT), XCopyArgKind::path, "file", 0, "mount an ADF image or df0:, or list what is mounted"},
    {XCopyCmd::unmount, "unmount", nullptr, XCopyCat::files, NOOPTS, XCopyArgKind::text, "slot", 0, "release a slot, or all of them"},

    {XCopyCmd::readadf, "readadf", nullptr, XCopyCat::disk, OPTS(OPT_READADF), XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK | XCOPY_NEEDS_SD, "read the floppy disk to an ADF file"},
    {XCopyCmd::writeadf, "writeadf", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::path, "file", XCOPY_NEEDS_SD | XCOPY_SUBJECT_REQUIRED, "write an ADF file to the floppy disk"},
    {XCopyCmd::readscp, "readscp", nullptr, XCopyCat::disk, OPTS(OPT_READSCP), XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK | XCOPY_NEEDS_SD, "read the floppy disk to an SCP flux image"},
    {XCopyCmd::writeflash, "writeflash", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::none, nullptr, 0, "read the floppy disk into flash memory"},
    {XCopyCmd::writebin, "writebin", nullptr, XCopyCat::disk, OPTS(OPT_WRITEBIN), XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK, "write a binary file to the disk from a given block"},
    {XCopyCmd::live, "live", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::none, nullptr, 0, "hand this USB session to a host over the live link"},
    {XCopyCmd::testdisk, "testdisk", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK, "test the floppy disk"},
    {XCopyCmd::scanblocks, "scanblocks", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK, "scan the floppy disk for free blocks"},
    {XCopyCmd::search, "search", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::text, "text", XCOPY_NEEDS_DISK | XCOPY_RAW_TAIL | XCOPY_SUBJECT_REQUIRED, "search the disk for case sensitive ascii text"},
    {XCopyCmd::modsearch, "modsearch", nullptr, XCopyCat::disk, NOOPTS, XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK, "search the disk for tracker modules"},
    {XCopyCmd::modrip, "modrip", nullptr, XCopyCat::disk, OPTS(OPT_MODRIP), XCopyArgKind::none, nullptr, 0, "rip a tracker module out of the disk"},

    {XCopyCmd::boot, "boot", nullptr, XCopyCat::analysis, OPTS(OPT_BOOT), XCopyArgKind::none, nullptr, 0, "print and identify the boot block"},
    {XCopyCmd::hist, "hist", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::none, nullptr, 0, "print a histogram of the track in ascii"},
    {XCopyCmd::rpm, "rpm", nullptr, XCopyCat::analysis, OPTS(OPT_RPM), XCopyArgKind::none, nullptr, XCOPY_NEEDS_DISK, "drive speed from the index, until a key is pressed"},
    {XCopyCmd::headcal, "headcal", "hc", XCopyCat::analysis, NOOPTS, XCopyArgKind::number, "cylinder", 0, "continuous head calibration test, ATK style"},
    {XCopyCmd::name, "name", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::none, nullptr, 0, "read track 80 and return the disk label in ascii"},
    {XCopyCmd::print, "print", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::none, nullptr, 0, "print the amiga track with its header"},
    {XCopyCmd::read, "read", nullptr, XCopyCat::analysis, OPTS(OPT_READ), XCopyArgKind::number, "track", 0, "read a logical track into the buffer"},
    {XCopyCmd::dump, "dump", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::path, "file", XCOPY_NEEDS_SD | XCOPY_SUBJECT_REQUIRED, "describe an ADF image and list its root directory"},
    {XCopyCmd::vol, "vol", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::path, "file", XCOPY_NEEDS_SD | XCOPY_SUBJECT_REQUIRED, "describe an ADF image without listing it"},
    {XCopyCmd::weak, "weak", nullptr, XCopyCat::analysis, NOOPTS, XCopyArgKind::none, nullptr, 0, "retry number for the last read, in binary"},

    {XCopyCmd::time, "time", nullptr, XCopyCat::time, NOOPTS, XCopyArgKind::none, nullptr, 0, "show the current date and time"},
    {XCopyCmd::settime, "settime", nullptr, XCopyCat::time, OPTS(OPT_SETTIME), XCopyArgKind::none, nullptr, 0, "set the date and time from NTP, or from an epoch"},
    {XCopyCmd::timezone, "timezone", nullptr, XCopyCat::time, NOOPTS, XCopyArgKind::number, "-12..12", 0, "show the time zone, or set it"},

    {XCopyCmd::connect, "connect", nullptr, XCopyCat::network, OPTS(OPT_CONNECT), XCopyArgKind::none, nullptr, 0, "connect to a wifi network"},
    {XCopyCmd::clearwifi, "clearwifi", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "clear the wifi settings from the configuration"},
    {XCopyCmd::status, "status", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "show the wifi status"},
    {XCopyCmd::ip, "ip", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "show the wifi ip address"},
    {XCopyCmd::mac, "mac", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "show the wifi mac address"},
    {XCopyCmd::ssid, "ssid", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "show the wifi ssid"},
    {XCopyCmd::websocket, "websocket", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::text, "message", XCOPY_RAW_TAIL | XCOPY_SUBJECT_REQUIRED, "broadcast a message to web clients"},
    {XCopyCmd::scan, "scan", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "scan for wireless networks"},
    {XCopyCmd::ping, "ping", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "ping the ESP"},
    {XCopyCmd::pass, "pass", nullptr, XCopyCat::network, NOOPTS, XCopyArgKind::none, nullptr, 0, "enter ESP passthrough mode"},
};

const uint8_t XCOPY_COMMAND_COUNT = (uint8_t)(sizeof(XCOPY_COMMANDS) / sizeof(XCOPY_COMMANDS[0]));

static bool equalsIgnoreCase(const char *a, const String &b)
{
    return b.equalsIgnoreCase(a);
}

const XCopyCommandDef *xcopyFindCommand(const String &name)
{
    for (uint8_t i = 0; i < XCOPY_COMMAND_COUNT; i++)
    {
        const XCopyCommandDef &entry = XCOPY_COMMANDS[i];
        if (equalsIgnoreCase(entry.name, name))
            return &entry;
        if (entry.alias != nullptr && equalsIgnoreCase(entry.alias, name))
            return &entry;
    }

    return nullptr;
}

const XCopyOption *xcopyFindOption(const XCopyCommandDef *command, const String &name)
{
    if (command == nullptr)
        return nullptr;

    for (uint8_t i = 0; i < command->optionCount; i++)
        if (equalsIgnoreCase(command->options[i].name, name))
            return &command->options[i];

    return nullptr;
}

const char *xcopyCategoryName(XCopyCat category)
{
    switch (category)
    {
    case XCopyCat::general:
        return "General";
    case XCopyCat::files:
        return "SD card";
    case XCopyCat::disk:
        return "Floppy disk";
    case XCopyCat::analysis:
        return "Analysis";
    case XCopyCat::time:
        return "Date and time";
    default:
        return "Network";
    }
}

const char *xcopyKindName(XCopyArgKind kind)
{
    switch (kind)
    {
    case XCopyArgKind::number:
        return "<n>";
    case XCopyArgKind::path:
        return "<path>";
    case XCopyArgKind::choice:
        return "<choice>";
    case XCopyArgKind::text:
        return "<text>";
    default:
        return "";
    }
}

bool xcopyChoiceValid(const XCopyOption *option, const String &value)
{
    if (option == nullptr || option->choices == nullptr)
        return true;

    // Walked in place rather than split into Strings: this runs on every parse of
    // a choice option and the list is a handful of short words.
    const char *walk = option->choices;
    while (*walk != 0)
    {
        const char *end = walk;
        while (*end != 0 && *end != '|')
            end++;

        const size_t length = (size_t)(end - walk);
        if (length == value.length() && strncasecmp(walk, value.c_str(), length) == 0)
            return true;

        walk = (*end == '|') ? end + 1 : end;
    }

    return false;
}
