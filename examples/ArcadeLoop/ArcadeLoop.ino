// ---- ArcadeLoop -------------------------------------------------------------------------
// Background music, the way an arcade cabinet does it: a lead line and a bass line looping
// forever together, with sound effects played over the top without interrupting either.
//
// The tune is an original written for this example. Everything here is generated - there is no
// WAV file involved, so the whole soundtrack costs a few hundred bytes of note tables.
//
// Wiring is an I2S DAC or amplifier (MAX98357A, PCM5102, UDA1334A and similar) on the three
// pins below.

#include <SoundEngine.h>

constexpr int cI2S_DOUT = 13;  // data
constexpr int cI2S_BCLK = 14;  // bit clock
constexpr int cI2S_LRC  = 21;  // left/right clock, also called word select

SoundEngine sound(cI2S_LRC, cI2S_BCLK, cI2S_DOUT);

MelodyClip lead;
MelodyClip bass;
MelodyClip sfx;


// ---- The tune ---------------------------------------------------------------------------
// Four bars at 150 bpm, so a quarter note is 400 ms and an eighth is 200 ms. Chords are
// C - Am - F - G, one per bar.
//
// The two lines are both exactly 6400 ms long. That is what keeps them together: each clip
// restarts itself the sample after its last note ends, and both are driven by the same 44.1 kHz
// clock, so equal totals means they stay locked for as long as they play. Give the bass a
// different length - even by one note - and the two will walk apart audibly within a minute.
//
// Note arrays are not copied by MelodyClip, so they live at file scope.

constexpr uint16_t cEighth  = 200;
constexpr uint16_t cQuarter = 400;

// The library's note table starts at C4, which is where the lead already sits. A bass wants to
// be an octave below that to be heard as a separate voice rather than as part of the melody,
// and a Note carries a plain frequency in Hz - so the octave below is simply half.
constexpr float cNoteC3 = cNoteC4 / 2.0f;
constexpr float cNoteF3 = cNoteF4 / 2.0f;
constexpr float cNoteG3 = cNoteG4 / 2.0f;
constexpr float cNoteA3 = cNoteA4 / 2.0f;

const Note cLeadLine[] =
{
  // bar 1 - C
  { cNoteG5, cEighth }, { cNoteE5, cEighth }, { cNoteC5, cEighth }, { cNoteE5, cEighth },
  { cNoteG5, cEighth }, { cNoteC6, cEighth }, { cNoteG5, cQuarter },

  // bar 2 - Am
  { cNoteA5, cEighth }, { cNoteE5, cEighth }, { cNoteC5, cEighth }, { cNoteE5, cEighth },
  { cNoteA5, cQuarter }, { cNoteG5, cQuarter },

  // bar 3 - F
  { cNoteF5, cEighth }, { cNoteA5, cEighth }, { cNoteC6, cEighth }, { cNoteA5, cEighth },
  { cNoteF5, cQuarter }, { cNoteA5, cQuarter },

  // bar 4 - G, ending on a rest so the loop breathes before it comes round again
  { cNoteG5, cEighth }, { cNoteB5, cEighth }, { cNoteG5, cEighth }, { cNoteD5, cEighth },
  { cNoteG5, cQuarter }, { cNoteB5, cEighth }, { cNoteRest, cEighth },
};

// Root and fifth alternating on every eighth. A line that moves is far easier to pick out of a
// mix than one that holds a note, which matters more here than the pattern being clever.
const Note cBassLine[] =
{
  { cNoteC3, cEighth }, { cNoteG3, cEighth }, { cNoteC3, cEighth }, { cNoteG3, cEighth },
  { cNoteC3, cEighth }, { cNoteG3, cEighth }, { cNoteC3, cEighth }, { cNoteG3, cEighth },

  { cNoteA3, cEighth }, { cNoteE4, cEighth }, { cNoteA3, cEighth }, { cNoteE4, cEighth },
  { cNoteA3, cEighth }, { cNoteE4, cEighth }, { cNoteA3, cEighth }, { cNoteE4, cEighth },

  { cNoteF3, cEighth }, { cNoteC4, cEighth }, { cNoteF3, cEighth }, { cNoteC4, cEighth },
  { cNoteF3, cEighth }, { cNoteC4, cEighth }, { cNoteF3, cEighth }, { cNoteC4, cEighth },

  { cNoteG3, cEighth }, { cNoteD4, cEighth }, { cNoteG3, cEighth }, { cNoteD4, cEighth },
  { cNoteG3, cEighth }, { cNoteD4, cEighth }, { cNoteG3, cEighth }, { cNoteD4, cEighth },
};

// A pickup blip, fired over the music from loop(). Short and rising, which cuts through a
// square-wave lead better than anything long would.
const Note cPickup[] =
{
  { cNoteE5, 50 }, { cNoteA5, 50 }, { cNoteC6, 120 },
};

constexpr std::size_t cLeadNotes   = sizeof(cLeadLine) / sizeof(cLeadLine[0]);
constexpr std::size_t cBassNotes   = sizeof(cBassLine) / sizeof(cBassLine[0]);
constexpr std::size_t cPickupNotes = sizeof(cPickup)   / sizeof(cPickup[0]);

static_assert(cLeadNotes == 26, "lead line changed - check it still totals 6400 ms");
static_assert(cBassNotes == 32, "bass line changed - check it still totals 6400 ms");


// ---- Levels -----------------------------------------------------------------------------
// A tone is generated at half scale on the assumption it will be mixed, and the mixer clamps
// the sum. Unlike a recorded clip, whose peaks are brief and rarely coincide, a generated
// waveform holds its level - so three of them at once is worth budgeting for rather than
// setting by ear: 70 + 55 + 60 of half scale peaks at about 92% of full scale.
//
// The bass is quieter than the lead on paper and still louder in the mix. A square wave spends
// all its time at the rail, so its RMS equals its amplitude, where a triangle or sawtooth ramps
// through and manages only 1/sqrt(3) of it - a factor of 1.7. That is also why the bass is a
// square here: a small speaker reproduces almost nothing at C3's 130 Hz fundamental, and it is
// the harmonics that carry the line.

constexpr int cLeadVolume   = 60;
constexpr int cBassVolume   = 55;
constexpr int cPickupVolume = 40;

// How often loop() fires the pickup blip.
constexpr unsigned long cPickupIntervalMs = 2500;


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

  sound.setVolume(50);
  sound.setVolume(lead, cLeadVolume);
  sound.setVolume(bass, cBassVolume);
  sound.setVolume(sfx,  cPickupVolume);

  // The last argument is what makes them repeat. Started back to back rather than together:
  // the mixer can advance one 128-frame chunk between the two calls, which puts the bass up to
  // about 3 ms behind the lead - far below anything the ear places as an offset, and it does
  // not accumulate, because from here on both loops are the same length.
  sound.playMelody(lead, cLeadLine, cLeadNotes, Waveform::eSquare, true);
  sound.playMelody(bass, cBassLine, cBassNotes, Waveform::eSquare, true);

  Serial.println("music playing - blip every 2.5 s over the top");
}

// ----------------------------------------------------------------------------------------
void loop()
{
  static unsigned long lastPickupMs = 0;

  const unsigned long now = millis();

  if (now - lastPickupMs < cPickupIntervalMs)
    return;

  lastPickupMs = now;

  // Nothing is stopped first and nothing is waited on. play() returns immediately and clips
  // mix, so the blip lands on top of both loops and the music carries on underneath - which is
  // the whole point of a mixing engine, and the reason a game can play a sound effect without
  // its soundtrack stuttering.
  sound.playMelody(sfx, cPickup, cPickupNotes, Waveform::eSquare);

  Serial.println("blip");
}
