#include "src/XCopy/XCopy.h"
#include "src/FivePosNavigation/FivePosNavigation.h"

#ifndef __MK20DX256__
#error Not Teensy 3.2 MCU, make sure you get a version 3.2, because the 3.1 ist not 5V tolerant
#endif

#if F_CPU != 96000000L
#error Please compile Teensy set to 96MHz optimized
#endif

// remember to compile with smallest code or you will run out of memory

#define SCLK  13  // SCLK can also use pin 14
#define MOSI  11  // MOSI can also use pin 7

#define DC    20   //  but certain pairs must NOT be used: 2+10, 6+9, 20+23, 21+22
#define RST   21   // RST can use any pinboot 
#define TFTCS 10 // CS & DC can use pins 2, 6, 9, 10, 15, 20, 21, 22, 23

#define FLASHCS     23
#define CARDDETECT  2
#define SDCS        22

#define NAVIGATION_UP_PIN    29
#define NAVIGATION_DOWN_PIN  30
#define NAVIGATION_RIGHT_PIN 31
#define NAVIGATION_LEFT_PIN  32
#define NAVIGATION_PUSH_PIN  33

//Adafruit_ST7735 tft = Adafruit_ST7735(TFTCS, DC, MOSI, SCLK, RST);
//Adafruit_ST7735 tft = Adafruit_ST7735(TFTCS, DC, RST);
TFT_ST7735 tft = TFT_ST7735(TFTCS, DC, RST);
FivePosNavigation navigation = FivePosNavigation(NAVIGATION_UP_PIN, NAVIGATION_DOWN_PIN, NAVIGATION_LEFT_PIN, NAVIGATION_RIGHT_PIN, NAVIGATION_PUSH_PIN);
XCopy xcopy = XCopy(&tft);

void navigationCallBack(uint8_t change_mask, FivePosNavigationState state, uint32_t duration) {
  if ((change_mask & FIVEPOSNAVIGATION_DOWN) && state.down)
  { 
    xcopy.navigateDown();
  }
  if ((change_mask & FIVEPOSNAVIGATION_UP) && state.up)
  { 
    xcopy.navigateUp();
  }
  if ((change_mask & FIVEPOSNAVIGATION_PUSH) && state.push)
  { 
    xcopy.navigateSelect();
  }
  if ((change_mask & FIVEPOSNAVIGATION_LEFT) && state.left)
  { 
    xcopy.navigateLeft();
  }
  if ((change_mask & FIVEPOSNAVIGATION_RIGHT) && state.right)
  { 
    xcopy.navigateRight();
  }
}

unsigned long lastCancel = 0;
unsigned long current = 0;
void ISR_CANCEL()
{  
  // NOTE: Dubious software debounce
  current = millis();
  if (current - lastCancel > 150)
  {
    lastCancel = current;  
    xcopy.cancelOperation();
  }
}
   
void setup() {
  Serial.begin(115200);

  navigation.begin(10, INPUT_PULLUP, navigationCallBack);
  xcopy.begin(SDCS, FLASHCS, CARDDETECT);
  attachInterrupt(NAVIGATION_LEFT_PIN, ISR_CANCEL, FALLING);
}

void loop() {
  navigation.update();
  xcopy.update();
}
