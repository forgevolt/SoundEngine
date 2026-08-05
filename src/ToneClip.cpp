#include "ToneClip.h"

#include <cmath>

namespace
{
  // One cycle of a sine, 256 points. Read with the top 8 bits of the phase accumulator - a
  // table is both cheaper and more predictable than calling sinf() 44100 times a second, and
  // at this size the error is far below what a small speaker resolves.
  const int16_t cSineTable[256] =
  {
       0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
    6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
   12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
   18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
   23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
   27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
   30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
   32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
   32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
   32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
   30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
   27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
   23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
   18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
   12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
    6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
       0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
   -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
  -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
  -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
  -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
  -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
  -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
  -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
  -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
  -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
  -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
  -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
  -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
  -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
   -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,  };

  // Turns the phase accumulator into a sample, full scale, before the envelope.
  int32_t waveformSample(uint32_t phase, Waveform wave)
  {
    switch (wave)
    {
      case Waveform::eSine:
        return cSineTable[phase >> 24];

      case Waveform::eSquare:
        // Top bit is the half-cycle.
        return (phase & 0x80000000u) ? -32767 : 32767;

      case Waveform::eTriangle:
      {
        // Ramp up over the first half, down over the second.
        const int32_t ramp = int32_t(phase >> 16);          // 0 .. 65535
        return (ramp < 32768) ? (ramp * 2 - 32768) : (98303 - ramp * 2);
      }

      case Waveform::eSawtooth:
        // Straight ramp across the whole cycle.
        return int32_t(phase >> 16) - 32768;
    }

    return 0;
  }
}

// ----------------------------------------------------------------------------------------
ToneClip::ToneClip(float frequencyHz, uint32_t durationMs, Waveform wave)
{
  setTone(frequencyHz, durationMs, wave);
}

// ----------------------------------------------------------------------------------------
void ToneClip::setTone(float frequencyHz, uint32_t durationMs, Waveform wave)
{
  myFrequencyHz = (frequencyHz > 0.0f) ? frequencyHz : 0.0f;  // anything else is a rest
  myDurationMs  = durationMs;
  myWaveform    = wave;
}

// ----------------------------------------------------------------------------------------
void ToneClip::beginNote(float frequencyHz, uint32_t durationMs, Waveform wave)
{
  myWaveform = wave;
  myPhase    = 0;

  // Whole cycle = 2^32, so the step is frequency / sampleRate scaled by that. Wrapping the
  // accumulator is the modulo, which is why the phase never drifts.
  myPhaseStep = (frequencyHz > 0.0f)
              ? uint32_t((double(frequencyHz) / double(cAudioSampleRate)) * 4294967296.0)
              : 0;   // a rest still runs its duration, silently

  myNoteSamples = uint32_t((uint64_t(durationMs) * cAudioSampleRate) / 1000);
  mySamplesLeft = myNoteSamples;

  // Both ramps have to fit inside the note, however short it is asked to be.
  myFadeSamples = (cFadeMs * cAudioSampleRate) / 1000;

  if (myFadeSamples * 2 > myNoteSamples)
    myFadeSamples = myNoteSamples / 2;
}

// ----------------------------------------------------------------------------------------
bool ToneClip::nextNoteSample(int16_t& sample)
{
  if (mySamplesLeft == 0)
  {
    sample = 0;
    return false;
  }

  const uint32_t elapsed = myNoteSamples - mySamplesLeft;

  int32_t value = (myPhaseStep != 0) ? waveformSample(myPhase, myWaveform) : 0;

  value = (value * cToneAmplitude) / 32767;

  // Linear ramp in at the start and out at the end.
  if (myFadeSamples > 0)
  {
    if (elapsed < myFadeSamples)
      value = int32_t((int64_t(value) * elapsed) / myFadeSamples);
    else if (mySamplesLeft <= myFadeSamples)
      value = int32_t((int64_t(value) * (mySamplesLeft - 1)) / myFadeSamples);
  }

  myPhase += myPhaseStep;
  mySamplesLeft--;

  sample = int16_t(value);
  return true;
}

// ----------------------------------------------------------------------------------------
void ToneClip::start()
{
  beginNote(myFrequencyHz, myDurationMs, myWaveform);
  setPlaying(myNoteSamples > 0);
}

// ----------------------------------------------------------------------------------------
void ToneClip::nextSample(int16_t& left, int16_t& right)
{
  int16_t sample = 0;

  if (nextNoteSample(sample) == false)
    setPlaying(false);

  left = right = sample;   // a tone is mono, sent to both channels
}
