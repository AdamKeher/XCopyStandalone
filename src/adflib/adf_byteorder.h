/*
 *  adf_byte_order.h - cpu endianess
 *
 *  Copyright (C) 1997-2022 Laurent Clevy
 *                2023-2026 Tomasz Wolak
 *
 *  This file is part of ADFLib.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef ADF_BYTEORDER_H
#define ADF_BYTEORDER_H

#ifndef LITT_ENDIAN
 #if defined(__hppa__) || \
     defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
     (defined(__MIPS__) && defined(__MISPEB__)) || \
     defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
     defined(__sparc__)
 #else
  #define LITT_ENDIAN 1
 #endif
#endif

#endif /* ADF_BYTEORDER_H */
