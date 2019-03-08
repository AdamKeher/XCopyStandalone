#ifndef XCOPYAUDIO_H
#define XCOPYAUDIO_H

#include <Arduino.h>
#include <SerialFlash.h>
#include "../Audio/play_serialflash_raw.h"
#include "../Audio/mixer.h"
#include "../Audio/output_dac.h"
#include <Wire.h>

class XCopyAudio
{
public:
  XCopyAudio();

  void begin();

  void playFile(const char *filename, bool wait);
  void setGain(uint8_t channel, float level);

  void playChime(bool wait);
  void playClick(bool wait);
  void playBoing(bool wait);
  void playBong(bool wait);
  void playSelect(bool wait);
  void playBack(bool wait);

private:
  AudioPlaySerialflashRaw _playWav;
  AudioMixer4 _mixer;
  AudioOutputAnalog _dac;
  AudioConnection _patchCord1 = AudioConnection(_playWav, 0, _mixer, 0);
  AudioConnection _patchCord2 = AudioConnection(_mixer, _dac);
};

#endif // XCOPYAUDIO_H