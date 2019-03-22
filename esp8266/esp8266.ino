#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "ESPCommand.h"
#include <FS.h>   // Include the SPIFFS library

ESP8266WebServer server(80);

const int led = 13;
const int busyPin = 2;

ESPCommandLine command;

void busyISR()
{
  Serial.print("Busy Interrupt: ");
  Serial.print(digitalRead(busyPin));
  Serial.println();
}

String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  return false;                                         // If the file doesn't exist, return false
}

void setup(void) {
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  pinMode(busyPin, INPUT);
  attachInterrupt(busyPin, busyISR, CHANGE);

  Serial.begin(115200);

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  SPIFFS.begin();
    
  server.onNotFound([]() {                              // If the client requests any URI
    digitalWrite(led, 1);
    if (!handleFileRead(server.uri()))                  // send it if it exists
      handleNotFound();
    digitalWrite(led, 0);
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  server.handleClient();
  MDNS.update();
  command.Update();
}
