#include "AudioClip.h"
#include <cstring>

// ---- AudioClip -------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------
AudioClip::AudioClip(const uint8_t* wavData, std::size_t wavSize)
{
  setWavData(wavData, wavSize);
}

// ----------------------------------------------------------------------------------------
void AudioClip::setWavData(const uint8_t* wavData, std::size_t wavSize)
{
  // Start from nothing, so a failed parse leaves the clip silent rather than still pointing at
  // whatever it held before.
  myData        = nullptr;
  myDataSize    = 0;
  myNumChannels = 0;
  myIsPlaying   = false;

  // Clearing on purpose - a clip being unloaded, or one constructed empty. Not a complaint.
  if (wavData == nullptr && wavSize == 0)
    return;

  // Minimal RIFF/WAVE chunk walker. Copes with WAV files carrying extra chunks (e.g.
  // LIST/INFO) ahead of "fmt "/"data" rather than assuming a fixed 44-byte header.
  //
  // Every access is bounds-checked against wavSize, the caller's actual buffer length. The
  // size fields inside the file are treated as untrusted input: a truncated or corrupt clip
  // must fail to parse rather than read past the end of the array.

  if (wavData == nullptr || wavSize < 12 ||
      memcmp(wavData, "RIFF", 4) != 0 || memcmp(wavData + 8, "WAVE", 4) != 0)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.println(" -> not a RIFF/WAVE file - clip will be silent");
    return;
  }

  uint32_t riffSize;
  memcpy(&riffSize, wavData + 4, 4);

  // riffSize is "bytes following this field". Believe it only as far as the buffer actually
  // extends - in 64-bit, so a corrupt value near UINT32_MAX cannot wrap round to something
  // small and pass as valid.
  const uint64_t declaredSize = static_cast<uint64_t>(riffSize) + 8;
  const uint32_t fileSize     = static_cast<uint32_t>(declaredSize < wavSize ? declaredSize : wavSize);

  bool haveFmt = false, haveData = false;
  uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
  uint32_t sampleRate = 0;
  const uint8_t* dataPtr = nullptr;
  uint32_t dataSize = 0;

  uint32_t pos = 12; // past "RIFF" + size + "WAVE"
  while (pos + 8 <= fileSize)
  {
    const uint8_t* chunkId = wavData + pos;
    uint32_t chunkSize;
    memcpy(&chunkSize, wavData + pos + 4, 4);

    // The chunk body must lie entirely inside the buffer. 64-bit again: a chunkSize near
    // UINT32_MAX would otherwise wrap and slip past this test.
    const uint64_t chunkEnd = static_cast<uint64_t>(pos) + 8 + chunkSize;
    if (chunkEnd > fileSize)
    {
      Serial.print(__PRETTY_FUNCTION__);
      Serial.println(" -> truncated chunk, ignoring the remainder");
      break;
    }

    const uint8_t* chunkData = wavData + pos + 8;

    if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
    {
      memcpy(&audioFormat,   chunkData + 0,  2);
      memcpy(&numChannels,   chunkData + 2,  2);
      memcpy(&sampleRate,    chunkData + 4,  4);
      memcpy(&bitsPerSample, chunkData + 14, 2);
      haveFmt = true;
    }
    else if (memcmp(chunkId, "data", 4) == 0)
    {
      dataPtr  = chunkData;
      dataSize = chunkSize;
      haveData = true;
    }

    // Chunks are word-aligned: a chunk with an odd size is followed by one pad byte.
    // chunkEnd is always >= pos + 8, so the loop makes forward progress and terminates.
    pos = static_cast<uint32_t>(chunkEnd + (chunkSize & 1));
  }

  if (haveFmt == false || haveData == false || audioFormat != 1 /* PCM */ ||
      bitsPerSample != 16 || (numChannels != 1 && numChannels != 2) || sampleRate == 0)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.println(" -> unsupported WAV (need 16-bit PCM, mono or stereo) - clip will be silent");
    return;
  }

  myData        = dataPtr;
  myDataSize    = dataSize;
  myNumChannels = numChannels;

  // Q16.16 resampling step, rounded to nearest. 64-bit intermediate because
  // (sampleRate << 16) overflows uint32_t above ~65 kHz.
  mySourceStep  = static_cast<uint32_t>(
                    ((static_cast<uint64_t>(sampleRate) << cFracBits) + cAudioSampleRate / 2)
                    / cAudioSampleRate);
}

// ----------------------------------------------------------------------------------------
void AudioClip::setVolume(int volumePercent)
{
  if (volumePercent < 0)   volumePercent = 0;
  if (volumePercent > 200) volumePercent = 200;

  myVolumePercent = volumePercent;
}

// ----------------------------------------------------------------------------------------
void AudioClip::start()
{
  myReadPos   = 0;
  myIsPlaying = (myData != nullptr && myDataSize > 0);
}

// ----------------------------------------------------------------------------------------
void AudioClip::nextSample(int16_t& left, int16_t& right)
{
  uint32_t frameIdx  = static_cast<uint32_t>(myReadPos >> cFracBits);
  uint32_t frameSize = myNumChannels * 2; // bytes per frame: 16-bit samples, 1 or 2 channels
  uint32_t byteIdx   = frameIdx * frameSize;

  if (myIsPlaying == false || byteIdx + frameSize > myDataSize)
  {
    myIsPlaying = false;
    left = right = 0;
    return;
  }

  // 16-bit little-endian samples; bitwise OR (rather than addition) avoids relying on
  // signed-integer promotion behaviour for the high byte.
  left = int16_t(myData[byteIdx] | (myData[byteIdx + 1] << 8));
  right = (myNumChannels == 2)
        ? int16_t(myData[byteIdx + 2] | (myData[byteIdx + 3] << 8))
        : left; // mono source -> duplicate to both output channels

  myReadPos += mySourceStep;
}
