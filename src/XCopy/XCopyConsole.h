#ifndef XCOPYCONSOLE_H
#define XCOPYCONSOLE_H

#include <Arduino.h>

class XCopyConsole
{
public:
  static String success(String text) { return green() + text + reset(); }
  static String error(String text)   { return red() + text + reset(); }
  
  static String reset()             { return "\033[0m"; };
  static String clearscreen()       { return "\033[2J"; }
  static String home()              { return "\033[H"; }
  static String echo()              { return "\033[12h"; }
  static String backspace()         { return "\033[1D \033[1D"; }

  static String black()             { return "\033[0;30m"; };
  static String red()               { return "\033[0;31m"; };
  static String green()             { return "\033[0;32m"; };
  static String yellow()            { return "\033[0;33m"; };
  static String blue()              { return "\033[0;34m"; };
  static String purple()            { return "\033[0;35m"; };
  static String cyan()              { return "\033[0;36m"; };
  static String white()             { return "\033[0;37m"; };

  static String bold_black()        { return "\033[1;30m"; };
  static String bold_red()          { return "\033[1;31m"; };
  static String bold_green()        { return "\033[1;32m"; };
  static String bold_yellow()       { return "\033[1;33m"; };
  static String bold_blue()         { return "\033[1;34m"; };
  static String bold_purple()       { return "\033[1;35m"; };
  static String bold_cyan()         { return "\033[1;36m"; };
  static String bold_white()        { return "\033[1;37m"; };

  static String underline_black()   { return "\033[4;30m"; };
  static String underline_red()     { return "\033[4;31m"; };
  static String underline_green()   { return "\033[4;32m"; };
  static String underline_yellow()  { return "\033[4;33m"; };
  static String underline_blue()    { return "\033[4;34m"; };
  static String underline_purple()  { return "\033[4;35m"; };
  static String underline_cyan()    { return "\033[4;36m"; };
  static String underline_white()   { return "\033[4;37m"; };

  static String background_black()  { return "\033[40m"; };
  static String background_red()    { return "\033[41m"; };
  static String background_green()  { return "\033[42m"; };
  static String background_yellow() { return "\033[43m"; };
  static String background_blue()   { return "\033[44m"; };
  static String background_purple() { return "\033[45m"; };
  static String background_cyan()   { return "\033[46m"; };
  static String background_white()  { return "\033[47m"; };

  static String high_black()        { return "\033[90m"; };
  static String high_red()          { return "\033[91m"; };
  static String high_green()        { return "\033[92m"; };
  static String high_yellow()       { return "\033[93m"; };
  static String high_blue()         { return "\033[94m"; };
  static String high_purple()       { return "\033[95m"; };
  static String high_cyan()         { return "\033[96m"; };
  static String high_white()        { return "\033[97m"; };

  static String boldhigh_black()    { return "\033[1;40m"; };
  static String boldhigh_red()      { return "\033[1;41m"; };
  static String boldhigh_green()    { return "\033[1;42m"; };
  static String boldhigh_yellow()   { return "\033[1;43m"; };
  static String boldhigh_blue()     { return "\033[1;44m"; };
  static String boldhigh_purple()   { return "\033[1;45m"; };
  static String boldhigh_cyan()     { return "\033[1;46m"; };
  static String boldhigh_white()    { return "\033[1;47m"; };

  static String highback_black()    { return "\033[0;100m"; };
  static String highback_red()      { return "\033[0;101m"; };
  static String highback_green()    { return "\033[0;102m"; };
  static String highback_yellow()   { return "\033[0;103m"; };
  static String highback_blue()     { return "\033[0;104m"; };
  static String highback_purple()   { return "\033[0;105m"; };
  static String highback_cyan()     { return "\033[0;106m"; };
  static String highback_white()    { return "\033[0;107m"; };
};      

#endif // XCOPYCONSOLE_H