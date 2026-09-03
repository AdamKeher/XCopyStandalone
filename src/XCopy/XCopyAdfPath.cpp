#include "XCopyAdfPath.h"

namespace
{
    char upper(char c)
    {
        return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }

    /*
       What may appear in a device name.

       Letters and digits only. Anything else before a colon is not a device
       reference at all, which matters for the one case that would otherwise
       surprise: an SdFat path can hold a colon in a directory name, and reading
       "my:notes/x" as a device would open nothing and say the wrong thing about
       why.
    */
    bool nameChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9');
    }
}

bool xcopySplitPath(const String &text, XCopyPath &out)
{
    out.qualified = false;
    out.device = "";
    out.rest = text;

    const int colon = text.indexOf(':');
    if (colon < 0)
        return true;

    if (colon == 0)
        return false; // ":something" names no device

    for (int i = 0; i < colon; i++)
    {
        if (!nameChar(text.charAt(i)))
            return true; // not a device reference; the whole thing is a card path
    }

    out.qualified = true;
    out.rest = text.substring(colon + 1);

    out.device = "";
    for (int i = 0; i < colon; i++)
        out.device += upper(text.charAt(i));

    return true;
}

bool xcopyNextComponent(String &path, String &component)
{
    unsigned int start = 0;
    while (start < path.length() && path.charAt(start) == '/')
        start++;

    if (start >= path.length())
    {
        path = "";
        component = "";
        return false;
    }

    unsigned int end = start;
    while (end < path.length() && path.charAt(end) != '/')
        end++;

    component = path.substring(start, end);
    path = path.substring(end);
    return true;
}

void xcopySplitLeaf(const String &path, String &directory, String &leaf)
{
    const int slash = path.lastIndexOf('/');

    if (slash < 0)
    {
        directory = "";
        leaf = path;
        return;
    }

    directory = path.substring(0, slash);
    leaf = path.substring(slash + 1);
}
