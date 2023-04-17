/*
 * Partial Mode example. Partial mode restrict screen to a specific area
 */
#include <SPI.h>
#include <TFT_ST7735.h>


#define __CS1 	10
#define __DC 	9


TFT_ST7735 tft = TFT_ST7735(__CS1, __DC);

void testLines(uint16_t color) {
	int16_t i;
	tft.clearScreen();
	for (i = 0; i < tft.width(); i += 6) tft.drawLine(0, 0, i, tft.height() - 1, color);
	for (i = 0; i < tft.height(); i += 6) tft.drawLine(0, 0, tft.width() - 1, i, color);
	tft.clearScreen();
	for (i = 0; i < tft.width(); i += 6) tft.drawLine(tft.width() - 1, 0, i, tft.height() - 1, color);
	for (i = 0; i < tft.height(); i += 6) tft.drawLine(tft.width() - 1, 0, 0, i, color);
	tft.clearScreen();
	for (i = 0; i < tft.width(); i += 6) tft.drawLine(0, tft.height() - 1, i, 0, color);
	for (i = 0; i < tft.height(); i += 6) tft.drawLine(0, tft.height() - 1, tft.width() - 1, i, color);
	tft.clearScreen();
	for (i = 0; i < tft.width(); i += 6) tft.drawLine(tft.width() - 1, tft.height() - 1, i, 0, color);
	for (i = 0; i < tft.height(); i += 6) tft.drawLine(tft.width() - 1, tft.height() - 1, 0, i, color);
}

void setup() {
/*
  Serial.begin(38400);
  start = millis ();
  while (!Serial && ((millis () - start) <= 5000)) ;
*/
  tft.begin();
}

void loop() {
	testLines(YELLOW);
	delay(1000);
	tft.changeMode(PARTIAL);//enable partial mode
	tft.setPartialArea(50, 80); //set area
	testLines(GREEN);
	delay(1000);
	tft.changeMode(NORMAL);//enable normal mode
	testLines(GREEN);
	delay(1000);
}

