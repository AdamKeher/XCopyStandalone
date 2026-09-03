#ifndef XCOPYADFPATH_H
#define XCOPYADFPATH_H

#include <Arduino.h>

/*
   One path grammar for the SD card and for mounted images.

       <command> [device:]<path>

   Amiga device syntax, because that is what the contents of these images call
   themselves and because the colon makes it unambiguous against an SdFat path -
   an Amiga file name cannot contain one, and neither can a FAT one.

       ADF0:c/list                  inside a mounted image
       DF0:s/startup-sequence       on the disk in the drive
       SD:/adfs/workbench13.adf     on the card, said explicitly
       /adfs/workbench13.adf        on the card, as every path meant before this

   Paths after the colon are read from the volume's root. There is no relative form
   yet: a "cd" command would give each slot a working directory to be relative to,
   and until there is one, "ADF0:c" meaning two different things depending on
   history would be a trap rather than a convenience.

   Depends on nothing but String, so it builds and is tested on a host - which is
   worth more here than usual, because a grammar has far more edges than it looks
   like it has and every one of them is a way to open the wrong file.
*/

struct XCopyPath
{
    //! A "<device>:" prefix was present.
    bool qualified = false;

    //! The device name without its colon, upper cased. Empty when not qualified.
    String device;

    //! Everything after the colon - or the whole of it, when not qualified.
    String rest;

    //! True for an unqualified path and for "SD:", which mean the same thing.
    bool isCard() const { return !qualified || device == "SD"; }
};

/**
 * @brief Split @p text into a device and the path within it.
 *
 * @result false only when the text has a colon with nothing before it, which is
 *         the one shape that cannot mean anything.
 */
bool xcopySplitPath(const String &text, XCopyPath &out);

/**
 * @brief Take the first component off @p path.
 *
 * "c/list" leaves @p path as "list" and sets @p component to "c". Repeated
 * separators and a leading one are skipped, so "//c//list" walks the same way.
 *
 * @result false when @p path holds no more components.
 */
bool xcopyNextComponent(String &path, String &component);

/**
 * @brief Split @p path into the directory part and the last component.
 *
 * "c/list" gives "c" and "list"; "list" gives "" and "list"; "c/" gives "c" and "".
 * Used by every command that has to reach a file: walk to @p directory, then act
 * on @p leaf.
 */
void xcopySplitLeaf(const String &path, String &directory, String &leaf);

#endif // XCOPYADFPATH_H
