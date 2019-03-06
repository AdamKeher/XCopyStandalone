#ifndef XCOPYGRAPHICS_H
#define XCOPYGRAPHICS_H

#include <Arduino.h>
#include <Streaming.h>
#include <SD.h>
#include <SerialFlash.h>
#include <TFT_ST7735.h>

#define BUFFPIXEL 20

class XCopyGraphics
{
public:
  // XCopyGraphics();
  void begin(TFT_ST7735 *tft);

  void bmpDraw(const char *filename, uint8_t x, uint16_t y);
  void bmpToRaw(const char *filename, const char *outfilename);
  void rawDraw(const char *filename, uint8_t x, uint16_t y, uint16_t width, uint16_t height);
  void drawHeader();
  void clearScreen() { _tft->fillScreen(ST7735_BLACK); }
  void drawTrack(uint8_t track, uint8_t side, bool drawText, bool retry, int retryCount, bool verify, uint16_t color);
  void drawDiskName(String name);
  void drawDisk();
  void drawText(uint8_t x, uint8_t y, uint16_t color, String text, bool clearLine = false);
  void drawText(uint16_t color, String text);
  void drawText(String text);
  void setCharSpacing(uint8_t space) { _tft->setCharSpacing(space); }
  void setTextScale(uint8_t scale) { _tft->setTextScale(scale); }
  void setTextWrap(bool wrap) { _tft->setTextWrap(wrap); }
  void setCursor(uint16_t x, uint16_t y) { _tft->setCursor(x, y); }
  uint16_t LerpRGB(uint16_t a, uint16_t b, float t);
  TFT_ST7735 *getTFT() { return _tft; }

  uint16_t read16(SerialFlashFile f);
  uint32_t read32(SerialFlashFile f);

private:
  TFT_ST7735 *_tft;
  int offset;
};

#endif // XCOPYGRAPHICS_H