#include "XCopy/XCopy.h"
#include "XCopy/XCopyPins.h"
#include "FivePosNavigation/FivePosNavigation.h"

#ifndef __MK20DX256__
#error Not Teensy 3.2 MCU, make sure you get a version 3.2, because the 3.1 ist not 5V tolerant
#endif

// #if F_CPU != 96000000L
// #error Please compile Teensy set to 96MHz optimized
// #endif

// remember to compile with smallest code or you will run out of memory

TFT_ST7735 tft = TFT_ST7735(PIN_TFTCS, PIN_DC, PIN_RST);
FivePosNavigation navigation = FivePosNavigation(PIN_NAVIGATION_UP_PIN, PIN_NAVIGATION_DOWN_PIN, PIN_NAVIGATION_LEFT_PIN, PIN_NAVIGATION_RIGHT_PIN, PIN_NAVIGATION_PUSH_PIN);
XCopy xcopy = XCopy(&tft);
XCopyLog Log = XCopyLog();

void navigationCallBack(uint8_t change_mask, FivePosNavigationState state, uint32_t duration) {
  if ((change_mask & FIVEPOSNAVIGATION_DOWN) && state.down)
  { 
    xcopy.navigateDown();
    // Serial.println("Down");
  }
  if ((change_mask & FIVEPOSNAVIGATION_UP) && state.up)
  { 
    // Serial.println("Up");
    xcopy.navigateUp();
  }
  if ((change_mask & FIVEPOSNAVIGATION_PUSH) && state.push)
  { 
    // Serial.println("Push");
    xcopy.navigateSelect();
  }
  if ((change_mask & FIVEPOSNAVIGATION_LEFT) && state.left)
  { 
    xcopy.navigateLeft();
    // Serial.println("Left");
  }
  if ((change_mask & FIVEPOSNAVIGATION_RIGHT) && state.right)
  { 
    xcopy.navigateRight();
    // Serial.println("Right");
  }
}

unsigned long lastCancel = 0;
unsigned long current = 0;
void ISR_CANCEL()
{  
  // shitty debounce
  current = millis();
  if (current - lastCancel > 150)
  {
    lastCancel = current;  
    xcopy.cancelOperation();
  }
}
   
void setup() {
  // Init Reset Pin
  // -------------------------------------------------------------------------------------------
  pinMode(PIN_TEENSYRESETPIN, INPUT_DISABLE);

  Serial.begin(115200);

  navigation.begin(10, INPUT_PULLUP, navigationCallBack);
  xcopy.begin();
  #if PCBVERSION == 1
  attachInterrupt(PIN_NAVIGATION_LEFT_PIN, ISR_CANCEL, FALLING);
  #else
  attachInterrupt(PIN_NAVIGATION_LEFT_PIN, ISR_CANCEL, FALLING);
  attachInterrupt(PIN_NAVIGATION_UP_PIN, ISR_CANCEL, FALLING);
  #endif
}

void loop() {
  navigation.update();
  xcopy.update();
}
