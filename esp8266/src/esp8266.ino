#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "ESPCommand.h"
#include <FS.h>
#include <WebSocketsServer.h>
#include <LITTLEFS.h>

#define ESPBaudRate 576000

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
const int led = 2;
const int busyPin = 4;
const int cancelPin = 13;
const String _marker = "espCommand";

ESPCommandLine command;

volatile int busyState = 0;
void IRAM_ATTR busyISR()
{
  busyState = digitalRead(busyPin);  
  String command = "pinStatus," + String(busyState);
  webSocket.broadcastTXT(command);
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
    int bufferSize = 2048;
    char buffer[bufferSize];
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
    Serial.printf("xcopyCommand,sendFile,%s\r\n", path.c_str());

    // get file size
    String ssize = "";
    ssize = Serial.readStringUntil('\n');
    ssize.replace("\n", "");
    if (ssize.startsWith("error")) {
      return false;
    }
    sscanf(ssize.c_str(), "%zu", &filesize);

    webSocket.broadcastTXT("download,start");

    // start http send
    server.setContentLength(filesize <= 0 ? CONTENT_LENGTH_UNKNOWN : filesize);
    // server.setContentLength(CONTENT_LENGTH_UNKNOWN);
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

      // timeout if no data received for 1 seconds
      if (millis() - lastDataTime > 1000) {
          // finish http send
          server.sendContent("");
          webSocket.broadcastTXT("download,end");
          return false;
      }
    }

    webSocket.broadcastTXT("download,end");
    
    return true;
  }

  return false;
}

bool handleFileUpload() {
  digitalWrite(led, 0);

  HTTPUpload& upload = server.upload();
  
  size_t filesize = 0;
  String ssize = server.arg("filesize");
  sscanf(ssize.c_str(), "%zu", &filesize);

  if (upload.status == UPLOAD_FILE_START) {
    String path = upload.filename;
    if(!path.startsWith("/")) path = "/"+path;

    // request file start
    Serial.print("\r\n");
    Serial.printf("xcopyCommand,getFile,%s,%d\r\n", path.c_str(), filesize);

    // file creation error?
    String response = "";
    response = Serial.readStringUntil('\n');
    response.replace("\n", "");  

    if (response.startsWith("error")) {
      response.replace("error,", "");
      webSocket.broadcastTXT(String("cancelUpload," + response).c_str());
      String url = "/#sdcard?fileerror=";
      url.concat(response);
      server.send(409, "text/plain", String("409: Conflict - Upload error: " + upload.filename + "(" + String(filesize) + ")").c_str());
      digitalWrite(led, 1);
      return false;
    }
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    Serial.write(upload.buf, upload.currentSize);
    delay(125);
  }
  else if (upload.status == UPLOAD_FILE_END) {
    server.send(200, "text/plain", String("200: OK - File uploaded: " + upload.filename + "(" + String(filesize) + ")").c_str());
  }

  digitalWrite(led, 1);
  return true;
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
      break;
    }
    // if new text data is received 
    case WStype_TEXT: {
      String cmd = (char *)payload;
      if (cmd == "ping") { webSocket.sendTXT(num, "pong"); }
      if (cmd.startsWith(_marker)) { 
        if (cmd = cmd.substring(_marker.length()+1) == "busyPin") busyISR(); 
        if (cmd = cmd.substring(_marker.length()+1) == "cancelPin") {
          digitalWrite(cancelPin, LOW);
          delay(50);
          digitalWrite(cancelPin, HIGH);
        }; 
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

  Serial.begin(ESPBaudRate);

  if (MDNS.begin("esp8266"))
    Serial.println("MDNS responder started");
  else
    Serial.println("MDNS responder failed to start");

  LittleFS.begin();
  Serial.println("LittleFS started");

  webSocket.begin();
  Serial.println("WebSockets server started");
  webSocket.onEvent(webSocketEvent);

  server.on("/upload", HTTP_GET, handleNotFound);
  server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleFileUpload);
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
  webSocket.loop();
  server.handleClient();
  MDNS.update();
  command.Update();
}
