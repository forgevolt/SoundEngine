#pragma once

#include "AudioPlayer.h"
#include "ToneClip.h"
#include "MelodyClip.h"
#include "FileClip.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>


// ---- SoundEngine -----------------------------------------------------------------------
// Wrapper for the sound driver to simplify playing sounds.
//
// A clip is identified by the AudioClip object itself. The caller owns its clips and must keep
// them alive for as long as they may be played. Every method that touches a clip the mixer might
// be reading takes the engine's mutex, so clip state is only changed through them - do not call
// AudioClip::setVolume() or setRepeatForever() directly while a clip may be playing.
//
// The built-in clips are reached through click()/beep()/signal() and are used like any other:
//   engine.play(engine.beep());
//   engine.setVolume(engine.click(), 75);

class SoundEngine
{
  public:
    SoundEngine(const int i2sLRC_Pin, const int i2sBCLK_Pin, const int i2sDOUT_Pin);
    ~SoundEngine();

    // Owns a SemaphoreHandle_t and a TaskHandle_t - copying would leave two objects pointing at
    // the same OS handles, and the first destructor to run would invalidate them for the other.
    SoundEngine(const SoundEngine&) = delete;
    SoundEngine& operator=(const SoundEngine&) = delete;

    // Brings up the I2S channel and starts the buffer-filler task. Returns false if either
    // fails.
    bool begin();

    // Set/get the master volume (gain), applied on top of each clip's own volume.
    // Not persisted - the volume returns to AudioPlayer's default on every boot.
    // Vol as %, 0=silence, 50 half, 100 full, 200 twice as loud as original
    void setVolume(int volume);
    int getVolume();

    // Clips shipped with the engine.
    AudioClip& click();
    AudioClip& beep();
    AudioClip& signal();

    void play(AudioClip& sound, bool repeat = false);

    // Generated sound - a note, or a run of notes - without a WAV file. Both arm the clip and
    // start it under the engine's mutex, which is the point of having them: setTone() and
    // setMelody() rewrite state the mixer may be reading mid-sample.
    //
    // The clip belongs to the caller and must outlive the playing, as any clip must; a melody's
    // note array must too, since it is not copied.
    void playTone(ToneClip& tone, float frequencyHz, uint32_t durationMs,
                  Waveform wave = Waveform::eSine, bool repeat = false);

    void playMelody(MelodyClip& melody, const Note* notes, std::size_t count,
                    Waveform wave = Waveform::eSine, bool repeat = false);

    // Reads a WAV off the filesystem into PSRAM. Stops the clip first, so it is safe to call
    // on one that is playing - which FileClip::load() on its own is not. Returns false, having
    // said why on the serial port, if the file is missing, unreadable or not a usable WAV.
    //
    // Slow: expect tens of milliseconds. Load at start-up, not in response to a button.
    bool load(FileClip& clip, fs::FS& filesystem, const char* path);

    // Stops the clip and frees its memory.
    void unload(FileClip& clip);
    bool isPlaying(const AudioClip& sound);
    void stop(AudioClip& sound);
    void stopAll();

    // Per-clip gain. Vol as %, 0=silence, 50 half, 100 full, 200 twice as loud as original.
    void setVolume(AudioClip& sound, int volume);

  private:
    AudioPlayer myAudio;

    static void fillBuffer(void* pvParameters);
    std::atomic<bool> myIsTaskRunning;
    TaskHandle_t      myTaskHandle;
    SemaphoreHandle_t myMutex;  
};
