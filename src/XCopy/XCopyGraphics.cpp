#include "XCopyGraphics.h"

void XCopyGraphics::begin(TFT_ST7735 *tft)
{
    _tft = tft;
}

void XCopyGraphics::drawHeader()
{
    bmpDraw("XCPYLOGO.BMP", 0, 0);
}

void XCopyGraphics::drawTrack(uint8_t track, uint8_t side, bool drawText, bool retry, int retryCount, bool verify, uint16_t color)
{
    const uint8_t blockSize = 6;
    if (drawText)
    {
        this->drawText(0, 10, ST7735_WHITE, "TRACK: ", true);
        this->drawText(ST7735_YELLOW, String(track));
        this->drawText(67, 10, WHITE, "SIDE: ");
        this->drawText(ST7735_YELLOW, String(side));

        if (retry)
        {
            this->drawText(115, 10, ST7735_RED, "RETRY " + String(retryCount));
        }

        if (verify)
        {
            this->drawText(115, 10, ST7735_GREEN, "VERIFY");
        }
    }

    const int yoffset = 25;
    const int xoffset = 5;
    const int sidegap = 10;
    const int tracksperline = 10;
    const int col = track / tracksperline;
    const int row = track - (col * tracksperline);

    int x = (row * (blockSize + 1)) + xoffset + (side == 1 ? ((blockSize + 1) * tracksperline) + sidegap : 0);
    int y = (col * (blockSize + 1)) + yoffset;

    _tft->fillRect(x, y, blockSize, blockSize, color);
}

void XCopyGraphics::drawDiskName(String name)
{
    drawText(0, 0, ST7735_WHITE, "DISKNAME: ", true);
    drawText(ST7735_YELLOW, name);
}

void XCopyGraphics::drawDisk(uint8_t start, uint16_t color)
{
    for (int x = start; x < 160; x++)
    {
        if (x % 2 == 0)
            drawTrack(x / 2, 0, false, false, 0, false, color);
        else
            drawTrack(x / 2, 1, false, false, 0, false, color);
    }
}

void XCopyGraphics::drawText(uint8_t x, uint8_t y, uint16_t color, String text, bool clearLine)
{
    if (clearLine)
        _tft->fillRect(0, y, _tft->width(), 10, ST7735_BLACK);

    _tft->setCursor(x, y);
    _tft->setTextColor(color);
    _tft->print(text);
}

void XCopyGraphics::drawText(uint16_t color, String text)
{
    _tft->setTextColor(color);
    _tft->print(text);
}

void XCopyGraphics::drawText(String text)
{
    _tft->print(text);
}

uint16_t XCopyGraphics::LerpRGB(uint16_t a, uint16_t b, float t)
{
    uint8_t ar = 0;
    uint8_t ag = 0;
    uint8_t ab = 0;
    uint8_t br = 0;
    uint8_t bg = 0;
    uint8_t bb = 0;

    _tft->Color565ToRGB(a, ar, ag, ab);
    _tft->Color565ToRGB(b, br, bg, bb);

    uint8_t cr = ar + (br - ar) * t;
    uint8_t cg = ag + (bg - ag) * t;
    uint8_t cb = ab + (bb - ab) * t;

    return _tft->Color565(cr, cg, cb);
}

void XCopyGraphics::bmpDraw(const char *filename, uint16_t x, uint16_t y)
{
    SerialFlashFile flashFile;
    int bmpWidth, bmpHeight;            // W+H in pixels
    uint8_t bmpDepth;                   // Bit depth (currently must be 24)
    uint32_t bmpImageoffset;            // Start of image data in file
    uint32_t rowSize;                   // Not always = bmpWidth; may have padding
    uint8_t sdbuffer[3 * BUFFPIXEL];    // pixel buffer (R+G+B per pixel)
    uint8_t buffidx = sizeof(sdbuffer); // Current position in sdbuffer
    boolean goodBmp = false;            // Set to true on valid header parse
    boolean flip = true;                // BMP is stored bottom-to-top
    int w, h, row, col;
    uint8_t r, g, b;
    uint32_t pos = 0;

    if ((x >= _tft->width()) || (y >= _tft->height()))
        return;

    // Serial.println();
    // Serial.print(F("Loading image '"));
    // Serial.print(filename);
    // Serial.println('\'');

    // flashFile.close(); // FIX: Whats This? Why close a file thats not open?
    if ((flashFile = SerialFlash.open(filename)) == NULL)
    {
        Serial.print(F("File not found"));
        return;
    }

    // Parse BMP header
    // Serial.println("POS: " + String(flashFile.position()));
    if (read16(flashFile) == 0x4D42)
    { // BMP signature

        (void)read32(flashFile); // read & ignore filesize
        // Serial.print(F("File size: "));
        // Serial.println(size);

        (void)read32(flashFile); // Read & ignore creator bytes

        bmpImageoffset = read32(flashFile); // Start of image data
        // Serial.print(F("Image Offset: "));
        // Serial.println(bmpImageoffset, DEC);
        // Read DIB header

        (void)read32(flashFile); // read & ignore headerSize
        // Serial.print(F("Header size: "));
        // Serial.println(headerSize);

        bmpWidth = read32(flashFile);
        bmpHeight = read32(flashFile);
        if (read16(flashFile) == 1)
        {                                 // # planes -- must be '1'
            bmpDepth = read16(flashFile); // bits per pixel
            // Serial.print(F("Bit Depth: "));
            // Serial.println(bmpDepth);
            if ((bmpDepth == 24) && (read32(flashFile) == 0))
            { // 0 = uncompressed

                goodBmp = true; // Supported BMP format -- proceed!
                // Serial.print(F("Image size: "));
                // Serial.print(bmpWidth);
                // Serial.print('x');
                // Serial.println(bmpHeight);

                // BMP rows are padded (if needed) to 4-byte boundary
                rowSize = (bmpWidth * 3 + 3) & ~3;

                // If bmpHeight is negative, image is in top-down order.
                // This is not canon but has been observed in the wild.
                if (bmpHeight < 0)
                {
                    bmpHeight = -bmpHeight;
                    flip = false;
                }

                // Crop area to be loaded
                w = bmpWidth;
                h = bmpHeight;
                if ((x + w - 1) >= _tft->width())
                    w = _tft->width() - x;
                if ((y + h - 1) >= _tft->height())
                    h = _tft->height() - y;

                //FIX: why isnt push working?
                // Set TFT address window to clipped image bounds
                // _tft->setAddrWindow(x, y, x + w - 1, y + h - 1);

                // alternative tries
                // _tft->setArea(x, y, x + w - 1, y + h - 1);
                // _tft->startPushData(x, y, x + w - 1, y + h - 1);

                for (row = 0; row < h; row++)
                { // For each scanline...

                    // Seek to start of scan line.  It might seem labor-
                    // intensive to be doing this on every line, but this
                    // method covers a lot of gritty details like cropping
                    // and scanline padding.  Also, the seek only takes
                    // place if the file position actually needs to change
                    // (avoids a lot of cluster math in SD library).
                    if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
                        pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
                    else // Bitmap is stored top-to-bottom
                        pos = bmpImageoffset + row * rowSize;
                    if (flashFile.position() != pos)
                    { // Need seek?
                        flashFile.seek(pos);
                        buffidx = sizeof(sdbuffer); // Force buffer reload
                    }

                    for (col = 0; col < w; col++)
                    { // For each pixel...
                        // Time to read more pixel data?
                        if (buffidx >= sizeof(sdbuffer))
                        { // Indeed
                            flashFile.read(sdbuffer, sizeof(sdbuffer));
                            buffidx = 0; // Set index to beginning
                        }

                        // Convert pixel from BMP to TFT format, push to display
                        b = sdbuffer[buffidx++];
                        g = sdbuffer[buffidx++];
                        r = sdbuffer[buffidx++];

                        // _tft->push(_tft->Color565(r, g, b));

                        // alternative tries
                        _tft->drawPixel(x + col, y + row, _tft->Color565(r, g, b));
                        // _tft->pushColor(_tft->Color565(r, g, b));
                        // _tft->pushData(_tft->Color565(r, g, b));

                    } // end pixel
                    // alternative tries
                    // _tft->endPushData();
                } // end scanline
                // Serial.print(F("Loaded in "));
                // Serial.print(millis() - startTime);
                // Serial.println(" ms");
            } // end goodBmp
        }
    }

    flashFile.close();

    offset = 0;

    if (!goodBmp)
        Serial.println(F("BMP format not recognized."));
}

void XCopyGraphics::rawDraw(const char *filename, uint16_t x, uint16_t y)
{
    SerialFlashFile bmpFile;
    int bmpWidth, bmpHeight; // W+H in pixels
    uint8_t bmpDepth;        // Bit depth (currently must be 24)
    uint8_t headerSize;
    uint32_t bmpImageoffset; // Start of image data in file
    uint32_t rowSize;        // Not always = bmpWidth; may have padding
    uint32_t fileSize;
    boolean goodBmp = false; // Set to true on valid header parse
    boolean flip = true;     // BMP is stored bottom-to-top
    uint16_t w, h, row, col;
    uint8_t r, g, b;
    uint32_t pos = 0, startTime;

    if ((x >= _tft->width()) || (y >= _tft->height()))
        return;

    // startTime = millis();

    // if (!bmpFile.open(filename, O_READ))
    // Open requested file on SD card
    if ((bmpFile = SerialFlash.open(filename)) == NULL)
    {
        Serial.print(F("File not found\r\n"));
        return;
    }

    // Parse BMP header
    if (read16(bmpFile) == 0x4D42)
    { // BMP signature
        fileSize = read32(bmpFile);
        (void)read32(bmpFile);            // Read & ignore creator bytes
        bmpImageoffset = read32(bmpFile); // Start of image data
        headerSize = read32(bmpFile);
        bmpWidth = read32(bmpFile);
        bmpHeight = read32(bmpFile);
        if (read16(bmpFile) == 1)
        {                               // # planes -- must be '1'
            bmpDepth = read16(bmpFile); // bits per pixel
            if (read32(bmpFile) == 0) // 0 = uncompressed
            {
                if (bmpHeight < 0)
                {
                    bmpHeight = -bmpHeight;
                    flip = false;
                }

                // Crop area to be loaded
                w = bmpWidth;
                h = bmpHeight;
                if ((x + w - 1) >= _tft->width())
                    w = _tft->width() - x;
                if ((y + h - 1) >= _tft->height())
                    h = _tft->height() - y;

                // Set TFT address window to clipped image bounds
                _tft->startPushData(x, y, x + w - 1, y + h - 1);

                if (bmpDepth == 16) //565 format
                {
                    goodBmp = true; // Supported BMP format -- proceed!

                    uint16_t buffer[BUFFPIXELCOUNT]; // pixel buffer

                    bmpFile.seek(54); //skip header
                    uint32_t totalPixels = (uint32_t)bmpWidth * (uint32_t)bmpHeight;
                    uint16_t numFullBufferRuns = totalPixels / BUFFPIXELCOUNT;

                    for (uint32_t p = 0; p < numFullBufferRuns; p++)
                    {                        
                        // read pixels into the buffer
                        bmpFile.read(buffer, 2 * BUFFPIXELCOUNT);
                        // push them to the diplay
                        for (int i = 0; i < BUFFPIXELCOUNT; i++)
                        {
                            _tft->pushColor(buffer[i]);
                        }
                    }

                    // render any remaining pixels that did not fully fit the buffer
                    uint32_t remainingPixels = totalPixels % BUFFPIXELCOUNT;
                    if (remainingPixels > 0)
                    {
                        bmpFile.read(buffer, 2 * remainingPixels);
                        for (int i = 0; i < BUFFPIXELCOUNT; i++)
                        {                            
                            _tft->pushColor(buffer[i]);
                        }
                    }
                }
                else
                {
                    Serial << "Unsupported Bit Depth.\r\n";
                }

                _tft->endPushData();

                // if (goodBmp)
                // {
                //     Serial.print("Loaded in ");
                //     Serial.print(millis() - startTime);
                //     Serial.println(" ms");
                // }
            }
        }
    }

    offset = 0;

    bmpFile.close();
    
    if (!goodBmp)
        Serial.print("565 format not recognized.\r\n");
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t XCopyGraphics::read16(SerialFlashFile f)
{
    uint16_t result;

    char buffer[2];
    f.seek(offset);
    f.read(buffer, 2);
    offset = offset + 2;

    /*
    Serial.println("\nstart16:\n");
    Serial.println(buffer[0], HEX);
    Serial.println(buffer[1], HEX);
    Serial.println("\nend\n");
    */

    ((uint8_t *)&result)[0] = (uint16_t)buffer[0]; // LSB
    ((uint8_t *)&result)[1] = (uint16_t)buffer[1]; // MSB
    return result;
}

uint32_t XCopyGraphics::read32(SerialFlashFile f)
{
    uint32_t result;

    char buffer[4];
    f.seek(offset);
    f.read(buffer, 4);
    offset = offset + 4;

    /*
    Serial.println("\nstart32:\n");
    Serial.println(buffer[0], HEX);
    Serial.println(buffer[1], HEX);
    Serial.println(buffer[2], HEX);
    Serial.println(buffer[3], HEX);
    Serial.println("\nend\n");
    */

    ((uint8_t *)&result)[0] = (uint16_t)buffer[0]; // LSB
    ((uint8_t *)&result)[1] = (uint16_t)buffer[1];
    ((uint8_t *)&result)[2] = (uint16_t)buffer[2];
    ((uint8_t *)&result)[3] = (uint16_t)buffer[3]; // MSB
    return result;
}
