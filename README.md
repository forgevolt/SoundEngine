# SoundEngine

Short WAV sound effects on an ESP32, played through an I2S DAC.

Clips live in flash as byte arrays. The engine mixes any number of them together and streams the
result to the I2S peripheral from its own background task, so `play()` returns immediately and
playback never blocks the sketch. Each clip carries its own volume and can be set to repeat; a
master gain sits on top of all of them.

```cpp
#include <SoundEngine.h>

SoundEngine sound(21, 14, 13);   // LRCLK, BCLK, DOUT

void setup()
{
  sound.begin();
  sound.setVolume(100);
}

void loop()
{
  sound.play(sound.beep());
  delay(2000);
}
```

## Requirements

- An ESP32, with **Arduino-ESP32 core 2.0 or later**. The library selects the I2S driver to
  match: the `i2s_std` API on core 3.x, the older `driver/i2s.h` one on core 2.x. Nothing in
  the sketch changes either way.
- An I2S DAC or amplifier — MAX98357A, PCM5102, UDA1334A and similar all work. Three pins:

  | signal | meaning |
  |---|---|
  | LRCLK | left/right clock, also called word select or frame clock |
  | BCLK  | bit clock |
  | DOUT  | data out, ESP32 to DAC |

No other library is needed.

The engine takes a free I2S port by default. On core 2.x that is always port 0, because the
older driver cannot allocate one; if something else on the board owns it, name another:

```cpp
SoundEngine sound(21, 14, 13, I2S_NUM_1);
```

## Installing

**Library Manager** — in the Arduino IDE, *Sketch → Include Library → Manage Libraries*, search for
`SoundEngine`, install.

**From this repository** — *Code → Download ZIP*, then *Sketch → Include Library → Add .ZIP
Library*. Use this if you want a version that has not been released yet.

## Examples

`PlaySounds` — plays each of the six clips in turn, one at a time.

`MixAndLoop` — several clips looping together, layered on and removed one at a time.

`PlayTones` — generated notes and melodies, with no WAV file involved.

`ArcadeLoop` — a two-part looping soundtrack with sound effects mixed over the top, all
generated.

`PlayFromFile` — WAV files read from LittleFS into PSRAM at start-up.

## Sounds

Three clips are built in and need no include:

```cpp
sound.play(sound.click());
sound.play(sound.beep());
sound.play(sound.signal());
```

Three more ship with the library and are included when you want them:

```cpp
#include <sounds/Startup.h>
AudioClip startupClip(cSoundStartupWAV);

sound.play(startupClip);
```

They cost no flash unless a sketch includes them.

### Adding your own

A clip is 16-bit PCM WAV data — mono or stereo, any sample rate; the engine resamples to 44.1 kHz
as it plays. Convert your file to a hex byte array and drop it in a header alongside the others:

1. to WAV, if it is not already — for example <https://convertio.co/mp3-wav/>
2. to a hex array — for example <http://tomeko.net/online_tools/file_to_hex.php?lang=en>

The array is what `AudioClip` reads:

```cpp
const uint8_t cMySoundWAV[] PROGMEM = { 0x52, 0x49, 0x46, 0x46, ... };
```

An `AudioClip` does **not** copy those bytes, so the array has to outlive every `play()`. File
scope is the simplest way to be sure of that. A file that is not RIFF/WAVE, or not 16-bit PCM,
is reported once on the serial port and then plays silently rather than making noise.

## Playing several at once

Clips mix rather than replace, so `play()` on more than one leaves them all running. A second
argument repeats a clip until it is stopped:

```cpp
sound.play(sound.signal(), true);   // loops
sound.play(sound.beep(), true);     // joins it, both now playing
sound.stop(sound.beep());           // the other carries on
```

The mixer sums the clips and clamps the total, so several at full volume will run into that
ceiling and distort. Share the budget out with per-clip volumes — see `MixAndLoop`.

## Tones and melodies

Sound without a WAV file. A `ToneClip` is one generated note; a `MelodyClip` plays a run of them
from an array. Both are ordinary clips, so they mix, repeat and take a per-clip volume:

```cpp
ToneClip   tone;
MelodyClip melody;

sound.playTone(tone, cNoteA4, 200);                        // A4, 200 ms, sine
sound.playTone(tone, 1200.0f, 80, Waveform::eSquare);      // any frequency you like

const Note cStartup[] = { { cNoteC5, 90 }, { cNoteE5, 90 }, { cNoteG5, 180 } };
sound.playMelody(melody, cStartup, 3);
```

Four waveforms: `eSine`, `eSquare`, `eTriangle`, `eSawtooth`. Note constants run `cNoteC4` to
`cNoteC6` — `cNoteA4` is 440 Hz — and `cNoteRest` is a silent gap of the given length.

Arm the clips through `playTone()` and `playMelody()` rather than calling `setTone()` or
`setMelody()` yourself: those rewrite state the mixer may be part-way through reading, and the
engine's versions take its mutex first. A melody's note array is not copied, so it has to
outlive the playing — file scope is the simplest guarantee.

Two things about how they sound. Every note is faded in and out over about 5 ms, because a
waveform that starts or stops away from zero clicks — most audibly on square and sawtooth. And
tones are generated at half scale, on the assumption that they will be mixed with something;
turn a clip up with `setVolume()` if it is playing alone.

## Playing files from the filesystem

A `FileClip` reads a WAV off any filesystem — LittleFS, FFat, SD — into memory once, and is an
ordinary clip from then on:

```cpp
FileClip alarm;                                  // file scope: it must outlive every play()

sound.load(alarm, LittleFS, "/alarm.wav");       // slow, do it in setup()
sound.play(alarm);                               // costs no more than a built-in clip
```

**PSRAM is required.** Internal RAM is around 200 kB with everything else running and a second of
44.1 kHz stereo is 176 kB, so quietly spending it would break something else instead — `load()`
refuses and says so. Set `cRequirePSRAM` in `FileClip.h` to `false` to allow the internal heap.

**The file is read in full, not streamed.** Filesystem reads block, and the buffer-filler task
has to produce a chunk every 2.9 ms, so a read inside it is how a dropout happens. Loading up
front moves that cost to start-up, where a pause does not matter.

Use `sound.load()` and `sound.unload()` rather than the `FileClip` methods directly: they stop
the clip first, which the clip cannot do for itself, and freeing memory the mixer is reading is
not survivable.

LittleFS is the sensible choice on internal flash — it is in the core, wear-levelled, survives a
power cut mid-write, and reads far faster than audio needs. FFat is quicker for long sequential
reads but less robust; SPIFFS is deprecated.

### How long before you hear it

`play()` is not instant, and the filesystem is not why. The I2S DMA holds
`dma_desc_num` × `dma_frame_num` frames — 8 × 256 by default, which is 2048 frames, or **46 ms at
44.1 kHz**. A clip is mixed into the next 2.9 ms chunk and then queues behind whatever is already
in the DMA. Those two constants in `AudioPlayer::begin()` are the lever if button feedback needs
to feel tighter; shrinking them leaves the filler task less room to fall behind before it is
audible. If it ever does fall behind, the gap is silence rather than a buzz - the channel is set
to clear itself instead of replaying its last buffer.

## Volume

Two independent gains, both percentages, both clamped to 0–200. 100 is the clip's own recorded
level and 200 is twice as loud:

```cpp
sound.setVolume(80);              // master, applies to everything
sound.setVolume(errorClip, 150);  // this clip only
```

Neither is persisted; both return to their defaults on every boot.

## Threads

The buffer is filled by a FreeRTOS task the engine starts in `begin()`, so anything touching a
clip the mixer might be reading has to go through `SoundEngine`, which takes its mutex. In
particular do not call `AudioClip::setVolume()` or `setRepeatForever()` directly on a clip that
may be playing — use `SoundEngine::setVolume(clip, volume)` instead.

`play()`, `stop()`, `stopAll()` and `isPlaying()` are all safe to call from `loop()` at any time.

### Where the task runs

`begin()` takes the task's priority and core, and defaults to one above idle on core 1:

```cpp
sound.begin();                    // tskIDLE_PRIORITY + 1, core 1
sound.begin(6);                   // higher priority, same core
sound.begin(6, tskNO_AFFINITY);   // and let the scheduler place it
```

One above idle is fine when the sketch has the CPU largely to itself. It is not enough next to a
library that runs its own task higher - a display driver redrawing a full screen, say. If that
task holds the CPU for longer than the 46 ms the DMA is holding, the audio drops out; raise the
priority above it. That is cheap to do: the filler mixes a 2.9 ms chunk and then blocks until the
DMA wants more, so it is idle almost all the time and costs the rest of the sketch nothing however
high it sits.

Core 1 is where `loop()` runs, which keeps the filler away from WiFi and Bluetooth on core 0.
`tskNO_AFFINITY` leaves the choice to the scheduler.

## Credits

The I2S driver follows the approach of the XTronical audio driver,
<https://www.xtronical.com/i2sprerelease>.

## Licence

MIT. See LICENSE.
