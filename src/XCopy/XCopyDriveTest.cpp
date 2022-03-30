#include "XCopyDriveTest.h"

XCopyDriveTest::XCopyDriveTest() { }

volatile bool _diskChangeValue = false;
volatile bool _redrawdiskChange = false;
void diskChangeIRQ2()
{
    _diskChangeValue = !_diskChangeValue;
    _redrawdiskChange = true;
}

volatile bool _indexValue = false;
volatile bool _redrawIndex = false;
void diskIndexIRQ2()
{
    _indexValue = !_indexValue;
    _redrawIndex = true;
}

volatile bool _driveSelectValue = false;
volatile bool _redrawDriveSelect = false;
void driveSelectIRQ2()
{
    _driveSelectValue = !_driveSelectValue;
    _redrawDriveSelect = true;
}

volatile bool _motorValue = false;
volatile bool _redrawMotor = false;
void motorIRQ2()
{
    _motorValue = !_motorValue;
    _redrawMotor = true;
}

void XCopyDriveTest::begin(XCopyGraphics *graphics, XCopyAudio *audio, XCopyESP8266 *esp) {
    _graphics = graphics;
    _audio = audio;
    _esp = esp;
    _floppy = new XCopyFloppy();
    _floppy->setupDrive();
    _floppy->motor(true);
    _diskChangeValue = _floppy->getDiskChange();

    // setup ISR
    attachInterrupt(_diskChange, diskChangeIRQ2, CHANGE);    
    attachInterrupt(_driveSelect, driveSelectIRQ2, CHANGE);    
    attachInterrupt(_motor, motorIRQ2, CHANGE);    
    // attachInterrupt(_index, diskIndexIRQ2, CHANGE);    
}

void XCopyDriveTest::draw() {
    String motorText = "Motor: ";
    motorText.concat(_floppy->getMotorStatus() ? "On" : "Off");

    _graphics->getTFT()->fillRect(0, 0, 20, 10, _driveSelectValue ? ST7735_RED : ST7735_GREEN);
    _graphics->getTFT()->fillRect(25, 0, 20, 10, _motorValue ? ST7735_RED : ST7735_GREEN);
    _graphics->getTFT()->fillRect(50, 0, 20, 10, _indexValue ? ST7735_RED : ST7735_GREEN);
    _graphics->getTFT()->fillRect(75, 0, 20, 10, _diskChangeValue ? ST7735_RED : ST7735_GREEN);
    _graphics->getTFT()->fillRect(100, 0, 20, 10, ST7735_WHITE);
    _graphics->getTFT()->fillRect(125, 0, 20, 10, ST7735_WHITE);
    _graphics->drawText(0, 12, ST7735_WHITE, "SEL", false);
    _graphics->drawText(25, 12, ST7735_WHITE, "MOT", false);
    _graphics->drawText(50, 12, ST7735_WHITE, "IDX", false);
    _graphics->drawText(75, 12, ST7735_WHITE, "CHG", false);
    _graphics->drawText(100, 12, ST7735_WHITE, "WP", false);
    _graphics->drawText(125, 12, ST7735_WHITE, "SI", false);

    _graphics->drawText(0, 30, ST7735_WHITE, "Drive Test", true);
    _graphics->drawText(0, 40, ST7735_WHITE, motorText, true);
    _graphics->drawText(0, 50, ST7735_WHITE, "Home", true);
    _graphics->drawText(0, 60, ST7735_WHITE, "Step Track", true);    
}

void XCopyDriveTest::update() {
    if (_redrawDriveSelect == true) {
        _graphics->getTFT()->fillRect(0, 0, 20, 10, _driveSelectValue ? ST7735_RED : ST7735_GREEN);
        _redrawDriveSelect = false;
    }
    if (_redrawMotor == true) {
        _graphics->getTFT()->fillRect(25, 0, 20, 10, _motorValue ? ST7735_RED : ST7735_GREEN);
        _redrawMotor = false;
    }
    if (_redrawIndex == true) {
        _graphics->getTFT()->fillRect(50, 0, 20, 10, _indexValue ? ST7735_RED : ST7735_GREEN);
        _redrawIndex = false;
    }
    if (_redrawdiskChange == true) {
        _graphics->getTFT()->fillRect(75, 0, 20, 10, _diskChangeValue ? ST7735_RED : ST7735_GREEN);
        _redrawdiskChange = false;
    }
}

void XCopyDriveTest::cancelOperation() {
    _cancelOperation = true;
}

void XCopyDriveTest::operationCancelled(uint8_t trackNum) {
    _graphics->drawText(0, 10, ST7735_RED, "Operation Cancelled", true);

    // TODO: remove interrupt & delete _floppy

    _audio->playBong(false);
}