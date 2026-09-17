#include "I2SOutput.h"

namespace
{
  // The IDF checks bclk and ws, but not dout - a mistyped data pin otherwise configures
  // successfully and plays into nothing. All three are outputs here, so check all three.
  bool checkPin(const char* name, int pin)
  {
    // Range first: GPIO_IS_VALID_OUTPUT_GPIO() shifts by the pin number, which is undefined
    // past the width of the mask.
    if (pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t) pin))
      return true;

    Serial.print("I2SOutput: ");
    Serial.print(name);
    Serial.print(" is not an output-capable GPIO: ");
    Serial.println(pin);
    return false;
  }

  // Deliberately not short-circuited, so one call reports every bad pin.
  bool pinsAreValid(int bclkPin, int wsPin, int doutPin)
  {
    const bool bclkOk = checkPin("BCLK", bclkPin);
    const bool wsOk   = checkPin("WS",   wsPin);
    const bool doutOk = checkPin("DOUT", doutPin);
    return bclkOk && wsOk && doutOk;
  }

  void report(const char* call, esp_err_t err)
  {
    Serial.print("I2SOutput: ");
    Serial.print(call);
    Serial.print(" failed: ");
    Serial.println(err);
  }
}

// ----------------------------------------------------------------------------------------
I2SOutput::~I2SOutput()
{
  end();
}

#if SOUNDENGINE_I2S_NG

// ---- core 3.x - driver/i2s_std.h --------------------------------------------------------

// ----------------------------------------------------------------------------------------
bool I2SOutput::begin(uint32_t sampleRate, int bclkPin, int wsPin, int doutPin,
                      int port, int dmaBufCount, int dmaFrames)
{
  end();

  if (pinsAreValid(bclkPin, wsPin, doutPin) == false)
    return false;

  const i2s_port_t id = (port == cPortAuto) ? I2S_NUM_AUTO : (i2s_port_t) port;

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(id, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num  = dmaBufCount;
  chanCfg.dma_frame_num = dmaFrames;
  // On an underrun, send silence rather than replaying the stale buffer, which buzzes.
  chanCfg.auto_clear    = true;

  esp_err_t err = i2s_new_channel(&chanCfg, &myHandle, nullptr);
  if (err != ESP_OK)
  {
    report("i2s_new_channel", err);
    myHandle = nullptr;
    return false;
  }

  i2s_std_config_t stdCfg =
  {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg =
    {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t) bclkPin,
      .ws   = (gpio_num_t) wsPin,
      .dout = (gpio_num_t) doutPin,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };

  err = i2s_channel_init_std_mode(myHandle, &stdCfg);
  if (err != ESP_OK)
  {
    report("i2s_channel_init_std_mode", err);
    end();
    return false;
  }

  err = i2s_channel_enable(myHandle);
  if (err != ESP_OK)
  {
    report("i2s_channel_enable", err);
    end();
    return false;
  }

  return true;
}

// ----------------------------------------------------------------------------------------
void I2SOutput::end()
{
  if (myHandle != nullptr)
  {
    i2s_channel_disable(myHandle);   // no-op error if it was never enabled
    i2s_del_channel(myHandle);
    myHandle = nullptr;
  }
}

// ----------------------------------------------------------------------------------------
bool I2SOutput::isOpen() const
{
  return myHandle != nullptr;
}

// ----------------------------------------------------------------------------------------
esp_err_t I2SOutput::write(const void* data, size_t bytes, size_t& bytesWritten)
{
  return i2s_channel_write(myHandle, data, bytes, &bytesWritten, portMAX_DELAY);
}

#else

// ---- core 2.x - driver/i2s.h ------------------------------------------------------------

// ----------------------------------------------------------------------------------------
bool I2SOutput::begin(uint32_t sampleRate, int bclkPin, int wsPin, int doutPin,
                      int port, int dmaBufCount, int dmaFrames)
{
  end();

  if (pinsAreValid(bclkPin, wsPin, doutPin) == false)
    return false;

  myPort = (port == cPortAuto) ? I2S_NUM_0 : (i2s_port_t) port;

#if defined(SOC_I2S_NUM) && SOC_I2S_NUM < 2
  if (myPort != I2S_NUM_0)
  {
    Serial.println("I2SOutput: this chip has one I2S port - using port 0");
    myPort = I2S_NUM_0;
  }
#endif

  // Zero-initialised and then assigned field by field: core 2.x compiles as gnu++11, where
  // designated initializers are not standard, and this does not depend on the order the IDF
  // happens to declare the fields in.
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = sampleRate;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;   // Philips, as the 3.x slot config
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = dmaBufCount;                 // == dma_desc_num
  cfg.dma_buf_len          = dmaFrames;                   // == dma_frame_num, also in frames
  cfg.use_apll             = false;
  // On an underrun, send silence rather than replaying the stale buffer, which buzzes.
  cfg.tx_desc_auto_clear   = true;
  cfg.fixed_mclk           = 0;

  esp_err_t err = i2s_driver_install(myPort, &cfg, 0, nullptr);
  if (err != ESP_OK)
  {
    report("i2s_driver_install", err);
    return false;
  }

  myIsInstalled = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = bclkPin;
  pins.ws_io_num    = wsPin;
  pins.data_out_num = doutPin;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  err = i2s_set_pin(myPort, &pins);
  if (err != ESP_OK)
  {
    report("i2s_set_pin", err);
    end();
    return false;
  }

  i2s_zero_dma_buffer(myPort);

  return true;
}

// ----------------------------------------------------------------------------------------
void I2SOutput::end()
{
  if (myIsInstalled)
  {
    i2s_driver_uninstall(myPort);
    myIsInstalled = false;
  }
}

// ----------------------------------------------------------------------------------------
bool I2SOutput::isOpen() const
{
  return myIsInstalled;
}

// ----------------------------------------------------------------------------------------
esp_err_t I2SOutput::write(const void* data, size_t bytes, size_t& bytesWritten)
{
  return i2s_write(myPort, data, bytes, &bytesWritten, portMAX_DELAY);
}

#endif
