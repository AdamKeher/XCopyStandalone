#ifndef FIVEPOSNAVIGATION_H
#define FIVEPOSNAVIGATION_H

#include <Arduino.h>
#include <Bounce2.h>

#define FIVEPOSNAVIGATION_UP 0b00000001
#define FIVEPOSNAVIGATION_DOWN 0b00000010
#define FIVEPOSNAVIGATION_LEFT 0b00000100
#define FIVEPOSNAVIGATION_RIGHT 0b00001000
#define FIVEPOSNAVIGATION_PUSH 0b00010000

struct FivePosNavigationState
{
  uint8_t up : 1;
  uint8_t down : 1;
  uint8_t left : 1;
  uint8_t right : 1;
  uint8_t push : 1;
};

/*
   @param repeat true when the direction is being held rather than newly pressed.

   A consumer that treats a repeat as a press gets a free key repeat; one that
   would rather not - anything where the control toggles a setting instead of
   moving a cursor - checks the flag and drops it.
*/
typedef void (*ChangeCallbackFunction)(uint8_t change_mask, FivePosNavigationState state, uint32_t duration, bool repeat);

class FivePosNavigation
{
  public:
    FivePosNavigation(int upPin, int downPin, int leftPin, int rightPin, int pushPin);

    void begin(int interval, int pinMode, ChangeCallbackFunction function);
    void setCallBack(ChangeCallbackFunction function);
    void setInterval(int interval);

    /**
     * @brief Auto repeat for up and down, the two that only ever move a cursor.
     *
     * Held for @p delayMs, then a fresh callback every @p rateMs, so a listing
     * longer than the screen can be walked without working the stick. Not offered
     * for left, right or push: those select, back out and act, and a control that
     * fires an action over and over because a thumb rested on it is a different
     * feature to a cursor that keeps moving.
     *
     * @param rateMs 0 turns repeating off.
     */
    void setRepeat(uint16_t delayMs, uint16_t rateMs);

    void update();

    FivePosNavigationState state;

  private:
    Bounce upButton = Bounce(_upPin);
    Bounce downButton = Bounce(_downPin);
    Bounce leftButton = Bounce(_leftPin);
    Bounce rightButton = Bounce(_rightPin);
    Bounce pushButton = Bounce(_pushPin);

    FivePosNavigationState prev_state;
    ChangeCallbackFunction changeCallBack;

    int _upPin;
    int _downPin;
    int _leftPin;
    int _rightPin;
    int _pushPin;

    // Which direction is repeating, as a FIVEPOSNAVIGATION_ bit, or 0 for none.
    uint8_t _repeatMask = 0;
    uint32_t _repeatNextMs = 0;
    uint16_t _repeatDelayMs = 0;
    uint16_t _repeatRateMs = 0;
};

#endif // FIVEPOSNAVIGATION_H