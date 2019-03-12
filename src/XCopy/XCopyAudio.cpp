#include "XCopyAudio.h"

XCopyAudio::XCopyAudio()
{
}

void XCopyAudio::begin(float gain)
{
    AudioMemory(8);
    setGain(0, gain);
}

void XCopyAudio::playFile(const char *filename, bool wait)
{
    _playWav.play(filename);

    // FIX: hacked to always pause while playing audio to stop conflict with rawDraw / SerialFlash pushcolor
    wait = true;
    if (wait)
    {
        while (_playWav.isPlaying())
        {
        }
    }
}

void XCopyAudio::setGain(uint8_t channel, float level)
{
    _mixer.gain(channel, level);
}

void XCopyAudio::playChime(bool wait)
{
    playFile("DADA0.RAW", wait);
}

void XCopyAudio::playSelect(bool wait)
{
    playFile("THISONE.RAW", wait);
}

void XCopyAudio::playBack(bool wait)
{
    playFile("BACK.RAW", wait);
}

void XCopyAudio::playClick(bool wait)
{
    playFile("KLICK.RAW", wait);
}

void XCopyAudio::playBoing(bool wait)
{
    playFile("BOING.RAW", wait);
}

void XCopyAudio::playBong(bool wait)
{
    playFile("BONG.RAW", wait);
}
