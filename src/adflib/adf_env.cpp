/*
 * ADF Library
 *
 * adf_env.c
 *
 *  $Id$
 *
 * library context and customization code
 *
 *  This file is part of ADFLib.
 *
 *  ADFLib is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ADFLib is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Foobar; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include<stdio.h>
#include <stdlib.h>

#include"adf_defs.h"
#include"adf_str.h"
#include"adf_nativ.h"
#include"adf_env.h"

#include"defendian.h"
#include <Arduino.h>

#define debugadf

union u{
    int32_t l;
    char c[4];
    };

ENV_DECLARATION;

bool envWarning = false;
bool envError = false;

void rwHeadAccess(SECTNUM physical, SECTNUM logical, BOOL write)
{
    /* display the physical sector, the logical block, and if the access is read or write */

#ifdef debugadf
    Serial.printf("phy %d / log %d : %c\r\n", physical, logical, write ? 'W' : 'R');
#endif
}

void progressBar(int perCentDone)
{
#ifdef debugadf
    Serial.printf("%d %% done\r\n",perCentDone);
#endif
}

void Warning(const char* msg) {
#ifdef debugadf
    Serial.printf("Warning <%s>\r\n",msg);
#endif
	envWarning = true;
}

void Error(const char* msg) {
#ifdef debugadf
    Serial.printf("Error <%s>\r\n",msg);
#endif
	envError = true;
}



bool adfError()
{
	return envError|envWarning;
}

void adfClearError()
{
	envWarning = false;
	envError = false;
}

void Verbose(const char* msg) {
#ifdef debugadf
    Serial.printf("Verbose <%s>\r\n",msg);
#endif
}

void Changed(SECTNUM nSect, int changedType)
{
/*    switch(changedType) {
    case ST_FILE:
        Serial.printf("Notification : sector %ld (FILE)\r\n",nSect);
        break;
    case ST_DIR:
        Serial.printf("Notification : sector %ld (DIR)\r\n",nSect);
        break;
    case ST_ROOT:
        Serial.printf("Notification : sector %ld (ROOT)\r\n",nSect);
        break;
    default:
        Serial.printf("Notification : sector %ld (???)\r\n",nSect);
    }
*/}

/*
 * adfInitEnv
 *
 */
void adfEnvInitDefault()
{
    // char str[80];
    union u val;

    /* internal checking */

    if (sizeof(short)!=2) 
        { Serial.printf("Compilation error : sizeof(short)!=2\r\n"); exit(1); }
    if (sizeof(int32_t)!=4) 
        { Serial.printf("Compilation error : sizeof(short)!=2\r\n"); exit(1); }
    if (sizeof(struct bEntryBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bEntryBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bRootBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bRootBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bDirBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bDirBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bBootBlock)!=1024)
        { Serial.printf("Internal error : sizeof(struct bBootBlock)!=1024\r\n"); exit(1); }
    if (sizeof(struct bFileHeaderBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bFileHeaderBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bFileExtBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bFileExtBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bOFSDataBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bOFSDataBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bBitmapBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bBitmapBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bBitmapExtBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bBitmapExtBlock)!=512\r\n"); exit(1); }
    if (sizeof(struct bLinkBlock)!=512)
        { Serial.printf("Internal error : sizeof(struct bLinkBlock)!=512\r\n"); exit(1); }

    val.l=1L;
/* if LITT_ENDIAN not defined : must be BIG endian */
#ifndef LITT_ENDIAN
    if (val.c[3]!=1) /* little endian : LITT_ENDIAN must be defined ! */
        { Serial.printf("Compilation error : #define LITT_ENDIAN must exist\r\n"); exit(1); }
#else
    if (val.c[3]==1) /* big endian : LITT_ENDIAN must not be defined ! */
        { Serial.printf("Compilation error : #define LITT_ENDIAN must not exist\r\n"); exit(1); }
#endif

    adfEnv.wFct = Warning;
    adfEnv.eFct = Error;
    adfEnv.vFct = Verbose;
    adfEnv.notifyFct = Changed;
    adfEnv.rwhAccess = rwHeadAccess;
    adfEnv.progressBar = progressBar;
	
    adfEnv.useDirCache = FALSE;
    adfEnv.useRWAccess = FALSE;
    adfEnv.useNotify = FALSE;
    adfEnv.useProgressBar = FALSE;

    Serial.printf ("ADFlib %s (%s)\r\r\n",adfGetVersionNumber(),adfGetVersionDate());
    // (*adfEnv.vFct)(str);

    adfEnv.nativeFct=(struct nativeFunctions*)malloc(sizeof(struct nativeFunctions));
    if (!adfEnv.nativeFct) (*adfEnv.wFct)("adfInitDefaultEnv : malloc");

    adfInitNativeFct();
}


/*
 * adfEnvCleanUp
 *
 */
void adfEnvCleanUp()
{
    free(adfEnv.nativeFct);
}


/*
 * adfChgEnvProp
 *
 * compilation warnings
 * adf_env.c: In function adfChgEnvProp:
 * adf_env.c:176: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:179: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:182: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:185: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:192: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:203: warning: ISO C forbids conversion of object pointer to function pointer type
 *
 */
void adfChgEnvProp(int prop, void *newInt)
{
	BOOL *newBool;
/*    int *newInt;*/

    switch(prop) {
    case PR_VFCT:
        adfEnv.vFct = (void(*)(const char*))newInt;
        break;
    case PR_WFCT:
        adfEnv.wFct = (void(*)(const char*))newInt;
        break;
    case PR_EFCT:
        adfEnv.eFct = (void(*)(const char*))newInt;
        break;
    case PR_NOTFCT:
        adfEnv.notifyFct = (void(*)(SECTNUM,int))newInt;
        break;
    case PR_USE_NOTFCT:
        newBool = (BOOL*)newInt;
		adfEnv.useNotify = *newBool;        
        break;
    case PR_PROGBAR:
        adfEnv.progressBar = (void(*)(int))newInt;
        break;
    case PR_USE_PROGBAR:
        newBool = (BOOL*)newInt;
		adfEnv.useProgressBar = *newBool;        
        break;
    case PR_USE_RWACCESS:
        newBool = (BOOL*)newInt;
		adfEnv.useRWAccess = *newBool;        
        break;
    case PR_RWACCESS:
        adfEnv.rwhAccess = (void(*)(SECTNUM,SECTNUM,BOOL))newInt;
        break;
    case PR_USEDIRC:
        newBool = (BOOL*)newInt;
		adfEnv.useDirCache = *newBool;
        break;
    }
}

/*
 *  adfSetEnv
 *
 */
void adfSetEnvFct( void(*eFct)(const char*), void(*wFct)(const char*), void(*vFct)(const char*),
    void(*notFct)(SECTNUM,int)  )
{
    if (*eFct!=0)
		adfEnv.eFct = *eFct;
    if (*wFct!=0)
		adfEnv.wFct = *wFct;
    if (*vFct!=0)
		adfEnv.vFct = *vFct;
    if (*notFct!=0)
        adfEnv.notifyFct = *notFct;
}


/*
 * adfGetVersionNumber
 *
 */
const char* adfGetVersionNumber()
{
	return(ADFLIB_VERSION);
}


/*
 * adfGetVersionDate
 *
 */
const char* adfGetVersionDate()
{
	return(ADFLIB_DATE);
}




/*##################################################################################*/
