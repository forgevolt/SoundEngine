#include "MelodyClip.h"

// ----------------------------------------------------------------------------------------
MelodyClip::MelodyClip(const Note* notes, std::size_t count, Waveform wave)
{
  setMelody(notes, count, wave);
}

// ----------------------------------------------------------------------------------------
void MelodyClip::setMelody(const Note* notes, std::size_t count, Waveform wave)
{
  myNotes    = notes;
  myCount    = (notes != nullptr) ? count : 0;
  myWaveform = wave;
  myIndex    = 0;
}

// ----------------------------------------------------------------------------------------
bool MelodyClip::beginCurrentNote()
{
  if (myIndex >= myCount)
    return false;

  beginNote(myNotes[myIndex].frequencyHz, myNotes[myIndex].durationMs, myWaveform);
  return true;
}

// ----------------------------------------------------------------------------------------
void MelodyClip::start()
{
  myIndex = 0;
  setPlaying(beginCurrentNote());
}

// ----------------------------------------------------------------------------------------
void MelodyClip::nextSample(int16_t& left, int16_t& right)
{
  int16_t sample = 0;

  // A note of zero length yields no samples at all, so stepping on is a loop rather than a
  // single advance - otherwise one such note would silence the rest of the melody.
  while (nextNoteSample(sample) == false)
  {
    myIndex++;

    if (beginCurrentNote() == false)
    {
      setPlaying(false);   // melody finished; AudioPlayer unlinks it, or restarts it if repeating
      left = right = 0;
      return;
    }
  }

  left = right = sample;
}
