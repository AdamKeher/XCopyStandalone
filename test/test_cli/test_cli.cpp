/*
   The console interpreter, on a host.

   Everything here runs against the real XCopyCommandTable, XCopyArgs,
   XCopyLineEditor and XCopyComplete - not copies of them - because those four
   depend on nothing but Arduino's String. The terminal and the SD card arrive as
   function pointers (see XCopyConsoleIO.h), so this file supplies a capture
   buffer for one and a fixed fake directory tree for the other.

   What is worth testing here is exactly what is hard to check by hand on the
   device: the shapes a command line can take, the error paths, and the corners of
   the completer and the editor. Anything that needs a floppy drive is not here
   and cannot be.
*/

#include <unity.h>
#include <string>
#include <vector>

#include "XCopyCommandTable.h"
#include "XCopyArgs.h"
#include "XCopyLineEditor.h"
#include "XCopyComplete.h"
#include "XCopyAdfPath.h"

// THE FAKE WORLD

static std::string captured;
static void capture(const String &text) { captured += text.str(); }

struct FakeEntry
{
    const char *directory;
    const char *name;
    bool isDirectory;
};

// Deliberately contains names with spaces and a nested directory: those are the
// two cases path completion gets wrong if it is careless.
static const FakeEntry FAKE_TREE[] = {
    {"/", "adf", true},
    {"/", "scp", true},
    {"/", "readme.txt", false},
    {"/", "config.txt", false},
    {"/adf/", "Workbench.adf", false},
    {"/adf/", "Work Disk.adf", false},
    {"/adf/", "Deluxe Paint.adf", false},
    {"/adf/", "games", true},
    {"/adf/games/", "Lemmings.adf", false},

    // A mounted image, so completion of a device qualified path is covered. The
    // real lister routes on the device prefix and reads the volume; here it is one
    // more directory name, which is exactly the point - the completer does not
    // know there are two kinds of place a path can lead.
    {"ADF0:", "c", true},
    {"ADF0:", "devs", true},
    {"ADF0:", "startup.txt", false},
    {"ADF0:c/", "list", false},
    {"ADF0:c/", "loadwb", false},
};

static void fakeLister(const String &directory, XCopyDirVisit visit, void *context)
{
    for (unsigned i = 0; i < sizeof(FAKE_TREE) / sizeof(FAKE_TREE[0]); i++)
        if (directory == FAKE_TREE[i].directory)
            visit(context, String(FAKE_TREE[i].name), FAKE_TREE[i].isDirectory);
}

static std::vector<std::string> submitted;
static void onLine(void *, const String &line) { submitted.push_back(line.str()); }
static void onComplete(void *, uint8_t presses) { captured += "<TAB" + std::to_string(presses) + ">"; }

void setUp(void)
{
    captured.clear();
    submitted.clear();
}

void tearDown(void) {}

// HELPERS

//! Runs a line through the table and the parser and renders what came out.
static std::string parse(const char *line)
{
    String whole(line);
    String name = whole;
    String tail("");
    const int space = whole.indexOf(' ');
    if (space > 0)
    {
        name = whole.substring(0, space);
        tail = whole.substring(space + 1);
    }

    const XCopyCommandDef *def = xcopyFindCommand(name);
    if (def == nullptr)
        return "unknown command";

    String error;
    XCopyArgs args;
    if (!args.parse(def, tail, error))
        return "error";

    std::string out;
    if (args.hasSubject())
        out = "[" + args.subject().str() + "]";
    for (uint8_t i = 0; i < def->optionCount; i++)
    {
        const char *option = def->options[i].name;
        if (!args.has(option))
            continue;
        if (!out.empty())
            out += " ";
        out += std::string("-") + option + "=" + args.text(option).str();
    }

    return out.empty() ? "(nothing)" : out;
}

//! Presses Tab on a line and returns what the line became.
static std::string complete(const char *line, uint8_t presses = 1)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);
    for (const char *c = line; *c; c++)
        editor.key(*c);

    XCopyCompleter completer;
    completer.begin(fakeLister);
    captured.clear();
    completer.complete(editor, presses);

    return editor.line().str();
}

//! Types a string into an editor one character at a time, as a terminal does.
static void type(XCopyLineEditor &editor, const char *keys)
{
    for (const char *k = keys; *k; k++)
        editor.key(*k);
}

#define ESC "\033"
#define K_UP ESC "[A"
#define K_DOWN ESC "[B"
#define K_RIGHT ESC "[C"
#define K_LEFT ESC "[D"
#define K_HOME ESC "[H"
#define K_END ESC "[F"
#define K_DEL ESC "[3~"

// THE TABLE

static void test_every_command_is_reachable_by_name(void)
{
    for (uint8_t i = 0; i < XCOPY_COMMAND_COUNT; i++)
    {
        const XCopyCommandDef &entry = XCOPY_COMMANDS[i];
        TEST_ASSERT_EQUAL_PTR(&entry, xcopyFindCommand(String(entry.name)));
        if (entry.alias != nullptr)
            TEST_ASSERT_EQUAL_PTR(&entry, xcopyFindCommand(String(entry.alias)));
    }
}

static void test_command_lookup_ignores_case(void)
{
    TEST_ASSERT_NOT_NULL(xcopyFindCommand(String("READSCP")));
    TEST_ASSERT_NOT_NULL(xcopyFindCommand(String("ReadScp")));
    TEST_ASSERT_NULL(xcopyFindCommand(String("readsc")));
}

/*
   The help table is drawn in a fixed width frame and pads by truncating, so an
   entry that outgrows its column would be silently cut in half rather than break
   the box. Caught here instead.
*/
static void test_help_text_fits_its_column(void)
{
    for (uint8_t i = 0; i < XCOPY_COMMAND_COUNT; i++)
    {
        const XCopyCommandDef &entry = XCOPY_COMMANDS[i];

        String signature = entry.name;
        if (entry.alias != nullptr)
            signature = signature + " | " + entry.alias;
        if (entry.subject != XCopyArgKind::none && entry.subjectName != nullptr)
            signature = signature + " <" + entry.subjectName + ">";
        if (entry.optionCount > 0)
            signature += " [-options]";

        TEST_ASSERT_TRUE_MESSAGE(signature.length() <= 31, entry.name);
        TEST_ASSERT_TRUE_MESSAGE(strlen(entry.help) <= 52, entry.name);
    }
}

// PARSING

static void test_parse_bare_commands(void)
{
    TEST_ASSERT_EQUAL_STRING("(nothing)", parse("version").c_str());
    TEST_ASSERT_EQUAL_STRING("(nothing)", parse("ver").c_str());
    TEST_ASSERT_EQUAL_STRING("unknown command", parse("nosuchcommand").c_str());
}

static void test_parse_subjects(void)
{
    TEST_ASSERT_EQUAL_STRING("[readme.txt]", parse("cat readme.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("[12]", parse("read 12").c_str());
    TEST_ASSERT_EQUAL_STRING("error", parse("cat").c_str());       // required
    TEST_ASSERT_EQUAL_STRING("error", parse("read abc").c_str());  // must be a number
    TEST_ASSERT_EQUAL_STRING("error", parse("cat a b").c_str());   // one only
    TEST_ASSERT_EQUAL_STRING("error", parse("readscp extra").c_str()); // takes none
}

static void test_parse_quoted_values(void)
{
    TEST_ASSERT_EQUAL_STRING("[/adf/Work Disk.adf]", parse("cat \"/adf/Work Disk.adf\"").c_str());
    TEST_ASSERT_EQUAL_STRING("-file=20260831 1830 Workbench.scp",
                             parse("readscp -file \"20260831 1830 Workbench.scp\"").c_str());
    // The whole point of naming the options: an SSID with a space in it was
    // unreachable from the console before, with no message saying so.
    TEST_ASSERT_EQUAL_STRING("-ssid=my net -pass=p a s s",
                             parse("connect -ssid \"my net\" -pass \"p a s s\"").c_str());
}

/*
   A dash followed by a digit is a value, not an option. Without this rule
   "timezone -10" is an unknown option, and the rule has to live in the tokenizer
   or every handler needs to know about it.
*/
static void test_parse_negative_numbers_are_not_options(void)
{
    TEST_ASSERT_EQUAL_STRING("[-10]", parse("timezone -10").c_str());
    TEST_ASSERT_EQUAL_STRING("[10]", parse("timezone 10").c_str());
}

static void test_parse_options_in_any_order(void)
{
    TEST_ASSERT_EQUAL_STRING("[12] -flash=1", parse("read 12 -flash").c_str());
    TEST_ASSERT_EQUAL_STRING("[12] -flash=1", parse("read -flash 12").c_str());
    // parse() renders in table order, so the two lines below coming out identical
    // is the assertion: the order they were typed in did not survive, and did not
    // need to.
    TEST_ASSERT_EQUAL_STRING("-cyls=0-83 -revs=3 -file=my.scp",
                             parse("readscp -cyls 0-83 -revs 3 -file my.scp").c_str());
    TEST_ASSERT_EQUAL_STRING("-cyls=0-83 -revs=3 -file=my.scp",
                             parse("readscp -file my.scp -revs 3 -cyls 0-83").c_str());
}

/*
   diskinfo, which is the one command with two shapes: -file analyses an image and
   wants no disk, everything else reads the drive and wants no card. The table
   carries neither NEEDS_DISK nor NEEDS_SD because of it, so what is checked here
   is that the grammar still holds the two apart.
*/
static void test_diskinfo_options(void)
{
    TEST_ASSERT_NOT_NULL(xcopyFindCommand(String("diskinfo")));
    TEST_ASSERT_NOT_NULL(xcopyFindCommand(String("di")));

    TEST_ASSERT_EQUAL_STRING("(nothing)", parse("diskinfo").c_str()); // the drive, whole disk
    TEST_ASSERT_EQUAL_STRING("-cyls=0-83", parse("diskinfo -cyls 0-83").c_str());
    TEST_ASSERT_EQUAL_STRING("-side=1", parse("diskinfo -side 1").c_str());
    TEST_ASSERT_EQUAL_STRING("-side=both", parse("diskinfo -side both").c_str());
    TEST_ASSERT_EQUAL_STRING("-file=a.scp", parse("diskinfo -file a.scp").c_str());

    // Table order again, not the order they were typed in.
    TEST_ASSERT_EQUAL_STRING("-cyls=0-83 -side=0 -file=a.scp",
                             parse("diskinfo -file a.scp -side 0 -cyls 0-83").c_str());

    // -side is a choice, so the table refuses anything outside the list before any
    // handler sees it. That is what lets cmdDiskInfo() treat "not 0 and not 1" as
    // "both" without having to distinguish it from a typo.
    TEST_ASSERT_EQUAL_STRING("error", parse("diskinfo -side 2").c_str());
    TEST_ASSERT_EQUAL_STRING("error", parse("diskinfo -side upper").c_str());
    TEST_ASSERT_EQUAL_STRING("error", parse("diskinfo -bogus 1").c_str());
    TEST_ASSERT_EQUAL_STRING("error", parse("diskinfo extra").c_str()); // takes no subject
}

static void test_diskinfo_completion(void)
{
    // "di" is the alias and so completes to itself, not onward to "diskinfo";
    // "dis" has only the one candidate.
    TEST_ASSERT_EQUAL_STRING("diskinfo ", complete("dis").c_str());
    TEST_ASSERT_EQUAL_STRING("diskinfo -cyls ", complete("diskinfo -c").c_str());
    TEST_ASSERT_EQUAL_STRING("diskinfo -side ", complete("diskinfo -si").c_str());
    TEST_ASSERT_EQUAL_STRING("diskinfo -file ", complete("diskinfo -f").c_str());
    TEST_ASSERT_EQUAL_STRING("diskinfo -", complete("diskinfo -").c_str()); // three, nothing to add
}

static void test_parse_rejects_bad_options(void)
{
    TEST_ASSERT_EQUAL_STRING("error", parse("readscp -bogus 1").c_str());          // no such option
    TEST_ASSERT_EQUAL_STRING("error", parse("readscp -revs").c_str());             // no value
    TEST_ASSERT_EQUAL_STRING("error", parse("readscp -revs -file a.scp").c_str()); // option as a value
    TEST_ASSERT_EQUAL_STRING("error", parse("readscp -revs x").c_str());           // not a number
}

/*
   A search string and a websocket message are arbitrary text. Neither the dashes
   nor the commas in them are grammar.
*/
static void test_parse_raw_tail_commands(void)
{
    TEST_ASSERT_EQUAL_STRING("[-notanoption at all]", parse("search -notanoption at all").c_str());
    TEST_ASSERT_EQUAL_STRING("[hello, world]", parse("websocket hello, world").c_str());
}

static void test_parse_multi_option_commands(void)
{
    TEST_ASSERT_EQUAL_STRING("-block=880 -offset=12 -size=40000",
                             parse("modrip -block 880 -offset 12 -size 40000").c_str());
    TEST_ASSERT_EQUAL_STRING("-file=fw.bin -block=880",
                             parse("writebin -file fw.bin -block 880").c_str());
    TEST_ASSERT_EQUAL_STRING("-flash=1", parse("md5 -flash").c_str());
    TEST_ASSERT_EQUAL_STRING("[file.adf]", parse("md5 file.adf").c_str());
}

// COMPLETION

static void test_complete_command_names(void)
{
    TEST_ASSERT_EQUAL_STRING("readscp ", complete("readsc").c_str());
    TEST_ASSERT_EQUAL_STRING("testdisk ", complete("testd").c_str());
    TEST_ASSERT_EQUAL_STRING("hc ", complete("hc").c_str());       // an alias completes too
    TEST_ASSERT_EQUAL_STRING("write", complete("wri").c_str());    // three matches, common prefix
    TEST_ASSERT_EQUAL_STRING("mod", complete("mod").c_str());      // already at the common prefix
    TEST_ASSERT_EQUAL_STRING("zzz", complete("zzz").c_str());      // no match, line untouched
}

static void test_complete_option_names(void)
{
    TEST_ASSERT_EQUAL_STRING("readscp -cyls ", complete("readscp -c").c_str());
    TEST_ASSERT_EQUAL_STRING("read 12 -flash ", complete("read 12 -f").c_str());
    TEST_ASSERT_EQUAL_STRING("connect -ssid ", complete("connect -s").c_str());
    TEST_ASSERT_EQUAL_STRING("readscp -", complete("readscp -").c_str()); // three, nothing to add
}

//! A command with no subject answers a bare Tab with what it does take.
static void test_complete_offers_options_when_there_is_no_subject(void)
{
    TEST_ASSERT_EQUAL_STRING("readscp -", complete("readscp ").c_str());
}

static void test_complete_paths(void)
{
    TEST_ASSERT_EQUAL_STRING("cat readme.txt ", complete("cat rea").c_str());
    TEST_ASSERT_EQUAL_STRING("cat /adf/Workbench.adf ", complete("cat /adf/Workb").c_str());
    TEST_ASSERT_EQUAL_STRING("cat /adf/Work", complete("cat /adf/W").c_str()); // two share this much
    TEST_ASSERT_EQUAL_STRING("cat /adf/games/Lemmings.adf ", complete("cat /adf/games/").c_str());
}

//! A directory gets a slash and no space, so the next Tab carries on inside it.
static void test_complete_directories_stay_open(void)
{
    TEST_ASSERT_EQUAL_STRING("cat /adf/games/", complete("cat /adf/g").c_str());
    // And a path typed without a leading slash is completed without one, or the
    // candidate would not start with what was typed and nothing would match.
    TEST_ASSERT_EQUAL_STRING("cat adf/", complete("cat ad").c_str());
}

static void test_complete_quotes_names_with_spaces(void)
{
    TEST_ASSERT_EQUAL_STRING("cat \"/adf/Deluxe Paint.adf\" ", complete("cat /adf/D").c_str());
    TEST_ASSERT_EQUAL_STRING("cat \"/adf/Deluxe Paint.adf\" ", complete("cat \"/adf/D").c_str());
}

static void test_second_press_lists_the_candidates(void)
{
    complete("readscp -", 2);
    TEST_ASSERT_TRUE(captured.find("-cyls") != std::string::npos);
    TEST_ASSERT_TRUE(captured.find("-revs") != std::string::npos);
    TEST_ASSERT_TRUE(captured.find("-file") != std::string::npos);

    complete("cat /adf/Work", 2);
    TEST_ASSERT_TRUE(captured.find("Workbench.adf") != std::string::npos);
    TEST_ASSERT_TRUE(captured.find("Work Disk.adf") != std::string::npos);
}

//! The first press must not print a list, only narrow the line.
static void test_first_press_prints_nothing(void)
{
    complete("readscp -", 1);
    TEST_ASSERT_TRUE(captured.find("-cyls") == std::string::npos);
}

// THE LINE EDITOR

static void test_editor_types_and_edits(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "readscp");
    TEST_ASSERT_EQUAL_STRING("readscp", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT16(7, editor.cursor());

    type(editor, K_LEFT K_LEFT);
    TEST_ASSERT_EQUAL_UINT16(5, editor.cursor());

    type(editor, "XY");
    TEST_ASSERT_EQUAL_STRING("readsXYcp", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT16(7, editor.cursor());

    type(editor, "\x08\x08");
    TEST_ASSERT_EQUAL_STRING("readscp", editor.line().c_str());

    type(editor, K_DEL);
    TEST_ASSERT_EQUAL_STRING("readsp", editor.line().c_str());

    type(editor, K_HOME);
    TEST_ASSERT_EQUAL_UINT16(0, editor.cursor());
    type(editor, K_END);
    TEST_ASSERT_EQUAL_UINT16(6, editor.cursor());

    // 0x7f, which is what most real terminals send for backspace and what the
    // editor this replaced ignored.
    type(editor, "\x7f");
    TEST_ASSERT_EQUAL_STRING("reads", editor.line().c_str());
}

static void test_editor_cursor_stops_at_both_ends(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "abc" K_HOME K_LEFT K_LEFT);
    TEST_ASSERT_EQUAL_UINT16(0, editor.cursor());
    type(editor, K_END K_RIGHT K_RIGHT);
    TEST_ASSERT_EQUAL_UINT16(3, editor.cursor());
}

static void test_editor_runs_a_line_and_clears(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "version\r");
    TEST_ASSERT_EQUAL_UINT(1, submitted.size());
    TEST_ASSERT_EQUAL_STRING("version", submitted[0].c_str());
    TEST_ASSERT_EQUAL_STRING("", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT16(0, editor.cursor());
}

static void test_editor_history(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "one\r");
    type(editor, "two\r");
    type(editor, "two\r"); // a repeat is not stored twice
    type(editor, "three\r");

    type(editor, K_UP);
    TEST_ASSERT_EQUAL_STRING("three", editor.line().c_str());
    type(editor, K_UP);
    TEST_ASSERT_EQUAL_STRING("two", editor.line().c_str());
    type(editor, K_UP);
    TEST_ASSERT_EQUAL_STRING("one", editor.line().c_str());
    type(editor, K_UP);
    TEST_ASSERT_EQUAL_STRING("one", editor.line().c_str()); // stops at the oldest

    type(editor, K_DOWN K_DOWN);
    TEST_ASSERT_EQUAL_STRING("three", editor.line().c_str());
    type(editor, K_DOWN);
    TEST_ASSERT_EQUAL_STRING("", editor.line().c_str());
    type(editor, K_DOWN);
    TEST_ASSERT_EQUAL_STRING("", editor.line().c_str()); // and stays there
}

//! Walking back through the history must not lose the line being written.
static void test_editor_keeps_a_half_typed_line(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "first\r");
    type(editor, "half");
    type(editor, K_UP);
    TEST_ASSERT_EQUAL_STRING("first", editor.line().c_str());
    type(editor, K_DOWN);
    TEST_ASSERT_EQUAL_STRING("half", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT16(4, editor.cursor());
}

static void test_editor_ctrl_c_abandons_the_line(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "abandon\x03");
    TEST_ASSERT_EQUAL_STRING("", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT(0, submitted.size());
}

/*
   An escape sequence the editor does not understand has to be swallowed whole.
   Letting its characters through would type "[5~" into the line, which is what
   made the arrow keys unusable before there was a state machine to catch them.
*/
static void test_editor_swallows_unknown_escapes(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "ab" ESC "[5~" "cd");
    TEST_ASSERT_EQUAL_STRING("abcd", editor.line().c_str());
    type(editor, ESC "Z");
    TEST_ASSERT_EQUAL_STRING("abcd", editor.line().c_str());
    type(editor, "\x0b\x0c"); // stray control characters are not printable
    TEST_ASSERT_EQUAL_STRING("abcd", editor.line().c_str());
}

static void test_editor_counts_tab_presses(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "cat\t");
    TEST_ASSERT_TRUE(captured.find("<TAB1>") != std::string::npos);
    type(editor, "\t");
    TEST_ASSERT_TRUE(captured.find("<TAB2>") != std::string::npos);

    captured.clear();
    type(editor, "x\t");
    TEST_ASSERT_TRUE(captured.find("<TAB1>") != std::string::npos);
}

/*
   A redraw is one write, because XCopyLog sleeps 6ms per call: it returns to the
   margin, reprints, erases whatever the line used to be longer by, and parks the
   cursor at the prompt width plus the offset, in 1 based columns.
*/
static void test_editor_redraw_is_one_write(void)
{
    XCopyLineEditor editor;
    editor.begin(nullptr, onLine, onComplete, capture);

    type(editor, "hello");
    editor.replace(0, 5, String("goodbye "));
    TEST_ASSERT_EQUAL_STRING("goodbye ", editor.line().c_str());
    TEST_ASSERT_EQUAL_UINT16(8, editor.cursor());

    captured.clear();
    editor.redraw();
    TEST_ASSERT_TRUE(captured.find("\033[K") != std::string::npos);
    TEST_ASSERT_TRUE(captured.find("\033[12G") != std::string::npos);
}


// PATH GRAMMAR
//
// One grammar covers the card and the mounted images, and it is the thing standing
// between a typed path and the wrong file being opened. Every shape it can take is
// cheap to check here and expensive to check anywhere else.

static void split(const char *text, XCopyPath &out)
{
    TEST_ASSERT_TRUE_MESSAGE(xcopySplitPath(String(text), out), text);
}

void test_path_without_a_device_is_the_card(void)
{
    XCopyPath p;
    split("/adfs/workbench.adf", p);
    TEST_ASSERT_FALSE(p.qualified);
    TEST_ASSERT_TRUE(p.isCard());
    TEST_ASSERT_EQUAL_STRING("/adfs/workbench.adf", p.rest.str().c_str());
}

void test_path_with_a_device(void)
{
    XCopyPath p;
    split("ADF0:c/list", p);
    TEST_ASSERT_TRUE(p.qualified);
    TEST_ASSERT_FALSE(p.isCard());
    TEST_ASSERT_EQUAL_STRING("ADF0", p.device.str().c_str());
    TEST_ASSERT_EQUAL_STRING("c/list", p.rest.str().c_str());
}

void test_device_name_is_upper_cased(void)
{
    XCopyPath p;
    split("adf1:s/startup-sequence", p);
    TEST_ASSERT_EQUAL_STRING("ADF1", p.device.str().c_str());
    TEST_ASSERT_EQUAL_STRING("s/startup-sequence", p.rest.str().c_str());
}

void test_bare_device_has_an_empty_path(void)
{
    XCopyPath p;
    split("ADF0:", p);
    TEST_ASSERT_TRUE(p.qualified);
    TEST_ASSERT_EQUAL_STRING("", p.rest.str().c_str());
}

void test_sd_is_a_device_that_means_the_card(void)
{
    XCopyPath p;
    split("SD:/adfs", p);
    TEST_ASSERT_TRUE(p.qualified);
    TEST_ASSERT_TRUE(p.isCard());
    TEST_ASSERT_EQUAL_STRING("/adfs", p.rest.str().c_str());
}

void test_a_colon_in_a_filename_is_not_a_device(void)
{
    // A FAT name can hold a colon where a device name cannot, so anything that is
    // not letters and digits before the colon leaves the whole string a card path.
    XCopyPath p;
    split("my notes:draft/x.txt", p);
    TEST_ASSERT_FALSE(p.qualified);
    TEST_ASSERT_EQUAL_STRING("my notes:draft/x.txt", p.rest.str().c_str());
}

void test_a_leading_colon_names_nothing(void)
{
    XCopyPath p;
    TEST_ASSERT_FALSE(xcopySplitPath(String(":oops"), p));
}

void test_components_come_off_one_at_a_time(void)
{
    String path = "c/utilities/more";
    String component;

    TEST_ASSERT_TRUE(xcopyNextComponent(path, component));
    TEST_ASSERT_EQUAL_STRING("c", component.str().c_str());
    TEST_ASSERT_TRUE(xcopyNextComponent(path, component));
    TEST_ASSERT_EQUAL_STRING("utilities", component.str().c_str());
    TEST_ASSERT_TRUE(xcopyNextComponent(path, component));
    TEST_ASSERT_EQUAL_STRING("more", component.str().c_str());
    TEST_ASSERT_FALSE(xcopyNextComponent(path, component));
}

void test_repeated_separators_are_skipped(void)
{
    String path = "//c//list//";
    String component;

    TEST_ASSERT_TRUE(xcopyNextComponent(path, component));
    TEST_ASSERT_EQUAL_STRING("c", component.str().c_str());
    TEST_ASSERT_TRUE(xcopyNextComponent(path, component));
    TEST_ASSERT_EQUAL_STRING("list", component.str().c_str());
    TEST_ASSERT_FALSE(xcopyNextComponent(path, component));
}

void test_leaf_splits_off_the_last_component(void)
{
    String directory, leaf;

    xcopySplitLeaf(String("c/list"), directory, leaf);
    TEST_ASSERT_EQUAL_STRING("c", directory.str().c_str());
    TEST_ASSERT_EQUAL_STRING("list", leaf.str().c_str());

    // No slash: the whole thing is the leaf, in the root.
    xcopySplitLeaf(String("list"), directory, leaf);
    TEST_ASSERT_EQUAL_STRING("", directory.str().c_str());
    TEST_ASSERT_EQUAL_STRING("list", leaf.str().c_str());

    // A trailing slash means the whole thing was a directory. "dir ADF0:c" relies
    // on this to list c rather than to look for c in the root.
    xcopySplitLeaf(String("c/"), directory, leaf);
    TEST_ASSERT_EQUAL_STRING("c", directory.str().c_str());
    TEST_ASSERT_EQUAL_STRING("", leaf.str().c_str());

    xcopySplitLeaf(String(""), directory, leaf);
    TEST_ASSERT_EQUAL_STRING("", directory.str().c_str());
    TEST_ASSERT_EQUAL_STRING("", leaf.str().c_str());
}


void test_completion_splits_a_device_from_its_path(void)
{
    // Without the colon counting as a separator this asks the card's root for
    // names beginning "ADF0:d" and finds nothing.
    TEST_ASSERT_EQUAL_STRING("cat ADF0:devs/", complete("cat ADF0:d").c_str());
}

void test_completion_inside_a_mounted_image(void)
{
    // A unique match is finished off with a space, as any other would be.
    TEST_ASSERT_EQUAL_STRING("cat ADF0:c/list ", complete("cat ADF0:c/li").c_str());
}

void test_completion_folds_a_device_path_to_its_common_prefix(void)
{
    // "list" and "loadwb" share only the l, so one Tab gets that far and stops.
    TEST_ASSERT_EQUAL_STRING("cat ADF0:c/l", complete("cat ADF0:c/l").c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_completion_splits_a_device_from_its_path);
    RUN_TEST(test_completion_inside_a_mounted_image);
    RUN_TEST(test_completion_folds_a_device_path_to_its_common_prefix);
    RUN_TEST(test_path_without_a_device_is_the_card);
    RUN_TEST(test_path_with_a_device);
    RUN_TEST(test_device_name_is_upper_cased);
    RUN_TEST(test_bare_device_has_an_empty_path);
    RUN_TEST(test_sd_is_a_device_that_means_the_card);
    RUN_TEST(test_a_colon_in_a_filename_is_not_a_device);
    RUN_TEST(test_a_leading_colon_names_nothing);
    RUN_TEST(test_components_come_off_one_at_a_time);
    RUN_TEST(test_repeated_separators_are_skipped);
    RUN_TEST(test_leaf_splits_off_the_last_component);

    RUN_TEST(test_every_command_is_reachable_by_name);
    RUN_TEST(test_command_lookup_ignores_case);
    RUN_TEST(test_help_text_fits_its_column);

    RUN_TEST(test_parse_bare_commands);
    RUN_TEST(test_parse_subjects);
    RUN_TEST(test_parse_quoted_values);
    RUN_TEST(test_parse_negative_numbers_are_not_options);
    RUN_TEST(test_parse_options_in_any_order);
    RUN_TEST(test_parse_rejects_bad_options);
    RUN_TEST(test_diskinfo_options);
    RUN_TEST(test_diskinfo_completion);
    RUN_TEST(test_parse_raw_tail_commands);
    RUN_TEST(test_parse_multi_option_commands);

    RUN_TEST(test_complete_command_names);
    RUN_TEST(test_complete_option_names);
    RUN_TEST(test_complete_offers_options_when_there_is_no_subject);
    RUN_TEST(test_complete_paths);
    RUN_TEST(test_complete_directories_stay_open);
    RUN_TEST(test_complete_quotes_names_with_spaces);
    RUN_TEST(test_second_press_lists_the_candidates);
    RUN_TEST(test_first_press_prints_nothing);

    RUN_TEST(test_editor_types_and_edits);
    RUN_TEST(test_editor_cursor_stops_at_both_ends);
    RUN_TEST(test_editor_runs_a_line_and_clears);
    RUN_TEST(test_editor_history);
    RUN_TEST(test_editor_keeps_a_half_typed_line);
    RUN_TEST(test_editor_ctrl_c_abandons_the_line);
    RUN_TEST(test_editor_swallows_unknown_escapes);
    RUN_TEST(test_editor_counts_tab_presses);
    RUN_TEST(test_editor_redraw_is_one_write);

    return UNITY_END();
}
