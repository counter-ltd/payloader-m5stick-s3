#include "audio_out.h"
#include <math.h>
#include "driver/i2s.h"   // legacy I2S driver — portable across ESP32 core 2.x/3.x

#define AUDIO_I2S_NUM   I2S_NUM_0
#define PIN_I2S_MCLK    18   // ES8311 MCLK
#define PIN_I2S_BCLK    17   // ES8311 BCLK / SCLK
#define PIN_I2S_LRCK    15   // ES8311 LRCK / WS
#define PIN_I2S_DOUT    14   // ES8311 DSDIN (I2S_DDAC) — ESP playback data OUT
                             // NB: G16 is I2S_DADC (ES8311 ASDOUT) = mic data IN, not output

static bool     s_started = false;
static uint32_t s_rate    = 44100;

bool audioOutBegin(uint32_t sampleRate) {
  if (s_started) return true;
  s_rate = sampleRate;

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = sampleRate;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // stereo
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = 256;
  cfg.use_apll             = true;   // cleaner audio clock; flip to false if APLL won't lock
  cfg.tx_desc_auto_clear   = true;
  cfg.fixed_mclk           = 0;

  if (i2s_driver_install(AUDIO_I2S_NUM, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = PIN_I2S_MCLK;
  pins.bck_io_num   = PIN_I2S_BCLK;
  pins.ws_io_num    = PIN_I2S_LRCK;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(AUDIO_I2S_NUM, &pins) != ESP_OK) {
    i2s_driver_uninstall(AUDIO_I2S_NUM);
    return false;
  }

  i2s_zero_dma_buffer(AUDIO_I2S_NUM);
  s_started = true;
  return true;
}

void audioOutEnd() {
  if (!s_started) return;
  i2s_zero_dma_buffer(AUDIO_I2S_NUM);
  i2s_driver_uninstall(AUDIO_I2S_NUM);
  s_started = false;
}

bool audioOutStarted() { return s_started; }

void audioOutSetSampleRate(uint32_t sampleRate) {
  if (!s_started || sampleRate == s_rate) return;
  s_rate = sampleRate;
  i2s_set_sample_rates(AUDIO_I2S_NUM, sampleRate);
}

size_t audioOutWrite(const int16_t* interleavedLR, size_t frames) {
  if (!s_started) return 0;
  size_t bytesWritten = 0;
  i2s_write(AUDIO_I2S_NUM, interleavedLR, frames * 2 * sizeof(int16_t),
            &bytesWritten, portMAX_DELAY);
  return bytesWritten / (2 * sizeof(int16_t));
}

void audioOutTestTone(uint32_t freqHz, uint32_t ms) {
  bool temp = !s_started;
  if (temp && !audioOutBegin()) return;

  const int N = 256;
  int16_t   buf[N * 2];
  uint32_t  total = (uint64_t)s_rate * ms / 1000;
  double    ph = 0.0, step = 2.0 * M_PI * (double)freqHz / (double)s_rate;

  for (uint32_t done = 0; done < total; ) {
    int n = (int)((total - done) < (uint32_t)N ? (total - done) : N);
    for (int i = 0; i < n; i++) {
      int16_t s = (int16_t)(sin(ph) * 8000.0);   // ~25% full-scale
      buf[i * 2]     = s;
      buf[i * 2 + 1] = s;
      ph += step;
      if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
    audioOutWrite(buf, n);
    done += n;
  }

  if (temp) audioOutEnd();
}
