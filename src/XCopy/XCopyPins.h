#ifndef XCOPYPINS_H
#define XCOPYPINS_H

#define PIN_SCLK  13  // SCLK can also use pin 14
#define PIN_MOSI  11  // MOSI can also use pin 7

#define PIN_DC    20   //  but certain pairs must NOT be used: 2+10, 6+9, 20+23, 21+22
#define PIN_RST   21   // RST can use any pinboot 
#define PIN_TFTCS 10 // CS & DC can use pins 2, 6, 9, 10, 15, 20, 21, 22, 23

#define PIN_FLASHCS         23
#define PIN_CARDDETECT      2
#define PIN_SDCS            22
#define PIN_BUSYPIN         24
#define PIN_ESPRESETPIN     25
#define PIN_ESPPROGPIN      26
#define PIN_TEENSYRESETPIN  28

#define PIN_NAVIGATION_UP_PIN    29
#define PIN_NAVIGATION_DOWN_PIN  30
#define PIN_NAVIGATION_RIGHT_PIN 31
#define PIN_NAVIGATION_LEFT_PIN  32
#define PIN_NAVIGATION_PUSH_PIN  33

#endif // XCOPYPINS_H