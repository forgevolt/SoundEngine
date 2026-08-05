#include "FileClip.h"

#include <esp_heap_caps.h>

// ----------------------------------------------------------------------------------------
FileClip::~FileClip()
{
  unload();
}

// ----------------------------------------------------------------------------------------
void FileClip::unload()
{
  setWavData(nullptr, 0);   // silences the clip before the memory goes away

  if (myBuffer != nullptr)
  {
    heap_caps_free(myBuffer);
    myBuffer = nullptr;
  }

  mySize = 0;
}

// ----------------------------------------------------------------------------------------
bool FileClip::load(fs::FS& filesystem, const char* path)
{
  unload();

  File file = filesystem.open(path, "r");

  if (file == false || file.isDirectory() == true)
  {
    Serial.print("FileClip: cannot open ");
    Serial.println(path);
    return false;
  }

  const std::size_t size = file.size();

  if (size < 12)   // smaller than a RIFF header
  {
    Serial.print("FileClip: ");
    Serial.print(path);
    Serial.println(" is too small to be a WAV");
    file.close();
    return false;
  }

  // PSRAM first. heap_caps_malloc names the memory explicitly, so a board without PSRAM fails
  // here rather than silently taking the internal heap.
  myBuffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));

  if (myBuffer == nullptr)
  {
    if (cRequirePSRAM == true)
    {
      Serial.print("FileClip: no PSRAM for ");
      Serial.print(path);
      Serial.print(" (");
      Serial.print(size);
      Serial.println(" bytes) - fit the clip in flash, or allow the internal heap in FileClip.h");
      file.close();
      return false;
    }

    myBuffer = static_cast<uint8_t*>(malloc(size));
  }

  if (myBuffer == nullptr)
  {
    Serial.print("FileClip: out of memory for ");
    Serial.println(path);
    file.close();
    return false;
  }

  // One read of the whole file. Reading in one call rather than a loop of small ones is what
  // keeps this quick - the per-call overhead dominates on LittleFS.
  const std::size_t got = file.read(myBuffer, size);
  file.close();

  if (got != size)
  {
    Serial.print("FileClip: short read on ");
    Serial.print(path);
    Serial.print(" - got ");
    Serial.print(got);
    Serial.print(" of ");
    Serial.println(size);
    unload();
    return false;
  }

  mySize = size;

  // Parses, and reports itself if the bytes are not a WAV this engine can play.
  setWavData(myBuffer, mySize);

  return true;
}
