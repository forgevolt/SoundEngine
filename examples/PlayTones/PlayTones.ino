// ---- PlayTones --------------------------------------------------------------------------
// Sound without a WAV file.
//
// A ToneClip is one generated note - frequency, duration, waveform. A MelodyClip is a run of
// them from a plain array, played through by itself. Both are ordinary clips, so they mix with
// recorded sounds, take a per-clip volume, and can repeat.
//
// Wiring is an I2S DAC or amplifier (MAX98357A, PCM5102, UDA1334A and similar) on the three
// pins below.

#include <SoundEngine.h>

constexpr int cI2S_DOUT = 13;  // data
constexpr int cI2S_BCLK = 14;  // bit clock
constexpr int cI2S_LRC  = 21;  // left/right clock, also called word select

SoundEngine sound(cI2S_LRC, cI2S_BCLK, cI2S_DOUT);

// Not called "tone": the Arduino core already declares a function of that name, and a global
// variable would collide with it.
ToneClip   toneClip;      // re-armed for each note below
MelodyClip melodyClip;

// A startup jingle. The array is not copied, so it has to outlive the playing - file scope.
// cNoteRest is a gap of the given length.
const Note cStartupTune[] =
{
  { cNoteC5, 90 },
  { cNoteE5, 90 },
  { cNoteG5, 90 },
  { cNoteC6, 220 },
};

// Two-tone alert, the sort of thing a machine says when something is wrong.
const Note cAlertTune[] =
{
  { cNoteA5, 140 }, { cNoteRest, 60 },
  { cNoteA5, 140 }, { cNoteRest, 60 },
  { cNoteF5, 300 },
};

// ----------------------------------------------------------------------------------------
void waitFor(AudioClip& clip)
{
  while (sound.isPlaying(clip) == true)
    delay(10);

  delay(400);
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

  sound.setVolume(100);

  // Tones are generated at half scale, leaving room for them to be mixed. Turn a clip up if it
  // is going to play on its own.
  sound.setVolume(toneClip,   150);
  sound.setVolume(melodyClip, 150);

  Serial.println("SoundEngine ready");
}

// ----------------------------------------------------------------------------------------
void loop()
{
  // ---- the same note in each of the four waveforms ---------------------------------------
  const Waveform waves[]     = { Waveform::eSine, Waveform::eSquare,
                                 Waveform::eTriangle, Waveform::eSawtooth };
  const char*    waveNames[] = { "sine", "square", "triangle", "sawtooth" };

  for (int i = 0; i < 4; i++)
  {
    Serial.print("A4, ");
    Serial.println(waveNames[i]);

    sound.playTone(toneClip, cNoteA4, 400, waves[i]);
    waitFor(toneClip);
  }

  // ---- a rising run, one tone at a time ---------------------------------------------------
  Serial.println("rising run");

  const float run[] = { cNoteC5, cNoteD5, cNoteE5, cNoteF5, cNoteG5 };

  for (float note : run)
  {
    sound.playTone(toneClip, note, 120, Waveform::eSquare);

    while (sound.isPlaying(toneClip) == true)
      delay(5);
  }

  delay(600);

  // ---- melodies, which sequence themselves ------------------------------------------------
  Serial.println("startup jingle");
  sound.playMelody(melodyClip, cStartupTune, 4, Waveform::eSine);
  waitFor(melodyClip);

  Serial.println("alert");
  sound.playMelody(melodyClip, cAlertTune, 5, Waveform::eSquare);
  waitFor(melodyClip);

  // ---- a melody is a clip, so it mixes ------------------------------------------------------
  // The jingle repeats underneath while a recorded clip plays over the top.
  Serial.println("jingle looping under a recorded clip");
  sound.setVolume(melodyClip, 60);
  sound.playMelody(melodyClip, cStartupTune, 4, Waveform::eTriangle, true);

  delay(500);
  sound.play(sound.beep());
  delay(2000);

  sound.stopAll();
  sound.setVolume(melodyClip, 150);
  delay(800);
}
