/*
   ADFlib's autotools config.h, written by hand.

   Upstream generates this with `configure`, and adf_util.h includes it unless
   BUILDING_WITH_CMAKE is defined. Nothing here builds with either, so the three
   macros the library actually reads are declared directly.

   All three name functions newlib provides on arm-none-eabi and glibc provides on
   the host, so ADFlib's own fallback implementations at the bottom of adf_util.c
   stay compiled out. If a toolchain ever turns up without one of them, undefine it
   here rather than editing adf_util.c - the fallbacks are already written.

   This file is an addition to the vendored tree, not a change to it. See PATCHES.md.
*/

#ifndef ADFLIB_CONFIG_H
#define ADFLIB_CONFIG_H

#define HAVE_STRNLEN 1
#define HAVE_STRNDUP 1
#define HAVE_STPNCPY 1

#endif /* ADFLIB_CONFIG_H */
