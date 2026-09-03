/*
 *  adf_env.c - library context and customization code
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

#include "adf_env.h"

#include "adf_blk.h"
#include "adf_byteorder.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


struct AdfEnv  adfEnv;


static void Warningf( const char * const  format, ... );
static void Errorf( const char * const  format, ... );
static void Verbosef( const char * const  format, ... );

static void Changed( ADF_SECTNUM  nSect,
                     int          changedType );

static void rwHeadAccess( const ADF_SECTNUM  physical,
                          const ADF_SECTNUM  logical,
                          const bool         write );

static void progressBar( int  perCentDone );


/*
 * adfInitEnv
 *
 */
void adfEnvInitDefault(void)
{
    adfEnv.wFct = Warningf;
    adfEnv.eFct = Errorf;
    adfEnv.vFct = Verbosef;

    adfEnv.notifyFct   = Changed;
    adfEnv.rwhAccess   = rwHeadAccess;
    adfEnv.progressBar = progressBar;

    adfEnv.useDirCache    = false;
    adfEnv.useRWAccess    = false;
    adfEnv.useNotify      = false;
    adfEnv.useProgressBar = false;
    adfEnv.ignoreChecksumErrors = false;
    adfEnv.quiet          = false;

/*    sprintf(str,"ADFlib %s (%s)",adfGetVersionNumber(),adfGetVersionDate());
    (*adfEnv.vFct)(str);
*/
}


/*
 * adfEnvCleanUp
 *
 */
void adfEnvCleanUp(void)
{
    memset( &adfEnv, 0, sizeof(struct AdfEnv) );
}


/*
 * adfEnvSetProperty
 *
 */
ADF_RETCODE adfEnvSetProperty( const ADF_ENV_PROPERTY  property,
                               const intptr_t          newval )
{
    switch ( property ) {
    case ADF_PR_VFCT:
        adfEnv.vFct = (AdfLogFct) newval;
        break;
    case ADF_PR_WFCT:
        adfEnv.wFct = (AdfLogFct) newval;
        break;
    case ADF_PR_EFCT:
        adfEnv.eFct = (AdfLogFct) newval;
        break;
    case ADF_PR_NOTFCT:
        adfEnv.notifyFct = (AdfNotifyFct) newval;
        break;
    case ADF_PR_USE_NOTFCT:
        adfEnv.useNotify = (bool) newval;
        break;
    case ADF_PR_PROGBAR:
        adfEnv.progressBar = (AdfProgressBarFct) newval;
        break;
    case ADF_PR_USE_PROGBAR:
        adfEnv.useProgressBar = (bool) newval;
        break;
    case ADF_PR_USE_RWACCESS:
        adfEnv.useRWAccess = (bool) newval;
        break;
    case ADF_PR_RWACCESS:
        adfEnv.rwhAccess = (AdfRwhAccessFct) newval;
        break;
    case ADF_PR_USEDIRC:
        adfEnv.useDirCache = (bool) newval;
        break;
    case ADF_PR_IGNORE_CHECKSUM_ERRORS:
        adfEnv.ignoreChecksumErrors =  (bool) newval;
        break;
    case ADF_PR_QUIET:
        adfEnv.quiet =  (bool) newval;
        break;
    default:
        adfEnv.eFct( "%s: invalid property %d", __func__, property );
        return ADF_RC_ERROR;
    }
    return ADF_RC_OK;
}


intptr_t adfEnvGetProperty( const ADF_ENV_PROPERTY  property )
{
    switch ( property ) {
    case ADF_PR_VFCT:                    return (intptr_t) adfEnv.vFct;
    case ADF_PR_WFCT:                    return (intptr_t) adfEnv.wFct;
    case ADF_PR_EFCT:                    return (intptr_t) adfEnv.eFct;
    case ADF_PR_NOTFCT:                  return (intptr_t) adfEnv.notifyFct;
    case ADF_PR_USE_NOTFCT:              return (intptr_t) adfEnv.useNotify;
    case ADF_PR_PROGBAR:                 return (intptr_t) adfEnv.progressBar;
    case ADF_PR_USE_PROGBAR:             return (intptr_t) adfEnv.useProgressBar;
    case ADF_PR_USE_RWACCESS:            return (intptr_t) adfEnv.useRWAccess;
    case ADF_PR_RWACCESS:                return (intptr_t) adfEnv.rwhAccess;
    case ADF_PR_USEDIRC:                 return (intptr_t) adfEnv.useDirCache;
    case ADF_PR_IGNORE_CHECKSUM_ERRORS:  return (intptr_t) adfEnv.ignoreChecksumErrors;
    case ADF_PR_QUIET:                   return (intptr_t) adfEnv.quiet;
    default:
        adfEnv.eFct( "%s: invalid property %d", __func__, property );
    }
    return 0;
}


/*
 *  adfEnvSetFct
 *
 */
void adfEnvSetFct( const AdfLogFct     eFct,
                   const AdfLogFct     wFct,
                   const AdfLogFct     vFct,
                   const AdfNotifyFct  notifyFct )
{
    if ( eFct != NULL )
        adfEnv.eFct = eFct;
    if ( wFct != NULL )
        adfEnv.wFct = wFct;
    if ( vFct != NULL )
        adfEnv.vFct = vFct;
    if ( notifyFct != NULL )
        adfEnv.notifyFct = notifyFct;
}


/*##################################################################################*/

static void rwHeadAccess( const ADF_SECTNUM  physical,
                          const ADF_SECTNUM  logical,
                          const bool         write )
{
    /* display the physical sector, the logical block, and if the access is read or write */
    fprintf( stderr, "phy %d / log %d : %c\n",
             physical, logical, write ? 'W' : 'R' );
}

static void progressBar( int perCentDone )
{
    fprintf( stderr, "%d %% done\n", perCentDone );
}

//static void Warning(char* msg) {
//    fprintf(stderr,"Warning <%s>\n",msg);
//}

//static void Error(char* msg) {
//    fprintf(stderr,"Error <%s>\n",msg);
/*    exit(1);*/
//}

//static void Verbose(char* msg) {
//    fprintf(stderr,"Verbose <%s>\n",msg);
//}


static void Warningf( const char * const  format, ... )
{
    if ( adfEnv.quiet )
        return;

    va_list ap;
    va_start( ap, format );

    fprintf( stderr, "Warning <" );
    vfprintf( stderr, format, ap );
    fprintf( stderr, ">\n" );
}


static void Errorf( const char * const  format, ... )
{
    if ( adfEnv.quiet )
        return;

    va_list ap;
    va_start( ap, format );

    fprintf( stderr, "Error <" );
    vfprintf( stderr, format, ap );
    fprintf( stderr, ">\n" );
/*    exit(1);*/
}


static void Verbosef( const char * const  format, ... )
{
    if ( adfEnv.quiet )
        return;

    va_list ap;
    va_start( ap, format );

    fprintf( stderr, "Verbose <" );
    vfprintf( stderr, format, ap );
    fprintf( stderr, ">\n" );
}


static void Changed( const ADF_SECTNUM  nSect,
                     const int          changedType )
{
    (void) nSect, (void) changedType;
/*    switch(changedType) {
    case ADF_ST_FILE:
        fprintf(stderr,"Notification : sector %ld (FILE)\n",nSect);
        break;
    case ADF_ST_DIR:
        fprintf(stderr,"Notification : sector %ld (DIR)\n",nSect);
        break;
    case ADF_ST_ROOT:
        fprintf(stderr,"Notification : sector %ld (ROOT)\n",nSect);
        break;
    default:
        fprintf(stderr,"Notification : sector %ld (???)\n",nSect);
    }
*/}
