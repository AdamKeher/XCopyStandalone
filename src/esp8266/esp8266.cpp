#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "ESPCommand.h"
#include <FS.h>
#include <WebSocketsServer.h>
#include <LITTLEFS.h>

// Baud rate, timeouts and the file transfer wire format are the Teensy link contract.
// The same header is compiled into the Teensy tree, so there is nothing to keep in
// step by hand -- see the -I in platformio.ini.
#include "XCopyProtocol.h"

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
const int led = 2;
const int busyPin = 4;
const int cancelPin = 13;
const String _marker = "espCommand";

ESPCommandLine command;

volatile int busyState = 0;
volatile bool busyChanged = false;

// IRAM_ATTR only places this function in IRAM. String, lwIP and arduinoWebSockets all
// live in flash, and calling into flash from an interrupt while the SPI flash cache is
// disabled faults the chip. Sample the pin here, broadcast from loop().
void IRAM_ATTR busyISR()
{
  busyState = digitalRead(busyPin);
  busyChanged = true;
}

void sendBusyStatus()
{
  // broadcastTXT takes String by non-const reference, so this needs to be an lvalue.
  String payload = "pinStatus," + String(busyState);
  webSocket.broadcastTXT(payload);
}

String getContentType(String filename)
{ 
  filename.toLowerCase();

  if (filename.endsWith(".htm"))
    return "text/html";
  else if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  if (filename.endsWith(".text"))
    return "text/plain";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/x-pdf";
  else if (filename.endsWith(".zip"))
    return "application/x-zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  // else if (filename.endsWith(".adf") == true)
  //   return "application/x-binary";
    
  return "text/plain";
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

bool handleFileRead(String path)
{
  if (path.endsWith("/")) {
    path += "index.html";
  }

  String contentType = getContentType(path);

  if (LittleFS.exists(path))
  {
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }

  if (path.startsWith("/sdcard/")) {
    // static: 2048 bytes is half the cont stack this handler runs on.
    static const int bufferSize = 2048;
    static char buffer[bufferSize];
    size_t totalsize = 0;
    unsigned long lastDataTime = millis();
    size_t filesize = 0;

    // strip /sdcard prefix for local SD Card path
    if (path.startsWith("/sdcard")) {
      path = path.substring(7);
    }

    path = server.urlDecode(path);

    // request file
    Serial.print("\r\n");
    Serial.printf(XCOPY_COMMAND_MARKER XFER_CMD_SENDFILE ",%s\r\n", path.c_str());

    // get file size
    String ssize = "";
    ssize = Serial.readStringUntil('\n');
    ssize.replace("\n", "");
    if (ssize.startsWith(XFER_REPLY_ERROR)) {
      return false;
    }
    sscanf(ssize.c_str(), "%zu", &filesize);

    // Without a size the loop below exits on its first pass (0 >= 0) and the reply is
    // a 200 with an empty, never-terminated chunked body. Fail the request instead.
    if (filesize == 0) {
      return false;
    }

    webSocket.broadcastTXT("download,start");

    // start http send
    server.setContentLength(filesize);
    server.send(200, contentType.c_str(), "");

    // get file
    while (true) {
      while (Serial.available()) {
          lastDataTime = millis();
          size_t readSize = Serial.readBytes(buffer, bufferSize);
          totalsize += readSize;
          // send http data
          server.sendContent(buffer, readSize);
      }

      // exit all bytes of file received
      if (totalsize >= filesize) { break; }

      // give up once the Teensy has gone quiet for XFER_IDLE_TIMEOUT
      if (millis() - lastDataTime > XFER_IDLE_TIMEOUT) {
          // finish http send
          server.sendContent("");
          webSocket.broadcastTXT("download,end");
          return false;
      }
    }

    server.sendContent("");
    webSocket.broadcastTXT("download,end");

    return true;
  }

  return false;
}

static bool   uploadFailed = false;
static String uploadError = "";
static size_t uploadSize = 0;
static size_t uploadSent = 0;
static String uploadCrc = "";

// Blocks until the Teensy confirms the chunk is buffered, or we give up. yield() keeps the
// WiFi stack and the soft WDT fed; stalling here is exactly the backpressure we want, since
// it propagates up the TCP window to the browser instead of overrunning the UART.
static bool waitAck() {
  unsigned long t = millis();
  while (millis() - t < XFER_TIMEOUT) {
    if (Serial.available() && Serial.read() == XFER_ACK) return true;
    yield();
  }
  return false;
}

// Reads the expected size from a header rather than server.arg(): headers are parsed before
// the body and cannot be clobbered by multipart form parsing, which is core-version
// dependent. Falls back to the query string.
static size_t requestedFileSize() {
  size_t n = 0;
  String v = server.header("X-File-Size");
  if (v.length() == 0) v = server.arg("filesize");
  sscanf(v.c_str(), "%zu", &n);
  return n;
}

// NOTE: this handler must never call server.send(). Responding from here emits a second
// HTTP response on the same connection and closes the socket before the multipart trailer
// has been consumed, which the browser reports as a network error even though every byte
// arrived. handleUploadDone() sends the single response.
void handleFileUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    digitalWrite(led, 0);
    uploadFailed = false;
    uploadError = "";
    uploadSent = 0;
    uploadCrc = "";
    uploadSize = requestedFileSize();

    String path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;

    bool overwrite = (server.header("X-Overwrite") == "1");

    // The transfer is length-delimited, so a missing size would make the Teensy write a
    // zero-byte file and report success. Fail loudly instead.
    if (uploadSize == 0) {
      uploadFailed = true;
      uploadError = "nosize";
      webSocket.broadcastTXT("cancelUpload,nosize");
      digitalWrite(led, 1);
      return;
    }

    while (Serial.available()) Serial.read();   // drop stale bytes before the handshake

    Serial.print("\r\n");
    Serial.printf(XCOPY_COMMAND_MARKER XFER_CMD_GETFILE ",%s,%u,%d\r\n", path.c_str(), (unsigned)uploadSize, overwrite ? 1 : 0);

    Serial.setTimeout(XFER_HANDSHAKE_TIMEOUT);  // SD init + bmpDraw headroom
    String response = Serial.readStringUntil('\n');
    response.trim();

    if (response != XFER_REPLY_OK) {
      uploadFailed = true;
      uploadError = response.startsWith(XFER_REPLY_ERROR ",") ? response.substring(6)
                  : (response.length() ? response : "timeout");
      webSocket.broadcastTXT(String("cancelUpload," + uploadError).c_str());
      digitalWrite(led, 1);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed) return;                   // swallow the rest of the body quietly

    size_t off = 0;
    while (off < upload.currentSize) {
      size_t n = upload.currentSize - off;
      if (n > XFER_CHUNK) n = XFER_CHUNK;

      Serial.write(upload.buf + off, n);
      Serial.flush();

      if (!waitAck()) {
        uploadFailed = true;
        uploadError = "timeout";
        webSocket.broadcastTXT("cancelUpload,timeout");
        digitalWrite(led, 1);
        return;
      }
      off += n;
      uploadSent += n;
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (!uploadFailed) {
      Serial.setTimeout(XFER_TIMEOUT);
      String receipt = Serial.readStringUntil('\n');   // "done,<bytes>,<crc32>"
      receipt.trim();

      if (receipt.startsWith(XFER_REPLY_DONE ",")) {
        int last = receipt.lastIndexOf(',');
        size_t got = 0;
        sscanf(receipt.substring(5, last).c_str(), "%zu", &got);
        uploadCrc = receipt.substring(last + 1);

        if (uploadSize > 0 && got != uploadSize) {
          uploadFailed = true;
          uploadError = "short";
        }
      } else {
        uploadFailed = true;
        uploadError = receipt.startsWith(XFER_REPLY_ERROR ",") ? receipt.substring(6) : "noreceipt";
      }
    }
    // UPLOAD_FILE_START raised this to XFER_HANDSHAKE_TIMEOUT and the receipt read
    // above to XFER_TIMEOUT; without this the download path would inherit that
    // timeout on its size read.
    Serial.setTimeout(SERIAL_DEFAULT_TIMEOUT);
    digitalWrite(led, 1);
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    // Just stop sending -- the Teensy closes the file on its own read timeout.
    uploadFailed = true;
    uploadError = "aborted";
    Serial.setTimeout(SERIAL_DEFAULT_TIMEOUT);
    digitalWrite(led, 1);
  }
}

// The single response for the whole POST.
void handleUploadDone() {
  if (uploadFailed) {
    webSocket.broadcastTXT(String("cancelUpload," + uploadError).c_str());
    server.send(500, "application/json",
                String("{\"ok\":false,\"error\":\"") + uploadError + "\"}");
  } else {
    server.send(200, "application/json",
                String("{\"ok\":true,\"size\":") + String(uploadSent) +
                ",\"crc32\":\"" + uploadCrc + "\"}");
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{ // When a WebSocket message is received
  switch (type)
  {
    // if the websocket is disconnected
    case WStype_DISCONNECTED: {
      Serial.print("\r\n");
      Serial.printf("xcopyCommand,disconnect,%u\r\n", num);    
      break;
    }
    // if a new websocket connection is established
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.print("\r\n");
      Serial.printf("xcopyCommand,connected,%u,%d.%d.%d.%d,%s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      // Greet the client with the busy state instead of waiting for it to ask.
      // Targeted rather than sendBusyStatus(), which broadcasts -- the browsers
      // already connected have it.
      busyState = digitalRead(busyPin);
      String status = "pinStatus," + String(busyState);
      webSocket.sendTXT(num, status);
      break;
    }
    // if new text data is received 
    case WStype_TEXT: {
      String cmd = (char *)payload;
      // else if: the else below binds to this if, so "ping" was answered *and*
      // forwarded to the Teensy as "xcopyCommand,ping".
      if (cmd == "ping") { webSocket.sendTXT(num, "pong"); }
      else if (cmd.startsWith(_marker)) {
        // These were "if (cmd = ... == \"busyPin\")": an assignment, not a comparison.
        // The bool result was assigned to cmd and the if tested the resulting String,
        // which is always truthy, so every espCommand message fired both branches --
        // including a pulse on the cancel pin.
        String subcmd = cmd.substring(_marker.length() + 1);
        if (subcmd == "busyPin") {
          busyState = digitalRead(busyPin);
          sendBusyStatus();
        }
        if (subcmd == "cancelPin") {
          digitalWrite(cancelPin, LOW);
          delay(50);
          digitalWrite(cancelPin, HIGH);
        }
      }
      else { Serial.printf("\r\nxcopyCommand,%s\r\n", cmd.c_str()); }
      break;
    }
    default:
      break;
  }
}

void setup(void)
{
  pinMode(led, OUTPUT);
  digitalWrite(led, 1);
  pinMode(busyPin, INPUT);
  pinMode(cancelPin, OUTPUT);
  digitalWrite(cancelPin, HIGH);
  attachInterrupt(busyPin, busyISR, CHANGE);

  // Must precede begin(). The core default is 256 bytes; the SD *download* path blasts
  // 2048-byte bursts from the Teensy while server.sendContent() can block on TCP, so at
  // 1 Mbaud 256 bytes is only ~2.5ms of headroom. 2048 gives ~20ms.
  Serial.setRxBufferSize(2048);
  Serial.begin(ESPBaudRate);

  if (MDNS.begin("esp8266"))
    Serial.println("MDNS responder started");
  else
    Serial.println("MDNS responder failed to start");

  LittleFS.begin();
  Serial.println("LittleFS started");

  webSocket.begin();
  // A Wi-Fi drop tears the TCP connection down without a FIN, so without a
  // heartbeat the client slot stays allocated forever. There are only
  // WEBSOCKETS_SERVER_CLIENT_MAX (5) of them -- five silent drops and the server
  // stops accepting browsers at all, which from the browser side looks like a
  // reconnect that never succeeds. Ping every 5s, 3s to answer, disconnect after
  // two consecutive misses. Browsers answer protocol pings themselves, so this
  // needs nothing from the client.
  webSocket.enableHeartbeat(5000, 3000, 2);
  Serial.println("WebSockets server started");
  webSocket.onEvent(webSocketEvent);

  const char *uploadHeaders[] = { "X-File-Size", "X-Overwrite" };
  // (size_t) cast is required: with an int literal the variadic collectHeaders() overload
  // wins overload resolution and fails to compile on newer cores.
  server.collectHeaders(uploadHeaders, (size_t)2);

  server.on("/upload", HTTP_GET, handleNotFound);
  server.on("/upload", HTTP_POST, handleUploadDone, handleFileUpload);
  server.onNotFound([]() {
    digitalWrite(led, 0);
    if (!handleFileRead(server.uri()))
      handleNotFound();
    digitalWrite(led, 1);
  });
  server.begin();
  Serial.println("HTTP server started");

  command.begin(&webSocket);
}

void loop(void)
{
  if (busyChanged) {
    busyChanged = false;
    sendBusyStatus();
  }

  webSocket.loop();
  server.handleClient();
  MDNS.update();
  command.Update();
}
