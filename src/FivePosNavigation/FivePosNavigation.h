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

typedef void (*ChangeCallbackFunction)(uint8_t change_mask, FivePosNavigationState state, uint32_t duration);

class FivePosNavigation
{
  public:
    FivePosNavigation(int upPin, int downPin, int leftPin, int rightPin, int pushPin);

    void begin(int interval, int pinMode, ChangeCallbackFunction function);
    void setCallBack(ChangeCallbackFunction function);
    void setInterval(int interval);
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
};

#endif // FIVEPOSNAVIGATION_H