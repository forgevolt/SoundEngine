// ---- PlaySounds -------------------------------------------------------------------------
// Plays each of the six clips in turn: the three built into the engine, then three included
// from the library's own sounds folder.
//
// Wiring is an I2S DAC or amplifier (MAX98357A, PCM5102, UDA1334A and similar) on the three
// pins below. Nothing else is needed.

#include <SoundEngine.h>

constexpr int cI2S_DOUT = 13;  // data
constexpr int cI2S_BCLK = 14;  // bit clock
constexpr int cI2S_LRC  = 21;  // left/right clock, also called word select

SoundEngine sound(cI2S_LRC, cI2S_BCLK, cI2S_DOUT);

// Clips are plain byte arrays in flash. The engine never copies them, so they have to outlive
// every play() - file scope is the simplest way to guarantee that.
#include <sounds/Startup.h>
#include <sounds/Shutdown.h>
#include <sounds/Error.h>

AudioClip startupClip(cSoundStartupWAV);
AudioClip shutdownClip(cSoundShutdownWAV);
AudioClip errorClip(cSoundErrorWAV);

// ----------------------------------------------------------------------------------------
void playAndWait(AudioClip& clip)
{
  sound.play(clip);

  while (sound.isPlaying(clip) == true)
    delay(10);

  delay(1000);
}

// ----------------------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  if (sound.begin() == false)
  {
    Serial.println("SoundEngine failed to start - check the I2S pins");
    return;
  }

  // Master gain, then a per-clip gain on top of it. Both are percentages: 100 is the clip's
  // own level, 200 twice as loud.
  sound.setVolume(100);
  sound.setVolume(startupClip,  50);
  sound.setVolume(shutdownClip, 50);
  sound.setVolume(errorClip,   100);

  Serial.println("SoundEngine ready");
}

// ----------------------------------------------------------------------------------------
void loop()
{
  // Built in - no include needed.
  playAndWait(sound.click());
  playAndWait(sound.signal());
  playAndWait(sound.beep());

  playAndWait(startupClip);
  playAndWait(shutdownClip);
  playAndWait(errorClip);
}
