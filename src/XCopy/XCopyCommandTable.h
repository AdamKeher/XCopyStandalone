#ifndef XCOPYCOMMANDTABLE_H
#define XCOPYCOMMANDTABLE_H

#include <Arduino.h>

/*
   The one description of what the console understands.

   Dispatch, argument validation, the help screen and tab completion are all read
   out of the table below. They used to be four separate statements of the same
   facts: a 45 arm if-chain of string compares, a hand written help block that
   nothing checked against it, and per command argument parsing that each handler
   invented for itself. The help had already drifted - "bootf" was documented,
   listed, and did nothing, because its handler set up the arguments for "boot" and
   then returned.

   Grammar:

       <command> [subject] [-option [value]] ...

   One optional subject per command, everything else named. A token is an option
   only if it is a dash followed by a letter, so "timezone -10" is a negative
   number and not an unknown option. Values with spaces are quoted; completion
   inserts the quotes, so they are rarely typed.
*/

//! What an option or a subject holds.
enum class XCopyArgKind : uint8_t
{
    none = 0, //!< as a subject: the command takes none
    flag,     //!< option only: present or absent, never has a value
    number,
    text,
    path,   //!< a path on the SD card, and what completion offers files for
    choice, //!< one of the pipe separated words in XCopyOption::choices
};

struct XCopyOption
{
    const char *name; //!< without the leading dash
    XCopyArgKind kind;
    const char *choices; //!< "a|b|c" for kind::choice, nullptr otherwise
    const char *help;
};

//! Help groups, printed in this order.
enum class XCopyCat : uint8_t
{
    general = 0,
    files,
    disk,
    analysis,
    time,
    network,
    count
};

//! Checked before the handler runs, so no handler repeats them.
static const uint8_t XCOPY_NEEDS_DISK = 0x01; //!< a floppy has to be in the drive
static const uint8_t XCOPY_NEEDS_SD = 0x02;   //!< the SD card has to be mounted
/*
   The subject is the whole of the rest of the line, unparsed - no options, no
   quote handling. For the two commands that take arbitrary text: a search string
   and a websocket message both legitimately contain dashes, quotes and spaces.
*/
static const uint8_t XCOPY_RAW_TAIL = 0x04;
static const uint8_t XCOPY_SUBJECT_REQUIRED = 0x08;

enum class XCopyCmd : uint8_t
{
    help,
    version,
    clear,
    reboot,
    config,
    mem,

    dir,
    cat,
    rm,
    md5,
    cp,
    mkdir,
    mount,
    unmount,

    readadf,
    writeadf,
    readscp,
    writeflash,
    writebin,
    live,
    testdisk,
    scanblocks,
    search,
    modsearch,
    modrip,

    boot,
    hist,
    rpm,
    headcal,
    diskinfo,
    drivetoolkit,
    name,
    print,
    read,
    dump,
    vol,
    weak,

    time,
    settime,
    timezone,

    connect,
    clearwifi,
    status,
    ip,
    mac,
    ssid,
    rssi,
    websocket,
    scan,
    ping,
    pass,

    count
};

struct XCopyCommandDef
{
    XCopyCmd id;
    const char *name;
    const char *alias; //!< nullptr when the command has none
    XCopyCat category;
    const XCopyOption *options;
    uint8_t optionCount;
    XCopyArgKind subject;
    const char *subjectName; //!< what the subject is called in help, nullptr for none
    uint8_t flags;
    const char *help;
};

extern const XCopyCommandDef XCOPY_COMMANDS[];
extern const uint8_t XCOPY_COMMAND_COUNT;

//! Name or alias, case insensitive. nullptr when there is no such command.
const XCopyCommandDef *xcopyFindCommand(const String &name);
//! nullptr when @p command has no option of that name.
const XCopyOption *xcopyFindOption(const XCopyCommandDef *command, const String &name);
//! The category headings, indexed by XCopyCat.
const char *xcopyCategoryName(XCopyCat category);
//! "<n>", "<text>", "<path>" - what to show in help for a value of this kind.
const char *xcopyKindName(XCopyArgKind kind);
//! True if @p value is one of @p option's pipe separated choices.
bool xcopyChoiceValid(const XCopyOption *option, const String &value);

#endif // XCOPYCOMMANDTABLE_H
