#ifndef XCOPYCOMPLETE_H
#define XCOPYCOMPLETE_H

#include <Arduino.h>
#include "XCopyCommandTable.h"
#include "XCopyConsoleIO.h"
#include "XCopyLineEditor.h"

/**
 * @brief Candidates, counted and folded without ever being stored.
 *
 * Two passes over the source instead of a list: the first counts the matches and
 * folds their longest common prefix, and the second - only reached when the
 * operator presses Tab twice - prints them. A directory of four hundred files
 * costs two reads and no memory, which matters on a part where the only sizeable
 * free block is the track buffer.
 *
 * Matching lives here rather than in the sources, so every kind of candidate is
 * matched the same way.
 */
class XCopyCandidates
{
public:
    //! Count and fold. What the first Tab does.
    void fold(const String &prefix);
    //! List in columns, through @p writer. What the second Tab does.
    void list(const String &prefix, XCopyWriter writer);

    /**
     * @param value     what would be typed into the line.
     * @param display   what to show when listing - the leaf, for a path.
     * @param directory a path that can be descended into, so it gets a slash and
     *                  no trailing space.
     */
    void offer(const String &value, const String &display, bool directory);
    //! Flush the last row of a listing.
    void finish();

    uint16_t count() const { return _count; }
    const String &common() const { return _common; }
    //! Only meaningful when count() is 1.
    bool directory() const { return _directory; }

private:
    // Wide enough for a long filename to sit in one column on an 80 column
    // terminal, and narrow enough for four short ones to a row.
    static const uint8_t kColumn = 18;
    static const uint8_t kWidth = 78;

    bool _printing = false;
    XCopyWriter _write = nullptr;
    String _prefix;
    String _common;
    String _row;
    uint16_t _count = 0;
    bool _directory = false;

    void flushRow();
    static bool matches(const String &value, const String &prefix);
};

/**
 * @brief Works out what the Tab was asking for, and answers it.
 *
 * The line is read with the same tokenizer the parser uses, which is the only way
 * a completion can be sure it is offering something the parser will then accept.
 */
class XCopyCompleter
{
public:
    /**
     * @param lister how to read a directory. See XCopyConsoleIO.h.
     */
    void begin(XCopyDirLister lister);

    void complete(XCopyLineEditor &editor, uint8_t presses);

private:
    //! What the token under the cursor is.
    enum class Target : uint8_t
    {
        none,
        commandName,
        optionName,
        value //!< an option's value, or the command's subject
    };

    struct Context
    {
        Target target = Target::none;
        const XCopyCommandDef *command = nullptr;
        const XCopyOption *option = nullptr; //!< set when completing a value
        XCopyArgKind kind = XCopyArgKind::none;
        String prefix;
        uint16_t start = 0; //!< where in the line the replacement begins
        uint16_t end = 0;
    };

    XCopyDirLister _lister = nullptr;

    static bool analyse(const String &line, uint16_t cursor, Context &context);
    void enumerate(const Context &context, XCopyCandidates &sink) const;
    static void enumerateCommands(XCopyCandidates &sink);
    static void enumerateOptions(const XCopyCommandDef *command, XCopyCandidates &sink, bool dashed);
    static void enumerateChoices(const XCopyOption *option, XCopyCandidates &sink);
    void enumeratePaths(const String &prefix, XCopyCandidates &sink) const;

    //! Wrap in quotes if it has a space in it, and close it off if it is finished.
    static String render(const String &value, bool directory, bool finished);
};

#endif // XCOPYCOMPLETE_H
