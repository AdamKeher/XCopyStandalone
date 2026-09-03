#ifndef XCOPYARGS_H
#define XCOPYARGS_H

#include <Arduino.h>
#include "XCopyCommandTable.h"

/**
 * @brief One token of a command line, and where in the line it came from.
 *
 * The positions are what tab completion needs: it has to know which token the
 * cursor is sitting in and where that token starts, so it can replace it.
 */
struct XCopyToken
{
    String text;    //!< with any quotes removed
    uint16_t start; //!< index of the first character, the opening quote included
    uint16_t end;   //!< one past the last character consumed
    bool quoted;
    bool isOption; //!< a dash followed by a letter - see the note in XCopyTokenizer
};

/**
 * @brief Walks a command line a token at a time. Holds no list.
 *
 * Both the parser and the completer read the same line the same way, which is the
 * only way a completion can be sure it is offering something the parser will
 * accept.
 */
class XCopyTokenizer
{
public:
    explicit XCopyTokenizer(const String &line) : _line(line), _pos(0) {}

    //! False once the line is exhausted.
    bool next(XCopyToken &token);

    //! Where the walk has reached, for a caller that wants the rest verbatim.
    uint16_t position() const { return _pos; }

    /*
       A dash followed by a letter. Not just a dash: "timezone -10" is a negative
       number, and the shape test is what keeps that working without every handler
       having to know about it.
    */
    static bool looksLikeOption(const String &text);

private:
    const String &_line;
    uint16_t _pos;
};

/**
 * @brief A command line parsed against its entry in the command table.
 *
 * The table says which options exist and what kind of value each one takes, so
 * unknown options, missing values and values of the wrong shape are all caught
 * here, once, and reported the same way. Handlers get to assume their arguments
 * are the shape they asked for.
 */
class XCopyArgs
{
public:
    /**
     * @param tail  everything after the command word.
     * @param error filled in when this returns false, ready to print.
     */
    bool parse(const XCopyCommandDef *command, const String &tail, String &error);

    //! Was the option given at all - the question a flag asks.
    bool has(const char *name) const;
    long number(const char *name, long fallback) const;
    String text(const char *name, const char *fallback = "") const;

    bool hasSubject() const { return _hasSubject; }
    const String &subject() const { return _subject; }
    long subjectNumber(long fallback) const;

private:
    // Three is the most any command in the table takes; four leaves room for one
    // more without a rebuild of this class being part of adding it.
    static const uint8_t kMaxOptions = 4;

    const char *_names[kMaxOptions] = {nullptr, nullptr, nullptr, nullptr};
    String _values[kMaxOptions];
    uint8_t _count = 0;

    String _subject;
    bool _hasSubject = false;

    int8_t find(const char *name) const;
    static bool isNumber(const String &text);
    static String optionList(const XCopyCommandDef *command);
};

#endif // XCOPYARGS_H
