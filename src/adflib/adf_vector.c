/*
 *  adf_vector.c - a custom vector implementation
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

#include "adf_vector.h"

#include <assert.h>
#include <stdlib.h>


static void adfVectorDestroy( void * const  container );
//static void adfVectorSectorsDestroy( struct AdfVectorSectors * const  vector );


struct AdfVector adfVectorCreate( const unsigned  nItems,
                                  const unsigned  itemSize )
{
    assert( itemSize > 0 );

    struct AdfVector vector = {
        .items    = ( nItems > 0 ? malloc( nItems * itemSize ) : NULL ),
        .itemSize = itemSize,
        .destroy  = adfVectorDestroy
    };
    vector.nItems = ( vector.items != NULL ? nItems : 0 );
    return vector;
}

static void adfVectorDestroy( void * const  container )
{
    struct AdfVector * const vector = container;
    free( vector->items );
    vector->items   = NULL;
    vector->nItems  = 0;
    vector->destroy = NULL;
}

struct AdfVectorSectors adfVectorSectorsCreate( const unsigned  nItems )
{
    struct AdfVector        vector       = adfVectorCreate( nItems, sizeof(ADF_SECTNUM) );
    struct AdfVectorSectors vectorSector = *( (struct AdfVectorSectors *) &vector );

    vectorSector.destroy = adfVectorDestroy;

    return vectorSector;
}


/*
   An example of a custom implemeted "destructor".

   This would have to be assigned in "constructor" (Create function) as
   .destroy (a "self-destructor") instead of adfVectorSector().

   For the vector of sectors - nothing special is needed, but there could be
   some struct with dynamically allocated data (in "constructor") which
   then should be freed in the "self-destructor".

static void adfVectorSectorsDestroy( struct AdfVectorSectors * const  vector )
{
    adfVectorDestroy( (struct AdfVector *) vector );
}
*/
