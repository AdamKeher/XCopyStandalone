/*
 *  adf_types.h
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

#ifndef ADF_TYPES_H
#define ADF_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t ADF_SECTNUM;

typedef enum {
    ADF_ACCESS_MODE_READWRITE = 0,
    ADF_ACCESS_MODE_READONLY  = 1
} AdfAccessMode;

#endif  /* ADF_TYPES_H */
