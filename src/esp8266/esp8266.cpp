#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "ESPCommand.h"
#include <FS.h>
#include <WebSocketsServer.h>
#include <LITTLEFS.h>
#include <ArduinoOTA.h>

// Baud rate, timeouts and the file transfer wire format are the Teensy link contract.
// The same header is compiled into the Teensy tree, so there is nothing to keep in
// step by hand -- see the -I in platformio.ini.
#include "XCopyProtocol.h"

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
/*
   One name, used for DHCP, for mDNS and for over the air updates, so the device
   is called the same thing by the router, by a browser looking for xcopy.local
   and by an upload aimed at it.
*/
#define DEVICE_HOSTNAME "xcopy"

const int led = 2;
const int busyPin = 4;
const int cancelPin = 13;
const String _marker = "espCommand";

ESPCommandLine command;

volatile int busyState = 0;
volatile bool busyChanged = false;

/*
   Over the air updates.

   The ESP is otherwise only reachable for flashing through the Teensy serial
   passthrough - plug in, put the device in ESP Programming Mode, point the upload
   port at the Teensy. That path stays exactly as it was and is the recovery route
   when an update goes wrong, so nothing here replaces it.

   Both halves can come over the air: the sketch, and the LittleFS image the whole
   web interface is served from. PlatformIO picks between them by appending the
   filesystem flag to espota for the uploadfs target, so the two are one
   mechanism with two targets rather than two mechanisms.

   Deliberately begun from loop() rather than setup(). Wi-Fi credentials are
   applied by the "connect" console command and the SDK reconnects to a saved
   network on its own, so at the end of setup() there is usually no network yet
   for mDNS to announce on.
*/
bool otaStarted = false;

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

/*
   Wires up the over the air handlers. Called from setup(); the listener itself is
   not opened until loop() sees a network - see otaStarted.
*/
void setupOTA()
{
  // Same name MDNS.begin() announces, so the device answers to one hostname
  // whichever way it is reached: the web interface and OTA are both xcopy.
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);

  ArduinoOTA.onStart([]() {
    if (ArduinoOTA.getCommand() == U_FS)
    {
      /*
         The filesystem is written in place, straight into its partition, so it
         has to be unmounted first. Nothing in the core does this for you - the
         reference example marks this exact spot - and leaving it mounted means
         overwriting a live filesystem from underneath the handles into it.
      */
      Serial.println("OTA: filesystem update, unmounting LittleFS");
      LittleFS.end();
    }
    else
    {
      Serial.println("OTA: firmware update");
    }
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Every tenth, not every packet. Serial is the Teensy link, and the console
    // it feeds is the same one a filesystem image would flood with 2MB of dots.
    static unsigned int lastTenth = 11;
    if (total == 0)
      return;
    unsigned int tenth = (progress / (total / 10 + 1));
    if (tenth == lastTenth)
      return;
    lastTenth = tenth;
    Serial.printf("OTA: %u%%\r\n", (progress / (total / 100 + 1)));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA: complete, rebooting");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    // Named rather than numbered: the number on its own says nothing at the point
    // somebody is looking at a failed update.
    const char *reason = "unknown";
    switch (error)
    {
    case OTA_AUTH_ERROR:    reason = "auth failed"; break;
    case OTA_BEGIN_ERROR:   reason = "begin failed, image too large for the space"; break;
    case OTA_CONNECT_ERROR: reason = "connect failed"; break;
    case OTA_RECEIVE_ERROR: reason = "receive failed"; break;
    case OTA_END_ERROR:     reason = "end failed"; break;
    }
    Serial.printf("OTA: error %u, %s\r\n", error, reason);

    // A failed filesystem update left it unmounted, and the web interface is
    // served out of it. Put it back rather than waiting for a reboot nobody
    // knows to perform.
    if (ArduinoOTA.getCommand() == U_FS)
      LittleFS.begin();
  });
}

void setup(void)
{
  /*
     First, before anything that takes time.

     This is the name handed to the DHCP server, which is where a DNS server that
     learns its records from DHCP gets them - so it is what makes the device
     resolve as itself rather than as an ESP_XXXXXX derived from its MAC, and
     what an over the air upload is aimed at.

     The SDK starts re-associating with a saved network at boot, on its own,
     before setup() runs. Association and the DHCP request that follows it take a
     second or two, so setting the name here beats them - but only just, which is
     why nothing slower is allowed above it. If the name ever does turn up stale,
     a power cycle settles it.
  */
  WiFi.hostname(DEVICE_HOSTNAME);

  /*
     Come back on our own after a reset.

     The Teensy pushes the network credentials over on its own boot, and for a
     long time that was the only thing that ever connected this radio - so
     resetting the ESP alone, which is exactly what flashing it does, left it off
     the network until the whole device was power cycled.

     The reason it could not reconnect by itself is that ESP8266 core 3.x
     defaults WiFi.persistent() to false, so the credentials the Teensy sends
     reach RAM and never flash. On the 2.x cores this project was written against
     the default was true, which is why the arrangement used to appear to work.
     The connect handler now stores them deliberately rather than by default, and
     this asks the radio to use what was stored.

     begin() with no arguments connects with the saved configuration, so the
     Teensy is the place the network is *configured* and no longer the thing that
     has to be running for the ESP to be *on* it.
  */
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();

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

  if (MDNS.begin(DEVICE_HOSTNAME))
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

  setupOTA();

  command.begin(&webSocket);
}

void loop(void)
{
  if (busyChanged) {
    busyChanged = false;
    sendBusyStatus();
  }

  // Opened the first time there is a network to announce on, not in setup():
  // the SDK reconnects to a saved network asynchronously and the "connect"
  // command can bring one up long afterwards.
  if (!otaStarted && WiFi.status() == WL_CONNECTED)
  {
    // false: MDNS.begin() has already run in setup(), and letting ArduinoOTA
    // call it again restarts the responder the web interface is found by.
    ArduinoOTA.begin(false);
    otaStarted = true;
    Serial.println("OTA ready");
  }

  if (otaStarted)
    ArduinoOTA.handle();

  webSocket.loop();
  server.handleClient();
  MDNS.update();
  command.Update();
}
