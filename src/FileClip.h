#pragma once

#include "AudioClip.h"
#include <FS.h>

// ---- FileClip --------------------------------------------------------------------------
// A WAV file read off the filesystem and held in PSRAM.
//
//   FileClip clip;
//   sound.load(clip, LittleFS, "/sounds/alarm.wav");
//   sound.play(clip);
//
// The file is read once, in full, at load time. After that it is an ordinary AudioClip - the
// mixer reads it straight out of memory, so playing it costs exactly what a built-in clip
// costs and adds no latency of its own.
//
// Deliberately not streamed. Reads from the filesystem block, and the buffer-filler task has
// to produce a chunk every 2.9 ms; a blocking read inside it is how a dropout happens. Loading
// up front moves that cost to a moment where a pause does not matter.
//
// PSRAM is required. Internal RAM on an ESP32 is around 200 kB with everything else running,
// and a second of 44.1 kHz stereo is 176 kB - quietly spending that is worse than refusing,
// because what breaks afterwards is something else entirely. load() says so and returns false.
// To allow the internal heap instead, change cRequirePSRAM below.

class FileClip : public AudioClip
{
  public:
    FileClip() = default;
    ~FileClip() override;

    // Owns a heap buffer; copying would free it twice.
    FileClip(const FileClip&) = delete;
    FileClip& operator=(const FileClip&) = delete;

    // Reads path into memory and parses it. Any filesystem will do - LittleFS, FFat, SD -
    // since it takes the FS by reference rather than choosing one.
    //
    // Do not call on a clip that is playing: use SoundEngine::load(), which stops it first.
    bool load(fs::FS& filesystem, const char* path);

    // Frees the buffer and silences the clip. Same warning as above - SoundEngine::unload()
    // is the safe way.
    void unload();

    bool        isLoaded() const { return myBuffer != nullptr; }
    std::size_t getSize()  const { return mySize; }

  private:
    // Whether a board without PSRAM is an error (true) or falls back to the internal heap.
    static constexpr bool cRequirePSRAM = true;

    uint8_t*    myBuffer = nullptr;
    std::size_t mySize   = 0;
};
