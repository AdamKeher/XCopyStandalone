#include "XCopyAudio.h"

XCopyAudio::XCopyAudio()
{
}

void XCopyAudio::begin(float gain)
{
    // 5 blocks, not 8: the graph is player -> mixer -> DAC, and the DAC holds two
    // blocks for its double buffer while one more is in transit, so three are live in
    // steady state. Five leaves a block of slack; each one costs 256 bytes of a
    // 64KB part. Running out only drops a UI chime, it does not fault.
    AudioMemory(5);
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
