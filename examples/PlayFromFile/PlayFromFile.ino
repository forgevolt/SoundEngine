// ---- PlayFromFile -----------------------------------------------------------------------
// WAV files kept on the ESP32's own filesystem instead of compiled into flash.
//
// Put some 16-bit PCM WAV files in a data/ folder next to this sketch, upload them with the
// LittleFS uploader plugin, then set the names below.
//
// First time on a board, the partition has never been formatted and mounting it fails with
// "Corrupted dir pair at {0x0, 0x1}". That is what blank flash looks like to LittleFS, not a
// fault. begin(true) formats it and carries on; it only ever formats when the mount has already
// failed, so it cannot wipe files that were there.
//
// Two things have to be true before any of this works:
//   - the board is set to a partition scheme that includes a filesystem
//     (Tools > Partition Scheme, anything listing SPIFFS or FATFS)
//   - the files have actually been uploaded - formatting gives an empty filesystem, not files
//
// PSRAM is required: the whole file is read into memory at start-up. That is deliberate -
// reads from the filesystem block, and the buffer-filler task has to produce a chunk every
// 2.9 ms, so a read inside it is a dropout waiting to happen. Loading up front puts the cost
// where a pause does not matter, and playing costs no more than a built-in clip afterwards.
//
// Wiring is an I2S DAC or amplifier (MAX98357A, PCM5102, UDA1334A and similar) on the three
// pins below.

#include <SoundEngine.h>
#include <LittleFS.h>

constexpr int cI2S_DOUT = 13;  // data
constexpr int cI2S_BCLK = 14;  // bit clock
constexpr int cI2S_LRC  = 21;  // left/right clock, also called word select

SoundEngine sound(cI2S_LRC, cI2S_BCLK, cI2S_DOUT);

const char* const cPaths[] =
{
  "/alarm.wav",
  "/ready.wav",
};

constexpr size_t cClipCount = sizeof(cPaths) / sizeof(cPaths[0]);

// Must outlive every play(), like any clip - so file scope, not a local.
FileClip clips[cClipCount];
size_t   loadedCount = 0;

// ----------------------------------------------------------------------------------------
// What is actually on the filesystem. Worth printing: an empty listing after a successful
// mount means the partition formatted fine and the upload is what has not happened.
void listFiles()
{
  File root = LittleFS.open("/");

  if (root == false || root.isDirectory() == false)
  {
    Serial.println("cannot open the root directory");
    return;
  }

  int count = 0;

  for (File entry = root.openNextFile(); entry; entry = root.openNextFile())
  {
    Serial.print("  ");
    Serial.print(entry.name());
    Serial.print("  ");
    Serial.print(entry.size());
    Serial.println(" bytes");

    count++;
    entry.close();
  }

  root.close();

  if (count == 0)
    Serial.println("  (empty - upload a data/ folder with the LittleFS uploader plugin)");
}

// ----------------------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  // true: format if the mount fails, which is what happens on a partition that has never
  // held a filesystem.
  if (LittleFS.begin(true) == false)
  {
    Serial.println("LittleFS will not mount even after formatting.");
    Serial.println("Check Tools > Partition Scheme - the board needs one with a filesystem.");
    return;
  }

  Serial.print("LittleFS mounted: ");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" of ");
  Serial.print(LittleFS.totalBytes());
  Serial.println(" bytes used");

  listFiles();

  if (sound.begin() == false)
  {
    Serial.println("SoundEngine failed to start - check the I2S pins");
    return;
  }

  sound.setVolume(100);

  // Loading is slow - tens of milliseconds each - which is why it happens here rather than in
  // response to a button press. load() reports on the serial port if a file is missing, is not
  // a WAV this engine can play, or does not fit.
  for (size_t i = 0; i < cClipCount; i++)
  {
    const unsigned long startedMs = millis();

    if (sound.load(clips[i], LittleFS, cPaths[i]) == true)
    {
      Serial.print("loaded ");
      Serial.print(cPaths[i]);
      Serial.print(" - ");
      Serial.print(clips[i].getSize());
      Serial.print(" bytes in ");
      Serial.print(millis() - startedMs);
      Serial.println(" ms");

      loadedCount++;
    }
  }

  if (loadedCount == 0)
    Serial.println("nothing loaded - playing a built-in clip instead");
}

// ----------------------------------------------------------------------------------------
void loop()
{
  if (loadedCount == 0)
  {
    // Still something to hear on a board with no files uploaded.
    sound.play(sound.beep());
    delay(2000);
    return;
  }

  for (size_t i = 0; i < cClipCount; i++)
  {
    if (clips[i].isLoaded() == false)
      continue;

    Serial.print("playing ");
    Serial.println(cPaths[i]);

    sound.play(clips[i]);

    while (sound.isPlaying(clips[i]) == true)
      delay(10);

    delay(1000);
  }
}
