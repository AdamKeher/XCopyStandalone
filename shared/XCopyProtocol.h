#ifndef XCOPYPROTOCOL_H
#define XCOPYPROTOCOL_H

/*
   The Teensy <-> ESP8266 link contract.

   This header is the single definition of the serial link between the two firmware
   trees and is compiled into both of them. They are two environments of the one
   PlatformIO project now, so both reach it by the same -I shared.

   Nothing here may be redefined locally in either tree. Values that used to be
   duplicated with a "must match" comment now live here only.

   ---------------------------------------------------------------------------------
   Command channel (ESP -> Teensy)

     xcopyCommand,<command>[,<arg>...]\r\n

   The ESP prefixes every line it wants the Teensy to act on with
   XCOPY_COMMAND_MARKER; XCopyESP8266::Update() strips the marker and hands the rest
   to XCopy::onWebCommand(). Lines without the marker are console output.

   ---------------------------------------------------------------------------------
   Download, SD card -> ESP -> browser (XFER_CMD_SENDFILE)

     ESP     xcopyCommand,sendFile,<path>\r\n
     Teensy  <size>\n                        decimal byte count, or "error"
     Teensy  <size> raw bytes                unframed, no ACKs
     ESP                                     streams straight into the HTTP response,
                                             gives up after XFER_IDLE_TIMEOUT of silence

   ---------------------------------------------------------------------------------
   Upload, browser -> ESP -> SD card (XFER_CMD_GETFILE)

     ESP     xcopyCommand,getFile,<path>,<size>,<overwrite>\r\n
     Teensy  OK\n                            or error,<reason>\n and nothing follows
     ESP     up to XFER_CHUNK raw bytes
     Teensy  XFER_ACK                        one byte, sent once the chunk is in RAM
             ... repeat until <size> bytes have been sent ...
     Teensy  done,<bytes>,<crc32>\n          or error,<reason>\n

   The ESP may never have more than one un-ACKed chunk outstanding, so peak Teensy
   Serial1 ring occupancy is XFER_CHUNK -- keep it well under SERIAL1_RX_BUFFER_SIZE
   (set in platformio.ini). The per-chunk ACK is flow control only; the byte count
   and CRC32 in the receipt are what actually verify the transfer.
*/

// 1,000,000 divides exactly on both ends: Teensy BAUD2DIV -> 192e6/192, ESP8266 ->
// 80e6/80. Zero divisor error on both sides, unlike the old 576000 (0.54% combined
// skew).
#define ESPBaudRate 1000000

// Serial1 drops to this while the ESP is flashed through the Teensy passthrough, so
// the data-link rate stays independent of what esptool has to cope with. Must match
// upload_speed on the d1_mini environment in platformio.ini.
#define ESPProgBaudRate 115200

// Arduino Stream default, restored after a transfer raises it. The pinned Teensy core
// has no Stream::getTimeout(), so the value is spelled out rather than saved.
#define SERIAL_DEFAULT_TIMEOUT 1000

// Prefix identifying a line the Teensy must act on rather than print.
#define XCOPY_COMMAND_MARKER "xcopyCommand,"

// File transfer
#define XFER_CHUNK 1024              // bytes per ACKed upload chunk
#define XFER_ACK 0x06                // ASCII ACK, one byte per received chunk
#define XFER_TIMEOUT 5000            // ms to wait for the next chunk / ACK / receipt
#define XFER_HANDSHAKE_TIMEOUT 8000  // ms for the getFile reply: SD init + bmpDraw headroom
#define XFER_IDLE_TIMEOUT 1000       // ms of silence that ends a stalled download

// Commands
#define XFER_CMD_SENDFILE "sendFile"
#define XFER_CMD_GETFILE "getFile"

// Replies. All are newline terminated; the ESP matches on the leading token.
#define XFER_REPLY_OK "OK"
#define XFER_REPLY_ERROR "error"
#define XFER_REPLY_DONE "done"

// Reasons carried by "error,<reason>". The ESP does not interpret these, it forwards
// them to the browser as "cancelUpload,<reason>", so the set is part of the contract.
#define XFER_ERR_DETECT "detect"   // no card in the slot
#define XFER_ERR_INIT "init"       // card would not mount
#define XFER_ERR_EXISTS "exists"   // file is there and overwrite was not asked for
#define XFER_ERR_OPEN "open"       // could not create the file
#define XFER_ERR_WRITE "write"     // transfer started but did not complete

#endif // XCOPYPROTOCOL_H
