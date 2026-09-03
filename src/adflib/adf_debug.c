/*
 *  adf_debug.c - utility functions for debugging
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

#include "adf_debug.h"

#include "adf_util.h"

#if ( defined HAVE_BACKTRACE && defined HAVE_BACKTRACE_SYMBOLS )
#include <execinfo.h>    /* required for backtrace() */
#endif

#include <stdio.h>
#include <stdlib.h>

#if ( defined HAVE_BACKTRACE && defined HAVE_BACKTRACE_SYMBOLS )
/*
  backtrace(), backtrace_symbols are available only in glibc

  Require link option: -rdynamic

  More info:
  https://www.gnu.org/software/libc/manual/html_node/Backtraces.html
  https://stackoverflow.com/questions/5945775/how-to-get-more-detailed-backtrace
  https://stackoverflow.com/questions/3899870/print-call-stack-in-c-or-c/54365144#54365144
  https://stackoverflow.com/questions/6934659/how-to-make-backtrace-backtrace-symbols-print-the-function-names
 */
void adfPrintBacktrace(void)
{
    const unsigned BUFSIZE = 100;
    void *buffer[ BUFSIZE ];

    int size = backtrace( buffer, (int) BUFSIZE );
    const char * const * const strings =
        ( const char * const* const ) backtrace_symbols( buffer, size );

    if ( strings == NULL ) {
        perror( "error getting symbols" );
        return;
    }

    printf( "Obtained %d stack frames.\n", size );
    for ( int i = 0 ; i < size ; i++ )
        printf( "%s\n", strings[i] );

    free( (void *) strings );

}
#else
/* no backtrace without glibc

   ( for Windows, if ever needed, this might be helpful:
     https://stackoverflow.com/questions/26398064/counterpart-to-glibcs-backtrace-and-backtrace-symbols-on-windows )
 */
void adfPrintBacktrace(void)
{
    fprintf( stderr, "Sorry, no backtrace without glibc...\n" );
}
#endif



#ifdef ADF_DEBUG_BLOCKS

/*
 * adfDumpBlock
 *
 * debug function : to dump a block before writing the check its contents
 *
 */
void adfDumpBlock( const uint8_t * const  buf )
{
    int i, j;

    for ( i = 0; i < 32; i++ ) {
        printf( "%5x ", i * 16 );
        for ( j = 0; j < 4; j++ ) {
            printf( "%08x ", swapUint32fromPtr( buf + j * 4 + i * 16 ) );
        }
        printf("    ");
        for ( j = 0; j < 16; j++ )
            if ( buf[ i * 16 + j ] < 32 ||
                 buf[ i * 16 + j ] > 127 )
            {
                putchar('.');
            } else {
                putchar( buf[ i * 16 + j ] );
            }
        putchar('\n');
    }
}
#else
void adfDumpBlock( const uint8_t * const  buf )
{
    (void) buf;
}
#endif
