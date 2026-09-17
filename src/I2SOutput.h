#pragma once

#include <Arduino.h>

#if defined(__has_include)
#  if __has_include(<esp_arduino_version.h>)
#    include <esp_arduino_version.h>
#  endif
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#  error "SoundEngine needs ESP32 Arduino core 2.0 or later"
#endif

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#  define SOUNDENGINE_I2S_NG 1   // core 3.x / IDF 5.x
#  include <driver/i2s_std.h>
#else
#  define SOUNDENGINE_I2S_NG 0   // core 2.x / IDF 4.4
#  include <driver/i2s.h>
#endif

// ---- I2SOutput -------------------------------------------------------------------------
// One I2S TX channel, 16-bit stereo. The two cores ship different I2S drivers; this is the only
// place in the library that knows which one it was compiled against.

class I2SOutput
{
  public:
    // Let the driver choose. Core 3.x allocates a free port; core 2.x cannot, and takes port 0.
    static constexpr int cPortAuto = -1;

    I2SOutput() = default;
    ~I2SOutput();

    // Owns a driver handle - copying would leave two objects releasing the same one.
    I2SOutput(const I2SOutput&) = delete;
    I2SOutput& operator=(const I2SOutput&) = delete;

    // Configures the channel and starts it. dmaBufCount x dmaFrames is the buffered depth.
    bool begin(uint32_t sampleRate, int bclkPin, int wsPin, int doutPin,
               int port = cPortAuto, int dmaBufCount = 8, int dmaFrames = 256);
    void end();
    bool isOpen() const;

    // Blocks until the DMA has taken all of it.
    esp_err_t write(const void* data, size_t bytes, size_t& bytesWritten);

  private:
#if SOUNDENGINE_I2S_NG
    i2s_chan_handle_t myHandle = nullptr;
#else
    i2s_port_t myPort        = I2S_NUM_0;
    bool       myIsInstalled = false;
#endif
};
