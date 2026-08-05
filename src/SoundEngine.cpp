#include "SoundEngine.h"

#include "sounds/Click.h"
#include "sounds/Beep.h"
#include "sounds/Signal.h"


// ---- SoundEngine -----------------------------------------------------------------------

// Default sounds clips
static AudioClip clickClip(cSoundClickWAV);
static AudioClip beepClip(cSoundBeepWAV);
static AudioClip signalClip(cSoundSignalWAV);

// ----------------------------------------------------------------------------------------
SoundEngine::SoundEngine(const int i2sLRC_Pin, const int i2sBCLK_Pin, const int i2sDOUT_Pin)
: myAudio(i2sLRC_Pin, i2sBCLK_Pin, i2sDOUT_Pin),
  myIsTaskRunning(false)
{
  myMutex = xSemaphoreCreateMutex();
}

// ----------------------------------------------------------------------------------------
SoundEngine::~SoundEngine()
{
  // Signal the task to stop and give it a chance to finish its current loop iteration and park
  // itself, rather than calling vTaskDelete() immediately. vTaskDelete() does not run any
  // cleanup in the killed task, so deleting it while it might be mid-FillBuffer() (holding
  // myMutex) could leave the mutex "locked" right before it's deleted out from under it.
  // Note: in normal use SoundEngine is a long-lived singleton and this destructor never runs -
  // this is defensive, for the case where one is ever created/destroyed dynamically.

  myIsTaskRunning = false;
  vTaskDelay(10 / portTICK_PERIOD_MS);   // let the task loop notice the flag and settle
  vTaskDelete(myTaskHandle);
  vSemaphoreDelete(myMutex);
}

// ----------------------------------------------------------------------------------------
bool SoundEngine::begin()
{
  // Checked here rather than in the constructor: this object is constructed before Serial is up,
  // so a failure there could not be reported. A null handle would make every later
  // xSemaphoreTake() undefined.
  if (myMutex == nullptr)
  {
    Serial.print("ERROR: ");
    Serial.print(__PRETTY_FUNCTION__);
    Serial.println(" -> mutex could not be created");
    return false;
  }

  if (myAudio.begin() == false)
    return false;

  myIsTaskRunning = true;
  if (xTaskCreatePinnedToCore(fillBuffer, "fillBuffer", 8192, this,                 
                              tskIDLE_PRIORITY + 1, // One above idle is plenty for this workload while still leaving the
                                                    // idle task able to run.
                              &myTaskHandle,        // Task handle
                              1)                    // Core where the task should run
                                                    // Pinning the audio filler to core 1 (where loop() runs)
                                                    // avoids contending with WiFi/BT for CPU time if either is ever enabled.
      != pdPASS)
  {
    Serial.print("ERROR: ");
    Serial.print(__PRETTY_FUNCTION__);
    Serial.println(" -> failed to create fillBuffer task");
    myTaskHandle    = nullptr;
    myIsTaskRunning = false;
    return false;
  }

  return true;
}

// ----------------------------------------------------------------------------------------
void SoundEngine::setVolume(int volume)
{
  if (volume < 0)   volume = 0;
  if (volume > 200) volume = 200;

  xSemaphoreTake(myMutex, portMAX_DELAY);

  myAudio.setVolume(volume);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
int SoundEngine::getVolume()
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  const int volume = myAudio.getVolume();

  xSemaphoreGive(myMutex);

  return volume;
}

// ----------------------------------------------------------------------------------------
AudioClip& SoundEngine::click()
{
  return clickClip;
}

// ----------------------------------------------------------------------------------------
AudioClip& SoundEngine::beep()
{
  return beepClip;
}

// ----------------------------------------------------------------------------------------
AudioClip& SoundEngine::signal()
{
  return signalClip;
}

// ----------------------------------------------------------------------------------------
void SoundEngine::play(AudioClip& sound, bool repeat)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  sound.setRepeatForever(repeat);
  myAudio.play(sound);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::playTone(ToneClip& tone, float frequencyHz, uint32_t durationMs,
                           Waveform wave, bool repeat)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  tone.setTone(frequencyHz, durationMs, wave);
  tone.setRepeatForever(repeat);
  myAudio.play(tone);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::playMelody(MelodyClip& melody, const Note* notes, std::size_t count,
                             Waveform wave, bool repeat)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  melody.setMelody(notes, count, wave);
  melody.setRepeatForever(repeat);
  myAudio.play(melody);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
bool SoundEngine::load(FileClip& clip, fs::FS& filesystem, const char* path)
{
  // Stop it under the lock, then read the file outside it. Holding the mutex across the I/O
  // would block the filler task in prepareChunk() for as long as the read takes, and it has
  // only the DMA buffer between it and a dropout. Once stopped the clip is off the playlist,
  // so the mixer cannot reach it and the lock is not needed for the rest.
  xSemaphoreTake(myMutex, portMAX_DELAY);
  myAudio.stop(clip);
  xSemaphoreGive(myMutex);

  return clip.load(filesystem, path);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::unload(FileClip& clip)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);
  myAudio.stop(clip);
  xSemaphoreGive(myMutex);

  clip.unload();
}

// ----------------------------------------------------------------------------------------
bool SoundEngine::isPlaying(const AudioClip& sound)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  const bool ret = myAudio.isPlaying(sound);

  xSemaphoreGive(myMutex);

  return ret;
}

// ----------------------------------------------------------------------------------------
void SoundEngine::stop(AudioClip& sound)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  if (myAudio.isPlaying(sound))
    myAudio.stop(sound);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::stopAll()
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  myAudio.stopAll();

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::setVolume(AudioClip& sound, int volume)
{
  xSemaphoreTake(myMutex, portMAX_DELAY);

  sound.setVolume(volume);

  xSemaphoreGive(myMutex);
}

// ----------------------------------------------------------------------------------------
void SoundEngine::fillBuffer(void* pvParameters)
{
  SoundEngine* s = (SoundEngine*) pvParameters;

  while (true)
  {
    if (s->myIsTaskRunning == true)
    {
      // 1. Lock mutex, mix samples safely inside driver, release mutex immediately
      xSemaphoreTake(s->myMutex, portMAX_DELAY);
      s->myAudio.prepareChunk();
      xSemaphoreGive(s->myMutex);

      // 2. Perform blocking write to I2S hardware without holding the mutex
      // Note: writeChunk() calls i2s_channel_write() with portMAX_DELAY.
      // This blocks and yields the CPU naturally for ~2.9ms per chunk,
      // pacing the task to the 44.1kHz sample rate without a manual vTaskDelay.
      s->myAudio.writeChunk();
    }
    else
    {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}
