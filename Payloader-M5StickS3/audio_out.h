#pragma once
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// I2S audio output to an EXTERNAL I2S DAC (e.g. PCM5102A / UDA1334) wired to
// the board's audio I2S bus. Used by the Music (AirPlay) payload to send audio
// out a real 3.5mm line-out instead of the onboard ES8311 speaker.
//
//   DAC pin     -> board pin
//   BCK / BCLK  -> G17   (ES8311 BCLK)
//   LCK / WS    -> G15   (ES8311 LRCK)
//   DIN         -> G14   (ES8311 DSDIN / I2S_DDAC = ESP playback data out)
//   SCK / MCLK  -> G18   (optional; PCM5102A runs fine with SCK tied low)
//   VIN / GND   -> 5V / GND  (PORT.A exposes both)
//
// NB: G16 (I2S_DADC) is the ES8311 ADC/mic data line (input to ESP) — NOT the
// DAC data. Feeding a DAC from G16 produces silence.
// NB: G14/15/17/18 are internal I2S lines, NOT broken out on Grove/HAT2 — they
// must be soldered to, OR remapped onto HAT2 EXT_GPIO pins via the I2S GPIO
// matrix (see hardware-audio-i2s memory note).
//
// 16-bit stereo. The ES8311 shares this bus but stays silent because it's
// never I2C-configured, so nothing comes out the onboard speaker.
// ---------------------------------------------------------------------------

bool   audioOutBegin(uint32_t sampleRate = 44100);
void   audioOutEnd();
bool   audioOutStarted();

// Re-tune the I2S clock (AirPlay streams are 44100, but keep this flexible).
void   audioOutSetSampleRate(uint32_t sampleRate);

// Write interleaved L/R 16-bit frames. Blocks until queued. Returns frames written.
size_t audioOutWrite(const int16_t* interleavedLR, size_t frames);

// Blocking sine test tone — proves the DAC + wiring + line-out path.
void   audioOutTestTone(uint32_t freqHz = 880, uint32_t ms = 250);
