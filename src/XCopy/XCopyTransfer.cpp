#include "XCopyTransfer.h"
#include "XCopyScratch.h"
#include "XCopySDCard.h"
#include "../FastCRC/FastCRC.h"
#include <Streaming.h>

void XCopyTransfer::begin(Stream *link, XCopyGraphics *graphics)
{
    _link = link;
    _graphics = graphics;
}

bool XCopyTransfer::fail(const char *reason)
{
    _link->printf(XFER_REPLY_ERROR ",%s\n", reason);
    Serial << "Error: " << reason << "\r\n";
    return false;
}

bool XCopyTransfer::sendFile(const String &path)
{
    Serial << "Sending file: '" << path << "'\r\n";

    // Cursor over the shared card, not an owner of it -- see XCopySdFat.h.
    XCopySDCard sdCard;

    // The download reply is a bare, unterminated "error": the ESP reads the size line
    // with readStringUntil('\n') and falls back to its own read timeout. Only one
    // status line is ever sent, so a second would be read as file data.
    if (!sdCard.cardDetect() || !sdCard.begin()) {
        _link->print(XFER_REPLY_ERROR);
        Serial << sdCard.getError() + "\r\n";
        return false;
    }

    FatFile file;
    if (!file.open(path.c_str())) {
        _link->print(XFER_REPLY_ERROR);
        Serial << "SD file open failed";
        return false;
    }

    if (_graphics != nullptr) {
        _graphics->clearScreen();
        _graphics->bmpDraw("XCPYLOGO.BMP", 0, 30);
        _graphics->drawText(44, 85, ST7735_GREEN, "Sending File", true);
    }

    // send file size
    size_t size = file.fileSize();
    _link->print(size);
    _link->print("\n");

    // Neither static nor stack: both come out of the same ~6KB left between the top
    // of the heap and _estack, so the earlier back and forth between them only moved
    // which end ran out first. The idle track buffer costs nothing from it. See
    // XCopyScratch.h.
    const size_t bufferSize = 2048;
    char *buffer = (char *)XCopyScratch::borrow("transfer.sendFile", bufferSize);
    if (buffer == nullptr) {
        _link->print(XFER_REPLY_ERROR);
        Serial << "Track buffer busy, cannot send now";
        file.close();
        return false;
    }
    int readsize = 0;

    unsigned long time = millis();

    while (true) {
        // read() returns -1 on error. The old loop only tested readsize after the
        // write, so a read failure became write(buffer, (size_t)-1).
        readsize = file.read(buffer, bufferSize);
        if (readsize <= 0) break;
        _link->write((const uint8_t *)buffer, readsize);
        Serial.print(".");
        delay(75);
    }

    Serial << "\r\nSent file '";
    file.printName();
    Serial << "': " << file.fileSize() << " in " << (millis() - time) / 1000.0f << "s\r\n";

    XCopyScratch::release((const uint8_t *)buffer);
    file.close();

    Serial.println("Done");

    return true;
}

bool XCopyTransfer::getFile(const String &path, size_t filesize, bool overwrite)
{
    Serial << "Getting file: '" << path << "' (" << filesize << ")\r\n";

    XCopySDCard sdCard;

    // One status line, always. The old code used independent if()s and could emit
    // several error lines; the ESP read one and the rest were left in the buffer to be
    // misread as file data or as console commands.
    if (!sdCard.cardDetect())                      return fail(XFER_ERR_DETECT);
    if (!sdCard.begin())                           return fail(XFER_ERR_INIT);
    if (sdCard.fileExists(path) && !overwrite)     return fail(XFER_ERR_EXISTS);

    FatFile file;
    // O_TRUNC matters once overwrite is allowed: without it a shorter re-upload would
    // leave the tail of the previous file behind.
    if (!file.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) return fail(XFER_ERR_OPEN);

    // Sent BEFORE any drawing. bmpDraw() reads a bitmap off the SD card and blits it over
    // SPI, which can outrun the ESP's handshake timeout and leave it streaming into a
    // Teensy that is not yet listening.
    _link->print(XFER_REPLY_OK "\n");

    if (_graphics != nullptr) {
        _graphics->clearScreen();
        _graphics->bmpDraw("XCPYLOGO.BMP", 0, 30);
        _graphics->drawText(42, 85, ST7735_GREEN, "Receiving File", true);
    }

    // Borrowed, not static: a permanent 1KB is 1KB off the gap the stack grows into.
    uint8_t *buffer = XCopyScratch::borrow("transfer.getFile", XFER_CHUNK);
    if (buffer == nullptr) {
        file.close();
        return fail(XFER_ERR_OPEN);
    }
    FastCRC32 CRC32;
    uint32_t crc = 0;
    bool firstChunk = true;
    size_t totalsize = 0;
    bool ok = true;
    uint32_t chunks = 0;

    unsigned long time = millis();
    _link->setTimeout(XFER_TIMEOUT);

    while (totalsize < filesize) {
        size_t want = filesize - totalsize;
        if (want > XFER_CHUNK) want = XFER_CHUNK;

        if (_link->readBytes((char *)buffer, want) != want) {
            Serial << "\r\nError: timeout after " << totalsize << " bytes\r\n";
            ok = false;
            break;
        }

        // ACK as soon as the chunk is in RAM, not after the SD write. The ESP can then
        // transmit chunk N+1 while we write chunk N, so per-chunk cost is max(tx, sd)
        // rather than tx + sd. Still overrun-proof: only one chunk is ever un-ACKed, so
        // peak Serial1 ring occupancy is XFER_CHUNK, well under SERIAL1_RX_BUFFER_SIZE.
        _link->write(XFER_ACK);

        if (file.write(buffer, want) != (int)want) {
            Serial << "\r\nError: SD write failed at " << totalsize << " bytes\r\n";
            ok = false;
            break;
        }

        crc = firstChunk ? CRC32.crc32(buffer, want) : CRC32.crc32_upd(buffer, want);
        firstChunk = false;
        totalsize += want;

        // Throttled: USB CDC writes can block when a host is attached but not reading.
        if ((++chunks & 0x3F) == 0) Serial.print(".");
    }

    _link->setTimeout(SERIAL_DEFAULT_TIMEOUT);

    file.sync();
    file.close();
    XCopyScratch::release(buffer);

    // Receipt. The byte count and CRC32 are what actually verify the transfer -- the
    // per-chunk ACK above is flow control only.
    if (ok) _link->printf(XFER_REPLY_DONE ",%u,%08lX\n", (unsigned)totalsize, (unsigned long)crc);
    else    _link->print(XFER_REPLY_ERROR "," XFER_ERR_WRITE "\n");

    Serial << "\r\nReceived '" << path << "': " << totalsize << "/" << filesize
           << " bytes, crc32 " << _HEX(crc) << ", in " << (millis() - time) / 1000.0f << "s\r\n";

    return ok;
}
