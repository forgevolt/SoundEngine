#pragma once

#include <Arduino.h>
#include <cstddef>   // std::size_t

// Fixed output sample rate used by AudioPlayer. Declared here (rather than in AudioPlayer.h)
// so AudioClip can compute its resampling step without a circular include.
constexpr uint32_t cAudioSampleRate = 44100;

// ---- AudioClip -------------------------------------------------------------------------
// A single sound effect: 16-bit PCM WAV data (mono or stereo, any sample rate) embedded as a
// byte array in flash, e.g. via #include "sounds/Click.h". 
//
// An AudioClip does not copy the WAV bytes; 'wavData' must stay valid for the clip's lifetime.
//
// This library is based on the ideas of the third-party XTronical audio driver 
// (https://www.xtronical.com/i2sprerelease). 

class AudioClip
{
  friend class AudioPlayer; // manages the intrusive playlist links and calls start()/nextSample()

  public:
    virtual ~AudioClip() = default;

    // Preferred form: deduces the buffer length from the array, so the RIFF walker can
    // bounds-check against the real size instead of trusting the length fields stored
    // inside the file. Every sounds/*.h clip is a plain array, so this is what they use.
    template <std::size_t N>
    explicit AudioClip(const uint8_t (&wavData)[N]) : AudioClip(wavData, N) {}

    // Explicit-size form, for a clip only available as a pointer.
    AudioClip(const uint8_t* wavData, std::size_t wavSize);

    // Vol as %: 0 = silence, 100 = original volume, 200 = twice as loud. Clamped to [0, 200].
    void setVolume(int volumePercent);
    int  getVolume() const { return myVolumePercent; }

    void setRepeatForever(bool repeat) { myRepeatForever = repeat; }
    bool getRepeatForever() const      { return myRepeatForever;  }

  protected:
    // Where a clip's samples come from. Overriding these two is how a clip that is not a WAV -
    // a generated tone, a melody - joins in: the mixer walks every clip through the same pair,
    // so anything that implements them mixes, loops and takes a volume like any other.
    //
    // Rewinds playback to the start of the clip. Called by AudioPlayer::play().
    virtual void start();

    // Writes the next output-rate stereo sample pair into left/right (not yet volume-scaled -
    // AudioPlayer applies both the clip's and the master volume while mixing). Clears
    // myIsPlaying once the clip's data is exhausted.
    virtual void nextSample(int16_t& left, int16_t& right);

    // For a subclass that generates its samples and so has no WAV data to run out of.
    void setPlaying(bool playing) { myIsPlaying = playing; }

    // Parses WAV data and points the clip at it, for a subclass that acquires its bytes after
    // construction - loading them from a file, say. The clip does not take ownership: whoever
    // supplied the buffer must keep it alive, and must not free it while the clip is playing.
    // A buffer that does not parse leaves the clip silent.
    void setWavData(const uint8_t* wavData, std::size_t wavSize);

  protected:
    // Only a derived clip constructs without WAV data.
    AudioClip() = default;

  private:
    // Playback cursor format: fixed point with cFracBits fractional bits.
    static constexpr uint32_t cFracBits = 16;
    static constexpr uint32_t cFracOne  = 1u << cFracBits;   // == 1.0

    const uint8_t* myData        = nullptr; // PCM sample data (past the WAV header), not owned
    uint32_t       myDataSize    = 0;       // size of myData in bytes
    uint16_t       myNumChannels = 0;       // 1 = mono, 2 = stereo; 0 = failed to parse -> silent

    int  myVolumePercent = 100;
    bool myRepeatForever = false;
    bool myIsPlaying     = false;

    // Playback position, in source frames (one sample per channel). Fractional: advanced by
    // mySourceStep per output sample so a clip recorded at e.g. 22050Hz plays back at the
    // right pitch/speed through the fixed cAudioSampleRate output stream. Resampling is
    // nearest-neighbour (no interpolation) - adequate for short UI sound effects.
    uint64_t myReadPos    = 0;         // Q48.16
    uint32_t mySourceStep = cFracOne;  // Q16.16

    // Intrusive doubly-linked playlist, owned and maintained by AudioPlayer.
    AudioClip* myPrev = nullptr;
    AudioClip* myNext = nullptr;
};
