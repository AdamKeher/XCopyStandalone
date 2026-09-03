#include "XCopyComplete.h"
#include "XCopyArgs.h"

// CANDIDATES

bool XCopyCandidates::matches(const String &value, const String &prefix)
{
    if (prefix.length() == 0)
        return true;
    if (value.length() < prefix.length())
        return false;

    return strncasecmp(value.c_str(), prefix.c_str(), prefix.length()) == 0;
}

void XCopyCandidates::fold(const String &prefix)
{
    _printing = false;
    _prefix = prefix;
    _common = "";
    _row = "";
    _count = 0;
    _directory = false;
}

void XCopyCandidates::list(const String &prefix, XCopyWriter writer)
{
    fold(prefix);
    _printing = true;
    _write = writer;
}

void XCopyCandidates::offer(const String &value, const String &display, bool directory)
{
    if (!matches(value, _prefix))
        return;

    if (_printing)
    {
        String item = display;
        if (directory)
            item += '/';

        if (_row.length() + item.length() + 2 > kWidth)
            flushRow();

        _row += item;
        // Padded to the next column boundary rather than to a fixed width, so a
        // name longer than a column takes two and the rows still line up.
        while (_row.length() % kColumn != 0)
            _row += ' ';

        _count++;
        return;
    }

    if (_count == 0)
    {
        _common = value;
        _directory = directory;
    }
    else
    {
        // Fold. Whatever the matches still agree on is what can be typed for the
        // operator without guessing which one they meant.
        uint16_t keep = 0;
        const uint16_t limit = (uint16_t)min(_common.length(), value.length());
        while (keep < limit && _common.charAt(keep) == value.charAt(keep))
            keep++;

        _common.remove(keep);
        _directory = false;
    }

    _count++;
}

void XCopyCandidates::flushRow()
{
    if (_row.length() == 0)
        return;

    // Trailing padding trimmed: it is invisible on screen and it is bytes over the
    // websocket for every row.
    while (_row.length() > 0 && _row.charAt(_row.length() - 1) == ' ')
        _row.remove(_row.length() - 1);

    if (_write != nullptr)
        _write(_row + "\r\n");
    _row = "";
}

void XCopyCandidates::finish()
{
    if (_printing)
        flushRow();
}

// WHAT IS BEING COMPLETED

bool XCopyCompleter::analyse(const String &line, uint16_t cursor, Context &context)
{
    /*
       Only what is to the left of the cursor. Completing from the middle of a
       word would have to decide what to do with the tail of it, and there is no
       answer to that which is not surprising half the time.
    */
    const String typed = line.substring(0, cursor);

    XCopyTokenizer tokenizer(typed);
    XCopyToken token;

    String commandWord;
    uint8_t index = 0;
    uint16_t lastEnd = 0;
    bool haveCurrent = false;

    // Only the last two tokens matter: the one being completed, and the one in
    // front of it, which says whether this is an option's value.
    XCopyToken previous;
    previous.text = "";
    previous.isOption = false;

    XCopyToken current;
    current.text = "";
    current.isOption = false;
    current.quoted = false;
    current.start = 0;
    current.end = 0;

    while (tokenizer.next(token))
    {
        if (index == 0)
            commandWord = token.text;

        previous = current;
        current = token;
        haveCurrent = true;
        lastEnd = token.end;
        index++;
    }

    // The cursor sits after a space, so a new token is being started rather than
    // an existing one extended.
    const bool freshToken = !haveCurrent || lastEnd < cursor;
    if (freshToken)
    {
        previous = current;
        current.text = "";
        current.start = cursor;
        current.end = cursor;
        current.isOption = false;
        current.quoted = false;
        index++;
    }

    context.start = current.start;
    context.end = cursor;
    context.prefix = current.text;

    if (index <= 1)
    {
        context.target = Target::commandName;
        return true;
    }

    context.command = xcopyFindCommand(commandWord);
    if (context.command == nullptr)
        return false;

    // A dash and at least the start of a name. A bare dash is still an option
    // being typed, which is the one case looksLikeOption() deliberately says no to.
    if (current.text.startsWith("-") && !current.quoted)
    {
        context.target = Target::optionName;
        return true;
    }

    if (previous.isOption)
    {
        const XCopyOption *option = xcopyFindOption(context.command, previous.text.substring(1));
        if (option != nullptr && option->kind != XCopyArgKind::flag)
        {
            context.target = Target::value;
            context.option = option;
            context.kind = option->kind;
            return true;
        }
    }

    context.target = Target::value;
    context.option = nullptr;
    context.kind = context.command->subject;

    return true;
}

// SOURCES

void XCopyCompleter::enumerateCommands(XCopyCandidates &sink)
{
    for (uint8_t i = 0; i < XCOPY_COMMAND_COUNT; i++)
    {
        const XCopyCommandDef &entry = XCOPY_COMMANDS[i];
        sink.offer(entry.name, entry.name, false);
        if (entry.alias != nullptr)
            sink.offer(entry.alias, entry.alias, false);
    }
}

void XCopyCompleter::enumerateOptions(const XCopyCommandDef *command, XCopyCandidates &sink, bool dashed)
{
    for (uint8_t i = 0; i < command->optionCount; i++)
    {
        const String name = dashed ? "-" + String(command->options[i].name) : String(command->options[i].name);
        sink.offer(name, name, false);
    }
}

void XCopyCompleter::enumerateChoices(const XCopyOption *option, XCopyCandidates &sink)
{
    if (option == nullptr || option->choices == nullptr)
        return;

    const char *walk = option->choices;
    while (*walk != 0)
    {
        const char *end = walk;
        while (*end != 0 && *end != '|')
            end++;

        String choice;
        for (const char *c = walk; c < end; c++)
            choice += *c;
        sink.offer(choice, choice, false);

        walk = (*end == '|') ? end + 1 : end;
    }
}

namespace
{
    //! What enumeratePaths() hands the lister, so its visitor can fold as it goes.
    struct PathVisit
    {
        XCopyCandidates *sink;
        const String *stem;
        const String *leaf;
    };

    void onPathEntry(void *context, const String &name, bool isDirectory)
    {
        PathVisit *visit = (PathVisit *)context;

        if (name.length() == 0)
            return;

        // Matched on the leaf here, so the sink can be given the whole path and
        // still be told only what the operator actually typed.
        const String &leaf = *visit->leaf;
        if (leaf.length() > 0 &&
            (name.length() < leaf.length() ||
             strncasecmp(name.c_str(), leaf.c_str(), leaf.length()) != 0))
            return;

        visit->sink->offer(*visit->stem + name, name, isDirectory);
    }
}

void XCopyCompleter::enumeratePaths(const String &prefix, XCopyCandidates &sink) const
{
    if (_lister == nullptr)
        return;

    /*
       Split at the last slash. The directory half is what gets opened and the leaf
       is what is matched, but the candidate offered is the whole path - replacing
       the whole token is what lets a name with a space in it come back quoted.
    */
    const int slash = prefix.lastIndexOf('/');
    const String directory = slash < 0 ? String("/") : prefix.substring(0, slash + 1);
    const String leaf = slash < 0 ? prefix : prefix.substring(slash + 1);

    /*
       What is offered has to begin with what was typed, or the sink will reject
       every one of its own candidates. So a path given without a leading slash is
       completed without one: "cat rea" becomes "cat readme.txt", not
       "cat /readme.txt". Both open the same file - SdFat resolves a relative path
       against the volume root - and echoing back what the operator typed is the
       less surprising of the two.
    */
    const String stem = slash < 0 ? String("") : directory;

    PathVisit visit = {&sink, &stem, &leaf};
    _lister(directory, onPathEntry, &visit);
}

void XCopyCompleter::begin(XCopyDirLister lister)
{
    _lister = lister;
}

void XCopyCompleter::enumerate(const Context &context, XCopyCandidates &sink) const
{
    switch (context.target)
    {
    case Target::commandName:
        enumerateCommands(sink);
        return;

    case Target::optionName:
        enumerateOptions(context.command, sink, true);
        return;

    case Target::value:
        if (context.kind == XCopyArgKind::path)
            enumeratePaths(context.prefix, sink);
        else if (context.kind == XCopyArgKind::choice)
            enumerateChoices(context.option, sink);
        else if (context.option == nullptr && context.command->optionCount > 0)
        {
            /*
               Nothing to complete for the subject - it is a number, or free text,
               or the command has no subject at all. Offering the options instead
               is the useful answer: "readscp <TAB>" is asking what it takes.
            */
            enumerateOptions(context.command, sink, true);
        }
        return;

    default:
        return;
    }
}

String XCopyCompleter::render(const String &value, bool directory, bool finished)
{
    const bool quote = value.indexOf(' ') >= 0;

    String out;
    if (quote)
        out += '"';
    out += value;

    if (!finished)
        return out; // still being narrowed down, so it is left open

    if (directory)
    {
        // No closing quote and no space: the next Tab carries on inside it.
        out += '/';
        return out;
    }

    if (quote)
        out += '"';
    out += ' ';

    return out;
}

void XCopyCompleter::complete(XCopyLineEditor &editor, uint8_t presses)
{
    Context context;
    if (!analyse(editor.line(), editor.cursor(), context))
        return;

    XCopyCandidates sink;
    sink.fold(context.prefix);
    enumerate(context, sink);

    if (sink.count() == 0)
        return;

    if (sink.count() == 1)
    {
        editor.replace(context.start, context.end, render(sink.common(), sink.directory(), true));
        return;
    }

    if (sink.common() != context.prefix)
    {
        // Type as far as the matches still agree and stop there.
        editor.replace(context.start, context.end, render(sink.common(), false, false));
        return;
    }

    // Nothing left to add, so the second press shows what the choice is.
    if (presses < 2)
        return;

    editor.write("\r\n");
    XCopyCandidates printer;
    printer.list(context.prefix, editor.writer());
    enumerate(context, printer);
    printer.finish();

    editor.redraw();
}
