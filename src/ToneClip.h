#pragma once

#include "AudioClip.h"

// ---- ToneClip --------------------------------------------------------------------------
// A single generated note: frequency, duration, waveform. No WAV data, no flash cost.
//
// It is an ordinary AudioClip, so it mixes with recorded clips, repeats, and takes a per-clip
// volume like any other. Arm and play it through the engine:
//
//   ToneClip tone;
//   sound.playTone(tone, 440.0f, 200);          // A4 for 200 ms
//   sound.playTone(tone, cNoteC5, 120, Waveform::eSquare);
//
// Re-arming a clip the mixer may be reading is not safe on its own - SoundEngine::playTone()
// does it under the engine's mutex, which is why it exists.

enum class Waveform : uint8_t
{
  eSine,      // clean, good for indications
  eSquare,    // loud and hollow, the classic beeper sound
  eTriangle,  // softer than square, still bright
  eSawtooth   // buzzy, the harshest of the four
};

// A few note frequencies, enough to write a simple tune. Equal temperament, A4 = 440 Hz.
constexpr float cNoteRest = 0.0f;   // a rest: the duration passes in silence

constexpr float cNoteC4 = 261.63f, cNoteCs4 = 277.18f, cNoteD4 = 293.66f, cNoteDs4 = 311.13f;
constexpr float cNoteE4 = 329.63f, cNoteF4  = 349.23f, cNoteFs4 = 369.99f, cNoteG4 = 392.00f;
constexpr float cNoteGs4 = 415.30f, cNoteA4 = 440.00f, cNoteAs4 = 466.16f, cNoteB4 = 493.88f;

constexpr float cNoteC5 = 523.25f, cNoteCs5 = 554.37f, cNoteD5 = 587.33f, cNoteDs5 = 622.25f;
constexpr float cNoteE5 = 659.26f, cNoteF5  = 698.46f, cNoteFs5 = 739.99f, cNoteG5 = 783.99f;
constexpr float cNoteGs5 = 830.61f, cNoteA5 = 880.00f, cNoteAs5 = 932.33f, cNoteB5 = 987.77f;

constexpr float cNoteC6 = 1046.50f;


class ToneClip : public AudioClip
{
  public:
    ToneClip() = default;
    ToneClip(float frequencyHz, uint32_t durationMs, Waveform wave = Waveform::eSine);

    // Arms the clip. Takes effect at the next start(); use SoundEngine::playTone() rather than
    // calling this directly on a clip that may be playing.
    void setTone(float frequencyHz, uint32_t durationMs, Waveform wave = Waveform::eSine);

    float    getFrequency() const { return myFrequencyHz; }
    uint32_t getDuration()  const { return myDurationMs; }
    Waveform getWaveform()  const { return myWaveform; }

  protected:
    void start() override;
    void nextSample(int16_t& left, int16_t& right) override;

    // ---- for MelodyClip, which plays a run of notes through one oscillator ---------------

    // Arms the oscillator for one note and restarts its envelope.
    void beginNote(float frequencyHz, uint32_t durationMs, Waveform wave);

    // Next sample of the current note. Returns false once the note has run its length, with
    // sample set to 0.
    bool nextNoteSample(int16_t& sample);

    // Amplitude a tone is generated at, out of 32767. Deliberately half scale: a note is
    // usually one of several things sounding at once, and the mixer clamps the total.
    static constexpr int32_t cToneAmplitude = 16384;

    // Fade applied at both ends of every note. A waveform that starts or stops away from zero
    // steps the speaker cone, which is audible as a click - most obviously on square and
    // sawtooth. Also keeps consecutive notes from running into each other.
    static constexpr uint32_t cFadeMs = 5;

  private:
    float    myFrequencyHz = 0.0f;
    uint32_t myDurationMs  = 0;
    Waveform myWaveform    = Waveform::eSine;

    // Phase accumulator, full 32-bit range = one cycle. Wraps by itself, so no conditional and
    // no drift.
    uint32_t myPhase      = 0;
    uint32_t myPhaseStep  = 0;

    uint32_t mySamplesLeft  = 0;  // of the current note
    uint32_t myFadeSamples  = 0;  // length of each ramp, in samples
    uint32_t myNoteSamples  = 0;  // total length of the current note
};
