
/* 
 DESCRIPTION
 ====================
 Simple example of the Bounce library that switches the debug LED when a button is pressed.
 */
// Include the Bounce2 library found here :
// https://github.com/thomasfredericks/Bounce-Arduino-Wiring
#include <Bounce2.h>

#define BUTTON_PIN 2
#define LED_PIN 13

#define DEBOUNCE_TIME 5
#define HOLD_TIME 1000
#define RETRIGGER_TIME 100

// Instantiate a Bounce object with default values
Bounce debouncer(BUTTON_PIN);

// Or set your own values when instantiating Bounce object
// Bounce debouncer(BUTTON_PIN,DEBOUNCE_TIME, RETRIGGER_TIME, HOLD_TIME);

void setup() {

  // Setup the button with an internal pull-up :
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  //Setup the LED :
  pinMode(LED_PIN,OUTPUT);

}

void loop() {
  // Update the Bounce instance :
  debouncer.update();

  // Get the updated value :
  int value = debouncer.read();

  // Turn on or off the LED as determined by the state :
  if ( value == LOW ) {
    digitalWrite(LED_PIN, HIGH );
  } 
  else {
    digitalWrite(LED_PIN, LOW );
  }

}


