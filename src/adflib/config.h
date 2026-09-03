/*
   ADFlib's autotools config.h, supplied by hand.

   Upstream generates this with `configure`, which probes the toolchain for the
   handful of POSIX string helpers the library uses and #defines a HAVE_ macro for
   each one it finds. adf_util.h includes this file unless BUILDING_WITH_CMAKE is
   defined, and adf_util.c uses the macros to decide whether to compile its own
   fallback implementations.

   Neither build here uses autotools or CMake, and the answers differ per toolchain
   - newlib on arm-none-eabi has all of them, MinGW declares strnlen and mempcpy and
   neither strndup nor stpncpy - so the probing is done once, in platformio.ini,
   where the toolchain is chosen. Look there to change it.

   This file is an addition to the vendored tree, not a change to it. See PATCHES.md.
*/

#ifndef ADFLIB_CONFIG_H
#define ADFLIB_CONFIG_H

/* Deliberately empty. See above. */

#endif /* ADFLIB_CONFIG_H */
