#include "XCopyFloppy.h"

void XCopyFloppy::setupDrive() {
    pinMode(_density, OUTPUT);
    digitalWriteFast(_density, HIGH);
    pinMode(_index, INPUT_PULLUP);
    pinMode(_motor, OUTPUT);
    digitalWriteFast(_motor, HIGH);
    pinMode(_driveSelect, OUTPUT);
    digitalWriteFast(_driveSelect, HIGH);
    pinMode(_direcion, OUTPUT);
    digitalWriteFast(_direcion, HIGH);
    pinMode(_step, OUTPUT);
    digitalWriteFast(_step, HIGH);
    pinModeFast(_writeData, OUTPUT);
    digitalWriteFast(_writeData, HIGH);
    pinMode(_writeEnable, OUTPUT);
    digitalWriteFast(_writeEnable, HIGH);
    pinMode(_track0, INPUT_PULLUP);
    pinMode(_writeProtect, INPUT_PULLUP);
    pinMode(_side, OUTPUT);
    digitalWriteFast(_side, HIGH);
    pinMode(_diskChange, INPUT_PULLUP);

    _position.direction = floppy_backward;
    _position.track = 0;
    _position.side = floppy_top;
}

void XCopyFloppy::motor(bool enable) {
    _motorStatus = enable;
    if (enable) {
        driveSelect(true);
        digitalWriteFast(_motor, LOW);
        delay(_delays.driveSpinUpDelayMs);
    } else {
        digitalWriteFast(_motor, HIGH);
        delayMicroseconds(_delays.driveSpinDownDelayUs);
        driveSelect(false);
    }
}

void XCopyFloppy::driveSelect(bool selected) {
    digitalWriteFast(_driveSelect, selected ? LOW : HIGH);
    delayMicroseconds(selected ? _delays.driveSelectDelayUs : _delays.driveDeselectDelayUs);
}

void XCopyFloppy::step() {
    digitalWriteFast(_step, LOW);
    delayMicroseconds(_delays.stepDurationUs);
    digitalWriteFast(_step, HIGH);

    delay(_delays.stepDelayMs);

    _position.direction == floppy_backward ? _position.track-- : _position.track++;
    printPosition();
}

bool XCopyFloppy::home() {
    motor(true);
    setDirection(floppy_backward);
    
    int stepCount = 0;
    while (digitalRead(_track0) == 1)
    {
        step();
        stepCount++;
        if (stepCount > 85) return false;
    }
    _position.track = 0;
    printPosition();
    return true;
}

bool XCopyFloppy::gotoTrack(int trackNumber) {
    if (_position.track == trackNumber) return false;
    if (trackNumber < 0 && trackNumber > 80) return false;
    if (trackNumber == 0) {
        home();
        return true;
    }

    motor(true);

    setDirection(trackNumber > _position.track ? floppy_forward : floppy_backward);
    size_t stepCount = trackNumber > _position.track 
        ? trackNumber - _position.track 
        : _position.track - trackNumber;

    for (size_t i = 0; i < stepCount; i++)
    {
        step();
    }

    return true;
}

void XCopyFloppy::setDirection(XCopyFloppyDirection direction) {
    _position.direction = direction;
    direction == floppy_backward ? digitalWriteFast(_direcion, HIGH) : digitalWriteFast(_direcion, LOW);
    delay(_delays.directionDelayMs);
}

void XCopyFloppy::setSide(XCopyFloppySide side) {
    _position.side = side;
    digitalWriteFast(_side, side == floppy_top ? HIGH : LOW);
    delay(_delays.selectSideDelayMs);
}

bool XCopyFloppy::getWriteProtect() {
    if (_motorStatus == false) motor(true);
    return !digitalRead(_writeProtect);
}

bool XCopyFloppy::getDiskChange() {
    driveSelect(true);
    delay(50);
    bool result = !digitalRead(_diskChange);
    if (!result) {
        setDirection(floppy_forward);
        step();
        result = !digitalRead(_diskChange);
    }
    return result;
}

void XCopyFloppy::initRead() {
    // bCnt = 0;
    // readPtr = 0;
    // bitCount = 0;
    // sectorCnt = 0;
    // errors = 0;

    for (int i = 0; i < streamLen; i++)
    {
        stream[i] = 0x00;
    }
    // setupFTM0();
}

void XCopyFloppy::readTrack() {
        for (int i = 0; i < 256; i++)
        {
            hist[i] = 0;
        }

        motor(true);
        stream = (byte *)malloc(streamSizeDD + 10);
}

void XCopyFloppy::pinModeFast(uint8_t pin, uint8_t mode)
{
    volatile uint32_t *config;

    if (pin >= CORE_NUM_DIGITAL)
        return;
    config = portConfigRegister(pin);

    if (mode == OUTPUT || mode == OUTPUT_OPENDRAIN)
    {
#ifdef KINETISK
        *portModeRegister(pin) = 1;
#else
        *portModeRegister(pin) |= digitalPinToBitMask(pin); // TODO: atomic
#endif
        *config = PORT_PCR_DSE | PORT_PCR_MUX(1);
        if (mode == OUTPUT_OPENDRAIN)
        {
            *config |= PORT_PCR_ODE;
        }
        else
        {
            *config &= ~PORT_PCR_ODE;
        }
    }
    else
    {
#ifdef KINETISK
        *portModeRegister(pin) = 0;
#else
        *portModeRegister(pin) &= ~digitalPinToBitMask(pin);
#endif
        if (mode == INPUT || mode == INPUT_PULLUP || mode == INPUT_PULLDOWN)
        {
            *config = PORT_PCR_MUX(1);
            if (mode == INPUT_PULLUP)
            {
                *config |= (PORT_PCR_PE | PORT_PCR_PS); // pullup
            }
            else if (mode == INPUT_PULLDOWN)
            {
                *config |= (PORT_PCR_PE); // pulldown
                *config &= ~(PORT_PCR_PS);
            }
        }
        else
        {
            *config = PORT_PCR_MUX(1) | PORT_PCR_PE | PORT_PCR_PS; // pullup
        }
    }
}


bool XCopyFloppy::detectCableOrientation() {
    bool step = digitalRead(_step);
    bool select = digitalRead(_driveSelect);
    bool write = digitalRead(_writeData);

    return (step & select & write);
}