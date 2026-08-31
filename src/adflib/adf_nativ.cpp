/*
 * adf_nativ.c
 *
 * $Id$
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
#include<stdlib.h>
#include<string.h>
// Changed
#include <Arduino.h>
// Changed
#include"adf_str.h"
#include"adf_nativ.h"
#include"adf_err.h"

// Changed: the shared card. This file used to construct an SdFat of its own, which
// repointed FatFile's static cwd at a local and left it dangling on return.
#include "../XCopy/XCopySdFat.h"

extern struct Env adfEnv;

char *adfFileName;

/*
 * myInitDevice
 *
 * must fill 'dev->size'
 */
RETCODE myInitDevice(struct Device* dev, char* name, BOOL ro)
{
    // Serial.printf( "myInitDevice\r\n" );

    struct nativeDevice *nDev;

    nDev = (struct nativeDevice*)dev->nativeDev;

    nDev = (struct nativeDevice*)malloc(sizeof(struct nativeDevice));
    if (!nDev) {
        (*adfEnv.eFct)("myInitDevice : malloc");
        return RC_ERROR;
    }
    dev->nativeDev = nDev;
    if (!ro)
        /* check if device is writable, if not, force readOnly to TRUE */
        dev->readOnly = FALSE;
    else
        /* mount device as read only */
        dev->readOnly = TRUE;

    dev->size = 0;

    // Changed: XCopyADFLib::begin() has already mounted the card.
    if (xcopySd().exists(name))
    {
        adfFileName = name;

        dev->cylinders = 80;
        dev->heads = 2;
        dev->sectors = 11;
        dev->devType = DEVTYPE_FLOPDD;
        dev->isNativeDev = myIsDevNative(name);
        dev->readOnly = ro;
        dev->size = dev->cylinders * dev->heads * dev->sectors * 512;

        return RC_OK;
    }
    else
    {
        free(nDev);
        return RC_ERROR;
    }
}

/*
 * myReadSector
 *
 */
RETCODE myReadSector(struct Device *dev, int32_t n, int size, uint8_t* buf)
{
    // Serial.printf( "myReadSector\r\n" );
    int track = n / dev->sectors;
    int sector = n % dev->sectors;
	// Serial.printf("Read Track: file: %s %d Sector: %d n: %d size: %d seek: %d\r\n", adfFileName, track, sector, n, size, n * size);

    // Changed: was an SdFat constructed on the stack plus a full SD_begin(), then an
    // open/seek/close followed by a second open, then a read into a VLA that was
    // memcpy'd to buf -- all of it once per 512 byte sector. One open on the shared
    // card, read straight into the caller's buffer.
    File adfFile = xcopySd().open(adfFileName);
    if (!adfFile)
    {
        (*adfEnv.eFct)("myReadSector: could not open the ADF file");
        return RC_ERROR;
    }

    if (!adfFile.seek((uint32_t)n * size))
    {
        adfFile.close();
        (*adfEnv.eFct)("myReadSector: seek past end of the ADF file");
        return RC_ERROR;
    }

    int got = adfFile.read(buf, size);
    adfFile.close();

    // The old code returned RC_OK unconditionally, so a short or failed read left
    // the caller parsing whatever happened to be in its buffer.
    return got == size ? RC_OK : RC_ERROR;
}


/*
 * myWriteSector
 *
 */
RETCODE myWriteSector(struct Device *dev, int32_t n, int size, uint8_t* buf)
{
    Serial.printf( "myWriteSector\r\n" );
    return RC_OK;
}


/*
 * myReleaseDevice
 *
 * free native device
 */
RETCODE myReleaseDevice(struct Device *dev)
{
    // Serial.printf( "myReleaseDevice\r\n" );

    struct nativeDevice* nDev;

    nDev = (struct nativeDevice*)dev->nativeDev;

    free(nDev);

    // if (adfFile.isOpen())
    //     adfFile.close();
    // if (adfFile)
    //     adfFile.close();

    return RC_OK;
}



/*
 * adfInitNativeFct
 *
 */
void adfInitNativeFct()
{
    // Serial.printf( "adfInitNativeFct\r\n" );

    struct nativeFunctions *nFct;

    nFct = (struct nativeFunctions*)adfEnv.nativeFct;

    nFct->adfInitDevice = myInitDevice ;
    nFct->adfNativeReadSector = myReadSector ;
    nFct->adfNativeWriteSector = myWriteSector ;
    nFct->adfReleaseDevice = myReleaseDevice ;
    nFct->adfIsDevNative = myIsDevNative;
}


/*
 * myIsDevNative
 *
 */
BOOL myIsDevNative(char *devName)
{
  // Serial.printf( "myIsDevNative\r\n" );
  // return (strncmp(devName,"/dev/",5)==0);
  return true;
}
/*##########################################################################*/
