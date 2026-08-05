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
AudioPlayer::AudioPlayer(int lrclkPin, int bclkPin, int doutPin)
: myLRCLKPin(lrclkPin), myBCLKPin(bclkPin), myDOUTPin(doutPin)
{
  // Pins only. The I2S channel is set up in begin().
}

// ----------------------------------------------------------------------------------------
bool AudioPlayer::begin()
{
  // Configure and create a TX I2S channel (Arduino Core 3.x / IDF 5.x "i2s_std" API).

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num  = 8;   // buffer/latency trade-off for UI sound effects mixed with
  chanCfg.dma_frame_num = 256; // display + WiFi/ESP-NOW load; see SoundEngine for task priority

  esp_err_t err = i2s_new_channel(&chanCfg, &myI2SHandle, nullptr);
  if (err != ESP_OK)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.print(" -> i2s_new_channel failed: ");
    Serial.println(err);
    myI2SHandle = nullptr;
    return false;
  }

  i2s_std_config_t stdCfg =
  {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cAudioSampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg =
    {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)myBCLKPin,
      .ws   = (gpio_num_t)myLRCLKPin,
      .dout = (gpio_num_t)myDOUTPin,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };

  err = i2s_channel_init_std_mode(myI2SHandle, &stdCfg);
  if (err != ESP_OK)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.print(" -> i2s_channel_init_std_mode failed: ");
    Serial.println(err);
    i2s_del_channel(myI2SHandle);
    myI2SHandle = nullptr;
    return false;
  }

  err = i2s_channel_enable(myI2SHandle);
  if (err != ESP_OK)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.print(" -> i2s_channel_enable failed: ");
    Serial.println(err);
    i2s_del_channel(myI2SHandle);
    myI2SHandle = nullptr;
    return false;
  }

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
AudioPlayer::~AudioPlayer()
{
  if (myI2SHandle != nullptr)
  {
    i2s_channel_disable(myI2SHandle);
    i2s_del_channel(myI2SHandle);
    myI2SHandle = nullptr;
  }
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
  if (myI2SHandle == nullptr)
  {
    vTaskDelay(10 / portTICK_PERIOD_MS); // avoid spinning if the hardware failed to init
    return;
  }

  size_t bytesWritten;
  esp_err_t err = i2s_channel_write(myI2SHandle, myMixBuffer, sizeof(myMixBuffer), &bytesWritten, portMAX_DELAY);

  if (err != ESP_OK)
  {
    Serial.print(__PRETTY_FUNCTION__);
    Serial.print(" -> i2s_channel_write failed: ");
    Serial.println(err);
  }
}
