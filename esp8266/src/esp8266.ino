#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "ESPCommand.h"
#include <FS.h>
#include <WebSocketsServer.h>

#define ESPBaudRate 576000

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
const int led = 13;
const int busyPin = 4;
const String _marker = "espCommand";

ESPCommandLine command;

volatile int busyState = 0;
void ICACHE_RAM_ATTR busyISR()
{
  busyState = digitalRead(busyPin);  
  String tmp = "pinStatus," + String(busyState);
  webSocket.broadcastTXT(tmp);
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
  else if (filename.endsWith(".adf") == true)
    return "application/x-binary";
    
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

  if (SPIFFS.exists(path))
  {
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }

  if (path.startsWith("/sdcard/")) {   
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);

    int bufferSize = 2048;
    char buffer[bufferSize];
    size_t readSize = 0;
    size_t totalsize = 0;
    bool filesending = true;
    unsigned long lastDataTime = millis();

    if (path.startsWith("/sdcard")) {
      path = path.substring(7);
    }

    server.send(200, contentType.c_str(), "");
    Serial.printf("xcopyCommand,sendFile,%s\r\n", path.c_str());

    while (filesending) {
      while (Serial.available()) {
          lastDataTime = millis();

          size_t readSize = Serial.readBytes(buffer, 1024);
          if (readSize > 0) {
            totalsize += readSize;
            server.sendContent(buffer, readSize);
          }
      }

      // timeout if no data received for 1 seconds
      if (millis() - lastDataTime > 1000) {
          break;
      }
    }

    server.sendContent("");

    Serial.printf("xcopyCommand,sendEnd\r\n");

    return true;
  }

  return false;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{ // When a WebSocket message is received
  switch (type)
  {
  case WStype_DISCONNECTED: // if the websocket is disconnected
    Serial.printf("xcopyCommand,disconnect,%u\r\n", num);
    break;
  case WStype_CONNECTED:
  { // if a new websocket connection is established
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("xcopyCommand,connected,%u,%d.%d.%d.%d,%s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
  }
  break;
  case WStype_TEXT: // if new text data is received
  {
    String cmd = (char *)payload;
    if (cmd.startsWith(_marker))
    {
      cmd = cmd.substring(_marker.length()+1);
      if (cmd == "busyPin")
        busyISR();
    }
    else
    {
      Serial.printf("xcopyCommand,%s\r\n", cmd.c_str());
    }
    break;
  }
  }
}

void setup(void)
{
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  pinMode(busyPin, INPUT);
  attachInterrupt(busyPin, busyISR, CHANGE);

  Serial.begin(ESPBaudRate);

  if (MDNS.begin("esp8266"))
    Serial.println("MDNS responder started");
  else
    Serial.println("MDNS responder failed to start");

  SPIFFS.begin();
  Serial.println("SPIFFS started");

  webSocket.begin();
  Serial.println("WebSockets server started");
  webSocket.onEvent(webSocketEvent);

  server.onNotFound([]() {
    digitalWrite(led, 1);
    if (!handleFileRead(server.uri()))
      handleNotFound();
    digitalWrite(led, 0);
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
