// ---- MixAndLoop -------------------------------------------------------------------------
// Several clips looping at once.
//
// play(clip, true) repeats a clip until it is stopped, and clips mix rather than replace each
// other - so calling it on more than one leaves them all running together. The sketch layers
// three, removes one, then clears the lot, and starts over.
//
// Nothing here blocks: play() and stop() return immediately and the mixing happens in the
// engine's own task, so loop() just moves a timeline along.
//
// Wiring is an I2S DAC or amplifier (MAX98357A, PCM5102, UDA1334A and similar) on the three
// pins below.

#include <SoundEngine.h>

constexpr int cI2S_DOUT = 13;  // data
constexpr int cI2S_BCLK = 14;  // bit clock
constexpr int cI2S_LRC  = 21;  // left/right clock, also called word select

SoundEngine sound(cI2S_LRC, cI2S_BCLK, cI2S_DOUT);

constexpr unsigned long cStepMs = 4000;   // how long each stage of the timeline lasts

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

  sound.setVolume(100);

  // The mixer sums the clips and clamps the total, so levels are set by ear rather than to
  // add up to 100 - the peaks rarely coincide. The quieter background makes the layering
  // easier to hear.
  sound.setVolume(sound.signal(), 50);   // background
  sound.setVolume(sound.beep(),   70);
  sound.setVolume(sound.click(),  100);   // shortest clip, so it reads as the rhythm on top
}

// ----------------------------------------------------------------------------------------
void loop()
{
  static unsigned long lastStepMs = 0;
  static int           stage      = -1;   // -1 so the first pass runs stage 0 immediately

  const unsigned long now = millis();

  if (stage >= 0 && now - lastStepMs < cStepMs)
    return;

  lastStepMs = now;
  stage      = (stage + 1) % 5;

  switch (stage)
  {
    case 0:
      // Loops until stopped. On its own to begin with.
      Serial.println("signal, looping");
      sound.play(sound.signal(), true);
      break;

    case 1:
      // The second argument is what makes it repeat; leaving it out plays once.
      Serial.println("+ beep - two clips at once");
      sound.play(sound.beep(), true);
      break;

    case 2:
      Serial.println("+ click - three at once");
      sound.play(sound.click(), true);
      break;

    case 3:
      // One clip stops, the others carry on untouched.
      Serial.println("- beep - the other two keep running");
      sound.stop(sound.beep());
      break;

    case 4:
      Serial.println("silence");
      sound.stopAll();
      break;
  }
}
