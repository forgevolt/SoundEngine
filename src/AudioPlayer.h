#pragma once

#include <Arduino.h>
#include "driver/i2s_std.h"
#include "AudioClip.h"

// ---- AudioPlayer -----------------------------------------------------------------------
// Owns the I2S output channel and mixes together every AudioClip that is currently playing.
//
// Not thread-safe on its own: SoundEngine is responsible for serializing access between the
// audio-filler task (which calls prepareChunk()/writeChunk()) and any other caller (play(),
// stop(), volume changes, ...) - see SoundEngine's mutex.
//
// This library is based on the ideas of the third-party XTronical audio driver 
// (https://www.xtronical.com/i2sprerelease). 

class AudioPlayer
{
  public:
    static constexpr size_t cChunkSize = 128; // stereo frames mixed per prepareChunk() call

  public:
    AudioPlayer(int lrclkPin, int bclkPin, int doutPin);
    ~AudioPlayer();

    // Configures and enables the I2S TX channel. Deliberately separate from the constructor so
    // it can run from setup() with Serial open.
    bool begin();

    // Owns an i2s_chan_handle_t and an intrusive playlist - copying would leave two objects
    // pointing at the same OS handle/list, and the first destructor to run would invalidate
    // it for the other.
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Master volume (%), applied on top of each clip's own volume. 0 = silence, 100 = normal,
    // 200 = twice as loud. Clamped to [0, 200].
    void setVolume(int volumePercent);
    int  getVolume() const { return myVolumePercent; }

    // Starts playing 'clip', mixed together with anything else currently playing. If 'clip' is
    // already playing it is restarted from the beginning. If mix == false, every other clip is
    // stopped first.
    void play(AudioClip& clip, bool mix = true);

    bool isPlaying(const AudioClip& clip) const;
    void stop(AudioClip& clip);
    void stopAll();

    // Mixes the next chunk of audio into an internal buffer (fast, no I/O).
    void prepareChunk();
    // Blocking write of the buffer prepared by prepareChunk() to the I2S peripheral.
    void writeChunk();

  private:
    void mixSamples(int16_t& left, int16_t& right);
    void unlink(AudioClip& clip);

  private:
    const int myLRCLKPin, myBCLKPin, myDOUTPin;

    i2s_chan_handle_t myI2SHandle = nullptr;

    AudioClip* myFirstPlaying = nullptr; // head/tail of the intrusive playlist
    AudioClip* myLastPlaying  = nullptr;

    int myVolumePercent = 100;
    uint32_t myMixBuffer[cChunkSize]; // left<<16 | right, ready for i2s_channel_write()
};
