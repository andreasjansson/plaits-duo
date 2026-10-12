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
// The firmware (UI, settings and DSP code) compiled to WebAssembly for the web
// demo (see web/index.html), with the simulated hardware of plaits-sim (see
// plaits/test/sim): the page turns the knobs, patches the jacks, presses the
// buttons and reads the LEDs, and the firmware runs as on the module, one
// block of 12 samples at a time (see FillBuffer() in plaits/plaits.cc).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/synthesizer.h"
#include "plaits/settings.h"
#include "plaits/test/sim/sim_hardware.h"
#include "plaits/ui.h"
#include "stmlib/utils/random.h"

using namespace plaits;

namespace stmlib {
namespace sim_flash {
Image image;
}  // namespace sim_flash
}  // namespace stmlib

Synthesizer synthesizer;
Settings settings;
Ui ui;
Patch patch;
Modulations modulations;
char shared_buffer[16384];

// OUT and AUX of the blocks rendered by plaits_render, from -1 to 1.
const int kMaxBlocks = 64;
float out[kMaxBlocks * kBlockSize];
float aux[kMaxBlocks * kBlockSize];

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

// As the startup code does on power on: the RAM of the object is cleared,
// and its constructor is called.
template<typename T>
void PowerOn(T* object) {
  memset(static_cast<void*>(object), 0, sizeof(T));
  new (object) T();
}

// Power on, then the same as Init() in plaits.cc. The knobs, jacks and
// buttons keep their state (see plaits_reset_hardware), and the settings are
// kept in the simulated flash memory.
EXPORT(plaits_boot) void plaits_boot() {
  PowerOn(&synthesizer);
  PowerOn(&settings);
  PowerOn(&ui);
  PowerOn(&patch);
  PowerOn(&modulations);
  memset(shared_buffer, 0, sizeof(shared_buffer));
  stmlib::Random::Seed(0x21);
  stmlib::BufferAllocator allocator(shared_buffer, 16384);
  synthesizer.Init(&allocator);
  settings.Init();
  ui.Init(&patch, &modulations, &settings);
}

// Knobs at noon, nothing patched, no button pressed.
EXPORT(plaits_reset_hardware) void plaits_reset_hardware() {
  sim::ResetHardware();
}

// Renders blocks of 12 samples (at most kMaxBlocks), in plaits_out and
// plaits_aux, as FillBuffer() in plaits.cc does.
EXPORT(plaits_render) void plaits_render(int blocks) {
  if (blocks > kMaxBlocks) {
    blocks = kMaxBlocks;
  }
  float* o = out;
  float* a = aux;
  for (int b = 0; b < blocks; ++b) {
    Voice::Frame frames[kBlockSize];
    ui.Poll();
    synthesizer.Render(patch, modulations, frames, kBlockSize);
    ui.set_active_engine(synthesizer.active_engine(patch));
    // While LEVEL is calibrated, the module outputs two DC voltages in turn
    // on OUT and AUX: here, silence instead (no square wave with a large DC
    // offset in the speakers).
    const bool calibrating = ui.calibration_output() != 0;
    for (size_t i = 0; i < kBlockSize; ++i) {
      // The output stage of the module inverts the DAC signal.
      *o++ = calibrating ? 0.0f : -static_cast<float>(frames[i].out) / 32768.0f;
      *a++ = calibrating ? 0.0f : -static_cast<float>(frames[i].aux) / 32768.0f;
    }
  }
}

EXPORT(plaits_out) float* plaits_out() { return out; }
EXPORT(plaits_aux) float* plaits_aux() { return aux; }

// Knobs, in PotsAdcChannel order: FREQUENCY, HARMONICS, TIMBRE, MORPH, and
// the TIMBRE, FM and MORPH attenuverters. From 0 to 1.
EXPORT(plaits_set_pot) void plaits_set_pot(int pot, float value) {
  if (pot >= 0 && pot < sim::kNumPots) {
    sim::hardware.pot[pot] = value;
  }
}

// Jacks, in CvAdcChannel order: MODEL, V/OCT, FM, HARMONICS, TIMBRE, MORPH,
// TRIGGER, LEVEL.
EXPORT(plaits_set_cv) void plaits_set_cv(int jack, float volts) {
  if (jack >= 0 && jack < sim::kNumCvInputs) {
    sim::hardware.cv_volts[jack] = volts;
  }
}

EXPORT(plaits_set_patched) void plaits_set_patched(int jack, int patched) {
  if (jack >= 0 && jack < sim::kNumCvInputs) {
    sim::hardware.patched[jack] = patched != 0;
  }
}

// Buttons: 0 left, 1 right.
EXPORT(plaits_set_button) void plaits_set_button(int button, int down) {
  if (button >= 0 && button < sim::kNumSwitches) {
    sim::hardware.switch_down[button] = down != 0;
  }
}

// LEDs from top to bottom: 0 off, 1 green, 2 red, 3 yellow.
EXPORT(plaits_led) int plaits_led(int led) {
  if (led < 0 || led >= sim::kNumSimLeds) {
    return 0;
  }
  switch (sim::hardware.led_color[led]) {
    case LED_COLOR_GREEN: return 1;
    case LED_COLOR_RED: return 2;
    case LED_COLOR_YELLOW: return 3;
    default: return 0;
  }
}

EXPORT(plaits_duo_mode) int plaits_duo_mode() {
  return patch.duo_mode ? 1 : 0;
}

// The selected model, from 0 to 15 (8 to 15: the second bank, red LEDs), of
// the original firmware or of duo mode.
EXPORT(plaits_model) int plaits_model() {
  return patch.duo_mode ? patch.duo_engine : patch.engine;
}

// Selects a model, as pressing the buttons does (without the presses).
EXPORT(plaits_select_model) void plaits_select_model(int model) {
  if (model < 0 || model > 15) {
    return;
  }
  if (patch.duo_mode) {
    patch.duo_engine = model;
  } else {
    patch.engine = model;
  }
}

// TIMBRE and MORPH as the voice computes them when their CV input is patched:
// the knob, plus the CV scaled by the attenuverter (see
// Voice::ApplyModulations).
float Modulated(float base, float amount, bool patched, float cv) {
  if (!patched) {
    return base;
  }
  amount *= std::max(fabsf(amount) - 0.05f, 0.05f);
  amount *= 1.05f;
  float value = base + amount * cv;
  CONSTRAIN(value, 0.0f, 1.0f);
  return value;
}

EXPORT(plaits_timbre) float plaits_timbre() {
  return Modulated(patch.timbre, patch.timbre_modulation_amount,
                   modulations.timbre_patched, modulations.timbre);
}

EXPORT(plaits_morph) float plaits_morph() {
  return Modulated(patch.morph, patch.morph_modulation_amount,
                   modulations.morph_patched, modulations.morph);
}
