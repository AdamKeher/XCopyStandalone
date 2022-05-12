#ifndef XCOPYMODFILE_H
#define XCOPYMODFILE_H

#include <Arduino.h>
#include <Streaming.h>
#include "XCopyFloppy.h"

class ModSample {
public:
  String name = "";
  int size = 0;
};

class ModInfo {
private:
  const int headerSize = 1080;

  void Samples() {
    int offset = 20;
    String sample_line;

    for (int i=0; i<31; i++) {
        // the websocket in the WebUI only accepts utf-8 text
        // if the module is corrupt or a false detection we must ensure that
        // non utf-8 characters are removed from the sample name or the websocket will crash
        char cleanname[22];
        memset(&cleanname, 0, 22);        
        for(int i=0; i<22; i++){
            cleanname[i] = byte2char(header[offset + i], ' ');
        }
        samples[i].name = String(cleanname);
    
        // byte swap the sample size it's stored as a big-endian number
        // double the sample size to get the correct size.
        uint16_t sample_size = 0;
        byte *bss = (byte*)&sample_size;
        bss[0] = header[offset + 23];
        bss[1] = header[offset + 22];
        samples[i].size = sample_size * 2;

        offset += 30;
    }
  }

  void Patterns() {
    length = header[950];
    int offset = 952;
    for (int i=0; i<128; i++) {
        patterns[i] = header[offset];
        if (i < length && header[offset] > highestPattern) highestPattern = header[offset];
        offset++;
    }
  }

  void Size() {
    filesize = 1084;
    for(int i=0; i<31; i++){
        filesize += samples[i].size;
    }
    filesize += ((highestPattern + 1) * 1024);
  }

  void Signature() { signature = String(byte2char(header[1080])).append(byte2char(header[1081])).append(byte2char(header[1082])).append(byte2char(header[1083])); } 

public:
  String name;
  byte header[1084];
  ModSample samples[31];
  uint8_t highestPattern = 0;
  byte patterns[128];
  byte length = 0;
  int filesize = 0;
  String signature = "";

  ModInfo() {
    memset(&header, 0, sizeof(header));
  }

  void Process() {
    Samples();
    Patterns();
    Size();
    Signature();
    name = String((char*)&header[0]);
  }

  void Print() {
    char f_modname[21];
    sprintf(f_modname, "%-20s", name.c_str());

    Log << "\r\n";
    Log << ".-----------------------------------------------------------------------------.\r\n";
    Log << "| Mod Name: " + String(f_modname) + "                                              |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n";   
    Log << "| Header Dump                                                                 |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n";
    String header_line;
    for (int i=0; i<1084; i++) {
        if (i % 64 == 0) header_line = "| ";
        header_line.append(byte2char(header[i]));
        if ((i + 1) % 64 == 0) Log << header_line.append("            |\r\n");
    }
    Log << header_line.append("                |\r\n");

    Log << "+-----------------------------------------------------------------------------+\r\n";
    Log << "| Samples:                                                                    |\r\n";
    String sample_line;
    for (int i=0; i<31; i++) {
        char f_samplename[23];
        sprintf(f_samplename, "%-22s", samples[i].name.c_str());
        char f_samplenumber[3];
        sprintf(f_samplenumber, "%02d", i + 1);
        char f_temp[7];
        char f_samplesize[7];
        sprintf(f_temp, "%d", samples[i].size);
        sprintf(f_samplesize, "%6s", f_temp);

        if (i % 2 == 0)
            sample_line = "| #" + String(f_samplenumber) + " '" + String(f_samplename) + "' " + String(f_samplesize);
        else {
            sample_line.append("     #" + String(f_samplenumber) + " '" + String(f_samplename) + "' " + String(f_samplesize) + " |\r\n");
            Log << sample_line;
        }
    }
    Log << sample_line.append("     #-- '                      '      - |\r\n");
    Log << "+-----------------------------------------------------------------------------+\r\n";

    char f_songlength[4];
    sprintf(f_songlength, "%-3d", length);
    Log << "| Song Length: " + String(f_songlength) + "                                                            |\r\n";
    
    Log << "| Song Patterns:                                                              |\r\n";   
    String pattern_line;
    for (int i=0; i<128; i++) {
        if (i % 6 == 0) pattern_line = "|  ";
        char pattern[10];
        if (i + 1 <= length)
            sprintf(pattern, "%3d: %03d", i + 1, patterns[i]);
        else
            sprintf(pattern, "%3d: ---", i + 1);
        pattern_line.append(pattern);
        if ((i + 1) % 6 == 0) {
            pattern_line.append("  |\r\n");
            Log << pattern_line;
        }
        else
            pattern_line.append("  .  ");
    }
    Log << pattern_line.append("                                                 |\r\n");

    Log << "+-----------------------------------------------------------------------------+\r\n";
    Log << "| Signature: '" + signature + "'                                                           |\r\n";

    char temp[8];
    char f_filesize[8];
    sprintf(temp, "%d", filesize);
    sprintf(f_filesize, "%6s", temp);
    Log << "| File Size: " + String(f_filesize) + "                                                           |\r\n";
    Log << "+-----------------------------------------------------------------------------+\r\n\r\n";
  }
};

#endif // XCOPYMODFILE_H