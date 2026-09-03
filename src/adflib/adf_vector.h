/*
 *  adf_vector.h - a custom vector implementation
 *
 *  Copyright (C) 2023-2026 Tomasz Wolak
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

#ifndef ADF_VECTOR_H
#define ADF_VECTOR_H

#include "adf_prefix.h"
#include "adf_types.h"

/* shorter, less clutter but type-unsafe version:
struct AdfVector {
    unsigned len;
    union {
        void *         contents;
        ADF_SECTNUM *  sectors;
        // ...
    };
};
*/

//typedef void (*AdfVectorDestructor)(AdfVector * const vector);
typedef void (*AdfDestructor)( void * const container );


struct AdfVector {
    unsigned       nItems,
                   itemSize;
    void *         items;
    AdfDestructor  destroy;
};

struct AdfVectorSectors {
    unsigned       nItems,
                   itemSize;
    ADF_SECTNUM *  sectors;
    AdfDestructor  destroy;
};


ADF_PREFIX struct AdfVector adfVectorCreate( const unsigned  nItems,
                                             const unsigned  itemSize );

ADF_PREFIX struct AdfVectorSectors adfVectorSectorsCreate( const unsigned  nItems );

#endif  /* ADF_VECTOR_H */
