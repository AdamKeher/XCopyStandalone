#ifndef ARDUINO_H
#define ARDUINO_H

/*
   Just enough Arduino to build the console interpreter on a host.

   The four files under test - XCopyCommandTable, XCopyArgs, XCopyLineEditor and
   XCopyComplete - depend on nothing but Arduino's String and a couple of its
   character helpers. That is deliberate, and XCopyConsoleIO.h says why: the
   terminal and the SD card reach them as function pointers, so neither the log
   nor SdFat has to come along to a host build.

   String here is a thin wrapper over std::string with the same surface those
   files use. Where Arduino's String is unusual - substring() clamping rather than
   throwing, remove() truncating, charAt() past the end returning 0 - this matches
   the behaviour rather than the standard library's, because the firmware relies
   on it.
*/

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <algorithm>

class String
{
public:
    String() {}
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}

    unsigned int length() const { return (unsigned int)_s.size(); }
    //! Past the end is 0, not a throw. The tokenizer relies on it.
    char charAt(unsigned int i) const { return i < _s.size() ? _s[i] : 0; }
    const char *c_str() const { return _s.c_str(); }

    String substring(unsigned int from) const
    {
        if (from >= _s.size()) return String();
        return String(_s.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const
    {
        if (from > to) std::swap(from, to);
        if (from >= _s.size()) return String();
        if (to > _s.size()) to = (unsigned int)_s.size();
        return String(_s.substr(from, to - from));
    }

    int indexOf(char c) const { auto p = _s.find(c); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const String &t) const { auto p = _s.find(t._s); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(char c) const { auto p = _s.rfind(c); return p == std::string::npos ? -1 : (int)p; }

    void remove(unsigned int index) { if (index < _s.size()) _s.erase(index); }
    void remove(unsigned int index, unsigned int count) { if (index < _s.size()) _s.erase(index, count); }
    void reserve(unsigned int n) { _s.reserve(n); }
    void trim()
    {
        while (!_s.empty() && isspace((unsigned char)_s.front())) _s.erase(_s.begin());
        while (!_s.empty() && isspace((unsigned char)_s.back())) _s.pop_back();
    }

    bool startsWith(const String &p) const { return _s.rfind(p._s, 0) == 0; }
    bool equalsIgnoreCase(const String &o) const
    {
        if (_s.size() != o._s.size()) return false;
        return strncasecmp(_s.c_str(), o._s.c_str(), _s.size()) == 0;
    }

    long toInt() const { return strtol(_s.c_str(), nullptr, 10); }

    String &operator+=(const String &o) { _s += o._s; return *this; }
    String &operator+=(const char *o) { _s += o; return *this; }
    String &operator+=(char c) { _s += c; return *this; }

    bool operator==(const String &o) const { return _s == o._s; }
    bool operator!=(const String &o) const { return _s != o._s; }
    bool operator==(const char *o) const { return _s == std::string(o ? o : ""); }
    bool operator!=(const char *o) const { return !(*this == o); }

    //! For the assertions, which want a plain C string to compare and print.
    const std::string &str() const { return _s; }

private:
    std::string _s;
};

inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, char b) { String r(a); r += b; return r; }

inline bool isAlpha(char c) { return isalpha((unsigned char)c) != 0; }
inline bool isDigit(char c) { return isdigit((unsigned char)c) != 0; }

// Arduino has these. As templates rather than macros, because as macros they
// collide with the host standard library this shim is built on top of.
template <typename A, typename B> inline A min(A a, B b) { return a < (A)b ? a : (A)b; }
template <typename A, typename B> inline A max(A a, B b) { return a > (A)b ? a : (A)b; }

//! On this part F() only marks a literal as living in flash; here it is nothing.
#define F(x) String(x)

#endif // ARDUINO_H
