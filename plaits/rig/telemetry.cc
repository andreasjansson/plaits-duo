// Copyright 2026 Andreas Jansson.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Telemetry of the rig firmware.

#include "plaits/rig/telemetry.h"

#include <algorithm>
#include <cstring>

#ifndef TEST
#include <stm32f37x_conf.h>
#endif  // TEST

#ifndef TEST
// From the linker script.
extern "C" uint32_t _sidata, _sdata, _edata;
#endif  // TEST

namespace plaits {

using namespace std;

// The number of 32-bit words of a frame.
static const int kFrameWords = sizeof(TelemetryFrame) / 4;

// The cycles of the processor in a block (at 72MHz, 12 samples at 47872Hz).
static const uint32_t kBlockCycles = 18048;


static const int kPreambleBits = 32;
static const int kSamplesPerBit = 6;
static const int kFrameSamples = \
    (kPreambleBits + 32 * kFrameWords) * kSamplesPerBit;
static const int16_t kAmplitude = 12000;

void Telemetry::Init() {
#ifndef TEST
  // The cycle counter of the processor.
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif  // TEST
  memset(&next_, 0, sizeof(next_));
  cv_sum_squares_[0] = cv_sum_squares_[1] = 0;
  countdown_ = rig.telemetry_interval();
  image_ = 0;
#ifndef TEST
  // The image: the code and constants, then the initial values of the
  // variables (see the linker script).
  const uint32_t* end = &_sidata + (&_edata - &_sdata);
  uint32_t hash = 2166136261u;
  for (const uint32_t* p = (const uint32_t*)0x08008000; p < end; ++p) {
    hash = (hash ^ *p) * 16777619u;
  }
  image_ = hash;
#endif  // TEST
  position_ = -1;
}

void Telemetry::Snapshot(
    const Ui& ui,
    const Patch& patch,
    const Modulations& modulations,
    const Settings& settings) {
  TelemetryFrame& f = next_;
  f.magic = kTelemetryMagic;
  f.version_size = (kTelemetryVersion << 16) | kFrameWords;
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    f.cv[i] = ui.cv_adc_.value(CvAdcChannel(i));
  }
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    f.pots[i] = ui.pots_adc_.value(PotsAdcChannel(i));
  }
  f.level_note_scale = ui.level_note_scale_;

  const PersistentData& p = settings.persistent_data();
  const ChannelCalibrationData& v_oct = p.channel_calibration_data[
      CV_ADC_CHANNEL_V_OCT];
  const ChannelCalibrationData& level = p.channel_calibration_data[
      CV_ADC_CHANNEL_LEVEL];
  f.v_oct_calibration[0] = v_oct.scale;
  f.v_oct_calibration[1] = v_oct.offset;
  f.level_calibration[0] = level.scale;
  f.level_calibration[1] = level.offset;
  f.level_note_calibration[0] = p.level_note_scale;
  f.level_note_calibration[1] = p.level_note_offset;

  f.engine = patch.engine;
  f.duo_mode = patch.duo_mode;
  f.duo_engine = patch.duo_engine;
  f.active_engine = ui.active_engine_;
  f.ui_mode = ui.mode_;
  f.patched = 0;
  const bool* patched = &modulations.frequency_patched;
  for (int i = 0; i < 5; ++i) {
    f.patched |= patched[i] << i;
  }
  f.calibrating = ui.calibrating_level_;
  for (int i = 0; i < 2; ++i) {
    f.cv_sum_squares[i][0] = static_cast<uint32_t>(cv_sum_squares_[i]);
    f.cv_sum_squares[i][1] = static_cast<uint32_t>(cv_sum_squares_[i] >> 32);
    cv_sum_squares_[i] = 0;
  }
  f.note = modulations.note;
  f.level_note = modulations.level_note;

  f.image = image_;
  f.commands = rig.commands();
  f.command_errors = rig.command_errors();
  f.last_command = rig.last_command();
  f.instrument = rig.instrument();
  copy(&rig.section_cycles()[0], &rig.section_cycles()[RIG_SECTION_LAST],
       &f.section_cycles[0]);
  copy(&rig.probe_max()[0], &rig.probe_max()[kRigNumProbes], &f.probe_max[0]);
  copy(&rig.probe_sum()[0], &rig.probe_sum()[kRigNumProbes], &f.probe_sum[0]);
  copy(&rig.probe_count()[0], &rig.probe_count()[kRigNumProbes],
       &f.probe_count[0]);
  rig.ClearSections();
  copy(&rig.engine_cycles()[0], &rig.engine_cycles()[kRigNumEngines],
       &f.engine_cycles[0]);
  copy(&rig.engine_overruns()[0], &rig.engine_overruns()[kRigNumEngines],
       &f.engine_overruns[0]);

  const uint32_t* words = reinterpret_cast<const uint32_t*>(&f);
  uint32_t hash = 2166136261u;
  for (int i = 0; i < kFrameWords - 1; ++i) {
    hash = (hash ^ words[i]) * 16777619u;
  }
  f.checksum = hash;

  // (As words: the struct copy would call memcpy, which newlib-nano does a
  // byte at a time - for long enough to delay the audio.)
  const uint32_t* from = reinterpret_cast<const uint32_t*>(&f);
  uint32_t* to = reinterpret_cast<uint32_t*>(&frame_);
  for (int i = 0; i < kFrameWords; ++i) {
    to[i] = from[i];
  }
  ++next_.frame;
  memset(&next_.blocks, 0, \
      reinterpret_cast<const char*>(&next_.cv) - \
      reinterpret_cast<const char*>(&next_.blocks));
}

void Telemetry::Process(
    const Ui& ui,
    const Patch& patch,
    const Modulations& modulations,
    const Settings& settings,
    uint32_t start,
    uint32_t poll,
    uint32_t render,
    int16_t* aux,
    size_t stride,
    size_t size) {
  TelemetryFrame& f = next_;
  ++f.blocks;
  const uint32_t poll_cycles = poll - start;
  const uint32_t render_cycles = render - poll;
  const uint32_t cycles = render - start;
  if (poll_cycles > f.poll_cycles) f.poll_cycles = poll_cycles;
  if (render_cycles > f.render_cycles) f.render_cycles = render_cycles;
  if (cycles > f.cycles) f.cycles = cycles;
  if (cycles > kBlockCycles) ++f.overruns;
  rig.EndBlock(ui.active_engine_, cycles);

  const CvAdcChannel channels[2] = {
    CV_ADC_CHANNEL_V_OCT, CV_ADC_CHANNEL_LEVEL
  };
  for (int i = 0; i < 2; ++i) {
    const int32_t x = ui.cv_adc_.value(channels[i]);
    f.cv_sum[i] += x;
    cv_sum_squares_[i] += x * x;
  }

  // Not while LEVEL is calibrated with the voltages output on OUT and AUX.
  const bool due = rig.telemetry_interval() && --countdown_ <= 0;
  if ((due || rig.frame_requested()) && position_ < 0 && \
      !ui.calibration_output()) {
    Snapshot(ui, patch, modulations, settings);
    rig.clear_frame_request();
    countdown_ = rig.telemetry_interval();
    position_ = 0;
  }

  if (position_ < 0) {
    return;
  }
  const uint32_t* words = reinterpret_cast<const uint32_t*>(&frame_);
  for (size_t i = 0; i < size; ++i) {
    const int bit = position_ / kSamplesPerBit - kPreambleBits;
    const bool second_half = position_ % kSamplesPerBit >= kSamplesPerBit / 2;
    const bool value = bit >= 0 && ((words[bit >> 5] >> (31 - (bit & 31))) & 1);
    aux[i * stride] = value != second_half ? kAmplitude : -kAmplitude;
    if (++position_ == kFrameSamples) {
      position_ = -1;
      break;
    }
  }
}

}  // namespace plaits
