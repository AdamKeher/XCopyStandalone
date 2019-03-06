#include "FivePosNavigation.h"

FivePosNavigation::FivePosNavigation(int upPin, int downPin, int leftPin, int rightPin, int pushPin)
{
    this->_upPin = upPin;
    this->_downPin = downPin;
    this->_leftPin = leftPin;
    this->_rightPin = rightPin;
    this->_pushPin = pushPin;
}

void FivePosNavigation::begin(int interval, int pinMode, ChangeCallbackFunction function)
{
    changeCallBack = function;

    this->upButton.attach(_upPin, pinMode);
    this->upButton.interval(interval);

    this->downButton.attach(_downPin, pinMode);
    this->downButton.interval(interval);

    this->leftButton.attach(_leftPin, pinMode);
    this->leftButton.interval(interval);

    this->rightButton.attach(_rightPin, pinMode);
    this->rightButton.interval(interval);

    this->pushButton.attach(_pushPin, pinMode);
    this->pushButton.interval(interval);
}

void FivePosNavigation::setCallBack(ChangeCallbackFunction function)
{
    changeCallBack = function;
}

void FivePosNavigation::setInterval(int interval)
{
    this->upButton.interval(interval);
    this->downButton.interval(interval);
    this->leftButton.interval(interval);
    this->rightButton.interval(interval);
    this->pushButton.interval(interval);
}

void FivePosNavigation::update()
{
    prev_state = state;

    this->upButton.update();
    this->downButton.update();
    this->leftButton.update();
    this->rightButton.update();
    this->pushButton.update();

    if (this->upButton.fell())
    {
        this->state.up = 1;
    }
    else if (this->upButton.rose())
    {
        this->state.up = 0;
    }
    if (this->downButton.fell())
    {
        this->state.down = 1;
    }
    else if (this->downButton.rose())
    {
        this->state.down = 0;
    }
    if (this->leftButton.fell())
    {
        this->state.left = 1;
    }
    else if (this->leftButton.rose())
    {
        this->state.left = 0;
    }
    if (this->rightButton.fell())
    {
        this->state.right = 1;
    }
    else if (this->rightButton.rose())
    {
        this->state.right = 0;
    }
    if (this->pushButton.fell())
    {
        this->state.push = 1;
    }
    else if (this->pushButton.rose())
    {
        this->state.push = 0;
    }

    uint8_t change_mask = 0;
    uint32_t duration = 0;

    if (this->state.up != prev_state.up)
    {
        change_mask |= FIVEPOSNAVIGATION_UP;
        duration = this->upButton.held();
    }
    if (this->state.down != prev_state.down)
    {
        change_mask |= FIVEPOSNAVIGATION_DOWN;
        duration = this->downButton.held();
    }
    if (this->state.left != prev_state.left)
    {
        change_mask |= FIVEPOSNAVIGATION_LEFT;
        duration = this->leftButton.held();
    }
    if (this->state.right != prev_state.right)
    {
        change_mask |= FIVEPOSNAVIGATION_RIGHT;
        duration = this->rightButton.held();
    }
    if (this->state.push != prev_state.push)
    {
        change_mask |= FIVEPOSNAVIGATION_PUSH;
        duration = this->pushButton.held();
    }

    if (this->changeCallBack && change_mask)
    {
        this->changeCallBack(change_mask, this->state, duration);
    }
}