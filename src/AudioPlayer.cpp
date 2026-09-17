#include "AudioPlayer.h"

// ---- AudioPlayer -----------------------------------------------------------------------

namespace
{
  int16_t clampToInt16(int32_t sample)
  {
    if (sample >  32767) return  32767;
    if (sample < -32768) return -32768;
    return int16_t(sample);
  }
}

// ----------------------------------------------------------------------------------------
AudioPlayer::AudioPlayer(int lrclkPin, int bclkPin, int doutPin, int i2sPort)
: myLRCLKPin(lrclkPin), myBCLKPin(bclkPin), myDOUTPin(doutPin), myI2SPort(i2sPort)
{
  // Pins only. The I2S channel is set up in begin().
}

// ----------------------------------------------------------------------------------------
bool AudioPlayer::begin()
{
  // Buffer/latency trade-off for UI sound effects mixed with display + WiFi/ESP-NOW load; see
  // SoundEngine for task priority. 8 x 256 frames is 2048 frames, 46 ms at 44.1 kHz.
  constexpr int cDmaBufCount = 8;
  constexpr int cDmaFrames   = 256;

  if (myOutput.begin(cAudioSampleRate, myBCLKPin, myLRCLKPin, myDOUTPin,
                     myI2SPort, cDmaBufCount, cDmaFrames) == false)
    return false;   // I2SOutput has already said what failed

  Serial.print("AudioPlayer: I2S TX channel initialized (LRCLK=");
  Serial.print(myLRCLKPin);
  Serial.print(", BCLK=");
  Serial.print(myBCLKPin);
  Serial.print(", DOUT=");
  Serial.print(myDOUTPin);
  Serial.println(")");

  return true;
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::setVolume(int volumePercent)
{
  if (volumePercent < 0)   volumePercent = 0;
  if (volumePercent > 200) volumePercent = 200;

  myVolumePercent = volumePercent;
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::play(AudioClip& clip, bool mix)
{
  if (isPlaying(clip))
    stop(clip);

  if (mix == false)
    stopAll();

  clip.start();

  clip.myPrev = myLastPlaying;
  clip.myNext = nullptr;
  if (myLastPlaying != nullptr)
    myLastPlaying->myNext = &clip;
  else
    myFirstPlaying = &clip;
  myLastPlaying = &clip;
}

// ----------------------------------------------------------------------------------------
bool AudioPlayer::isPlaying(const AudioClip& clip) const
{
  return (myFirstPlaying == &clip || clip.myPrev != nullptr || clip.myNext != nullptr);
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::stop(AudioClip& clip)
{
  if (isPlaying(clip) == false)
    return;

  clip.myIsPlaying = false;
  unlink(clip);
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::stopAll()
{
  AudioClip* clip = myFirstPlaying;
  while (clip != nullptr)
  {
    AudioClip* next = clip->myNext;
    clip->myIsPlaying = false;
    clip->myPrev = clip->myNext = nullptr;
    clip = next;
  }
  myFirstPlaying = myLastPlaying = nullptr;
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::unlink(AudioClip& clip)
{
  if (clip.myPrev != nullptr) clip.myPrev->myNext = clip.myNext; else myFirstPlaying = clip.myNext;
  if (clip.myNext != nullptr) clip.myNext->myPrev = clip.myPrev; else myLastPlaying  = clip.myPrev;
  clip.myPrev = clip.myNext = nullptr;
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::mixSamples(int16_t& left, int16_t& right)
{
  int32_t leftSum = 0, rightSum = 0;

  AudioClip* clip = myFirstPlaying;
  while (clip != nullptr)
  {
    AudioClip* next = clip->myNext;

    if (clip->myIsPlaying)
    {
      int16_t l, r;
      clip->nextSample(l, r);

      leftSum  += (int32_t(l) * clip->myVolumePercent) / 100;
      rightSum += (int32_t(r) * clip->myVolumePercent) / 100;
    }

    if (clip->myIsPlaying == false)
    {
      if (clip->myRepeatForever)
        clip->start();
      else
        unlink(*clip);
    }

    clip = next;
  }

  // Master volume, applied once to the (not yet clipped) 32-bit mix so this scaling can never
  // itself introduce wraparound - only the final clamp below can.
  leftSum  = (leftSum  * myVolumePercent) / 100;
  rightSum = (rightSum * myVolumePercent) / 100;

  left  = clampToInt16(leftSum);
  right = clampToInt16(rightSum);
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::prepareChunk()
{
  for (size_t i = 0; i < cChunkSize; i++)
  {
    int16_t left, right;
    mixSamples(left, right);
    myMixBuffer[i] = (uint32_t(uint16_t(left)) << 16) | uint16_t(right);
  }
}

// ----------------------------------------------------------------------------------------
void AudioPlayer::writeChunk()
{
  if (myOutput.isOpen() == false)
  {
    vTaskDelay(10 / portTICK_PERIOD_MS); // avoid spinning if the hardware failed to init
    return;
  }

  size_t bytesWritten;
  esp_err_t err = myOutput.write(myMixBuffer, sizeof(myMixBuffer), bytesWritten);

  if (err != ESP_OK)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.print(" -> write failed: ");
    Serial.println(err);
  }
}
