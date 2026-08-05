#pragma once

#include "ToneClip.h"
#include <cstddef>

// ---- MelodyClip ------------------------------------------------------------------------
// A run of notes played one after another, from a plain array:
//
//   const Note cStartup[] = { { cNoteC5, 90 }, { cNoteE5, 90 }, { cNoteG5, 180 } };
//   MelodyClip startup;
//   sound.playMelody(startup, cStartup, 3);
//
// It is an ordinary AudioClip, so a melody mixes with everything else, takes a per-clip volume,
// and repeats if played with repeat = true. It finishes on its own after the last note.
//
// The array is not copied: it has to outlive the playing, which file scope guarantees.

struct Note
{
  float    frequencyHz;   // cNoteRest (0) for a rest
  uint16_t durationMs;
};


class MelodyClip : public ToneClip
{
  public:
    MelodyClip() = default;
    MelodyClip(const Note* notes, std::size_t count, Waveform wave = Waveform::eSine);

    // Arms the clip. Takes effect at the next start(); use SoundEngine::playMelody() rather
    // than calling this directly on a clip that may be playing.
    void setMelody(const Note* notes, std::size_t count, Waveform wave = Waveform::eSine);

    std::size_t getNoteCount() const { return myCount; }

  protected:
    void start() override;
    void nextSample(int16_t& left, int16_t& right) override;

  private:
    // Arms the oscillator for myNotes[myIndex]. Returns false once the melody is spent.
    bool beginCurrentNote();

    const Note* myNotes    = nullptr;
    std::size_t myCount    = 0;
    std::size_t myIndex    = 0;
    Waveform    myWaveform = Waveform::eSine;
};
