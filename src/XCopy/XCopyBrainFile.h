#ifndef XCOPYBRAINFILE_H
#define XCOPYBRAINFILE_H

#define brainfilename "BRAINF~1.JSO"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SdFat.h>
#include "XCopySDCard.h"
#include "XCopyConsole.h"

class XCopyBrainFile {
public:
  /**
   * @brief Check bootblock against brain file recognition pattern
   *        Recognition pattern is 'offset,value' pairs as ascii dec values.
   *        Offset is the byte offset into the bootblock data.
   * 
   * @param recog brain file recognition pattern for boot block
   * @param sector0 512 byte array containing bootblock sector 0
   * @param sector0 512 byte array containing bootblock sector 1
   * 
   * @result true if bootblock matches recognition pattern
   */
  static bool checkBootBlock(String recog, byte sector0[], byte sector1[]) {
    bool found = true;

    while (recog.indexOf(",") != -1 && found == true) {
      int offset = recog.substring(0, recog.indexOf(",")).toInt();
      recog = recog.substring(recog.indexOf(",")+1);
      
      int value = recog.substring(0, recog.indexOf(",")).toInt();
      recog = recog.substring(recog.indexOf(",")+1);

      if (offset < 512) {
        found = sector0[offset] == value;
      }
      else {
        found = sector1[offset-512] == value;
      }
    }

    return found;
  }

  /**
   * @brief Return boot block class name based on supplied brain file class
   * 
   * @param bbclass brain file class
   * 
   * @result full class name
   */
  static String bootBlockClass(String bbclass) {
    if (bbclass == "s")   return "Standard";
    if (bbclass == "u")   return "Utility";
    if (bbclass == "v")   return "Virus";
    if (bbclass == "g")   return "Boot Game";
    if (bbclass == "bl")  return "Loader";
    if (bbclass == "l")   return "Logo";
    if (bbclass == "i")   return "Intro";
    if (bbclass == "p")   return "Copy Protection";
    if (bbclass == "xc")  return "XCopy";
    if (bbclass == "sc")  return "Scroller";
    if (bbclass == "bm")  return "Bootmenu";
    if (bbclass == "rs")  return "Ram Switch";
    if (bbclass == "vfm") return "Virus Free Memory Message";
    if (bbclass == "ds")  return "Demoscene";
    if (bbclass == "ga")  return "Game";
    if (bbclass == "bom") return "Boot Message";

    return "Unknown";
  }

  /**
   * @brief Return boot block console color based on supplied brain file class
   * 
   * @param bbclass brain file class
   * 
   * @result console escape sequence for relevant color
   */
  static String bootBlockColor(String bbclass) {
    if (bbclass == "s")   return XCopyConsole::green();
    if (bbclass == "u")   return XCopyConsole::green();
    if (bbclass == "v")   return XCopyConsole::bold_red();
    if (bbclass == "g")   return XCopyConsole::yellow();
    if (bbclass == "bl")  return XCopyConsole::yellow();
    if (bbclass == "l")   return XCopyConsole::purple();
    if (bbclass == "i")   return XCopyConsole::yellow();
    if (bbclass == "p")   return XCopyConsole::green();
    if (bbclass == "xc")  return XCopyConsole::green();
    if (bbclass == "sc")  return XCopyConsole::yellow();
    if (bbclass == "bm")  return XCopyConsole::green();
    if (bbclass == "rs")  return XCopyConsole::green();
    if (bbclass == "vfm") return XCopyConsole::green();
    if (bbclass == "ds")  return XCopyConsole::green();
    if (bbclass == "ga")  return XCopyConsole::green();
    if (bbclass == "bom") return XCopyConsole::yellow();

    return "Unknown";
  }

  /**
   * @brief Display the brain file boot block data for the supplied brain file entry
   * 
   * @param bbclass JSON document for brain file entry
   */
  static void displayBootBlock(StaticJsonDocument<512> doc) {
      String name = doc["Name"].as<const char*>();
      String bbclass = doc["Class"].as<const char*>();
      String notes = doc["Notes"].as<const char*>();
      bool bootable = doc["Bootable"];
      String ks = doc["KS"].as<const char*>();

      Log << "\r\nBoot Block Identified:\r\nName: " + name + "\r\n";
      Log << "Class: " + bootBlockColor(bbclass) + bootBlockClass(bbclass) + XCopyConsole::reset() + "\r\n";
      if (notes != "") Log << "Notes: '" + notes + "'\r\n";
      Log << "Bootable: " + String(bootable ? "True" : "False") + "\r\n";
      Log << "Kickstart: " + ks + "\r\n\r\n";
  }

  /**
   * @brief Identify the supplied boot block and display details on the console
   * 
   * @param sector0 512 byte array containing bootblock sector 0
   * @param sector0 512 byte array containing bootblock sector 1
   * @param crc32 crc32 checksum of boot block data
   */
  static void identifyBootblock(byte sector0[], byte sector1[], uint32_t crc32) {
    if (!SerialFlash.begin(PIN_FLASHCS)) {
      Log << "Serial Flash failed to initialise.\r\n";
      return; 
    }

    SerialFlashFile brainfile = SerialFlash.open(brainfilename);
    if (!brainfile) {
      Log << "Brainfile failed to open.\r\n";
      return; 
    }

    char hexvalue[10];
    sprintf(hexvalue, "%08X", (unsigned int)crc32);    
    String bootcrc32 = String(hexvalue);
    byte brainBuffer[512];
    char json[512];
    memset(json, 0, 512);
    int jsonIndex = 0;
    bool copying = false;
    uint32_t bbcount = 0;
    uint32_t bytesread = 0;
    StaticJsonDocument<512> root;

    // loop through brainfile, find json blocks {...}, ignore anything between blocks
    //  and process each non empty entry as a brain file entry
    do {
       bytesread = brainfile.read(brainBuffer, sizeof(brainBuffer));

      for (uint32_t i=0; i < bytesread; i++) {
          if (brainBuffer[i] == 123) copying = true; // {

          if (copying) json[jsonIndex++] = brainBuffer[i];

          if (brainBuffer[i] == 125) { // }
            // process json block
            if (String(json) != "") {
              bbcount++;
  
              deserializeJson(root, json);

              String crc32 = root["CRC"].as<const char*>();
              String recog = root["Recog"].as<const char*>();

              if (crc32 == bootcrc32) {
                displayBootBlock(root);
              }
              else if (recog != "" && checkBootBlock(recog, sector0, sector1)) {
                displayBootBlock(root);
              };
            }

            // reset json block
            memset(json, 0, 512);
            jsonIndex = 0;
            copying = false; // stop copying until next { is found
          } 
      }
    } while (bytesread > 0);

    Log << "Scanned: " + String(bbcount) + " boot blocks\r\n";

    brainfile.close();
  }

  /**
   * @brief Identify the supplied boot block and display details on the console
   * 
   * @param sector0 512 byte array containing bootblock sector 0
   * @param sector0 512 byte array containing bootblock sector 1
   * @param crc32 crc32 checksum of boot block data
   */
  static bool exists() {
    if (!SerialFlash.begin(PIN_FLASHCS)) {
      return false; 
    }

    bool result = true;
    SerialFlashFile brainfile = SerialFlash.open(brainfilename);    
    if (!brainfile ) result = false;

    brainfile.close();
    return result;
  }
private:

};

#endif // XCOPYBRAINFILE