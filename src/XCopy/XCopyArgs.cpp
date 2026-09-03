#include "XCopyArgs.h"

bool XCopyTokenizer::looksLikeOption(const String &text)
{
    return text.length() >= 2 && text.charAt(0) == '-' && isAlpha(text.charAt(1));
}

bool XCopyTokenizer::next(XCopyToken &token)
{
    const uint16_t length = (uint16_t)_line.length();

    while (_pos < length && _line.charAt(_pos) == ' ')
        _pos++;

    if (_pos >= length)
        return false;

    token.start = _pos;
    token.text = "";
    token.quoted = false;

    const char quote = _line.charAt(_pos);
    if (quote == '"' || quote == '\'')
    {
        /*
           Quoted. The closing quote is optional, because a line being completed is
           usually still being typed - `cat "/adf/` has to tokenise so that the
           completer can see what directory it is in.
        */
        token.quoted = true;
        _pos++;
        while (_pos < length && _line.charAt(_pos) != quote)
            token.text += _line.charAt(_pos++);
        if (_pos < length)
            _pos++;
    }
    else
    {
        while (_pos < length && _line.charAt(_pos) != ' ')
            token.text += _line.charAt(_pos++);
    }

    token.end = _pos;
    // A quoted token is never an option, so "-file" can be given as a value.
    token.isOption = !token.quoted && looksLikeOption(token.text);

    return true;
}

int8_t XCopyArgs::find(const char *name) const
{
    for (uint8_t i = 0; i < _count; i++)
        if (strcasecmp(_names[i], name) == 0)
            return (int8_t)i;

    return -1;
}

bool XCopyArgs::has(const char *name) const
{
    return find(name) >= 0;
}

long XCopyArgs::number(const char *name, long fallback) const
{
    const int8_t index = find(name);
    return index < 0 ? fallback : strtol(_values[index].c_str(), nullptr, 10);
}

String XCopyArgs::text(const char *name, const char *fallback) const
{
    const int8_t index = find(name);
    return index < 0 ? String(fallback) : _values[index];
}

long XCopyArgs::subjectNumber(long fallback) const
{
    return _hasSubject ? strtol(_subject.c_str(), nullptr, 10) : fallback;
}

bool XCopyArgs::isNumber(const String &text)
{
    if (text.length() == 0)
        return false;

    uint16_t i = (text.charAt(0) == '-' || text.charAt(0) == '+') ? 1 : 0;
    if (i >= text.length())
        return false;

    for (; i < text.length(); i++)
        if (!isDigit(text.charAt(i)))
            return false;

    return true;
}

String XCopyArgs::optionList(const XCopyCommandDef *command)
{
    if (command->optionCount == 0)
        return String("none");

    String list;
    for (uint8_t i = 0; i < command->optionCount; i++)
    {
        if (i > 0)
            list += ", ";
        list += "-";
        list += command->options[i].name;
    }

    return list;
}

bool XCopyArgs::parse(const XCopyCommandDef *command, const String &tail, String &error)
{
    if (command == nullptr)
        return false;

    /*
       A raw tail command takes the rest of the line exactly as typed. A search
       string and a websocket message both legitimately contain dashes, quotes and
       runs of spaces, and none of that should be read as grammar.
    */
    if (command->flags & XCOPY_RAW_TAIL)
    {
        _subject = tail;
        _subject.trim();
        _hasSubject = _subject.length() > 0;
    }
    else
    {
        XCopyTokenizer tokenizer(tail);
        XCopyToken token;

        while (tokenizer.next(token))
        {
            if (!token.isOption)
            {
                if (command->subject == XCopyArgKind::none)
                {
                    error = "'" + String(command->name) + "' takes no argument of its own. Options: " +
                            optionList(command);
                    return false;
                }

                if (_hasSubject)
                {
                    error = "'" + String(command->name) + "' takes one " + String(command->subjectName) +
                            ", and it already has '" + _subject + "'";
                    return false;
                }

                if (command->subject == XCopyArgKind::number && !isNumber(token.text))
                {
                    error = String(command->subjectName) + " must be a number, not '" + token.text + "'";
                    return false;
                }

                _subject = token.text;
                _hasSubject = true;
                continue;
            }

            const String name = token.text.substring(1);
            const XCopyOption *option = xcopyFindOption(command, name);
            if (option == nullptr)
            {
                error = "'" + String(command->name) + "' has no option '-" + name + "'. Options: " +
                        optionList(command);
                return false;
            }

            if (_count >= kMaxOptions)
            {
                error = "too many options";
                return false;
            }

            String value = "1";
            if (option->kind != XCopyArgKind::flag)
            {
                XCopyToken valueToken;
                // An option that is followed by another option has been given no
                // value at all - "-revs -file x" is a typo, not a value of "-file".
                if (!tokenizer.next(valueToken) || valueToken.isOption)
                {
                    error = "-" + name + " needs a value " + xcopyKindName(option->kind);
                    return false;
                }

                if (option->kind == XCopyArgKind::number && !isNumber(valueToken.text))
                {
                    error = "-" + name + " must be a number, not '" + valueToken.text + "'";
                    return false;
                }

                if (option->kind == XCopyArgKind::choice && !xcopyChoiceValid(option, valueToken.text))
                {
                    error = "-" + name + " must be one of " + String(option->choices);
                    return false;
                }

                value = valueToken.text;
            }

            // The name points into the table rather than being copied: the table
            // outlives every parse, and this saves a String per option.
            _names[_count] = option->name;
            _values[_count] = value;
            _count++;
        }
    }

    if ((command->flags & XCOPY_SUBJECT_REQUIRED) && !_hasSubject)
    {
        error = "'" + String(command->name) + "' needs a " + String(command->subjectName);
        return false;
    }

    return true;
}
