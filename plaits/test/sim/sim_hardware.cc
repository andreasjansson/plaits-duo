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
// Simulated hardware.

#include "plaits/test/sim/sim_hardware.h"

#include <cstring>

namespace plaits {
namespace sim {

Hardware hardware;

// ADC reading (from -1 to 1) for 0V, and its change per volt, for each input,
// in CvAdcChannel order: MODEL, V/OCT, FM, HARMONICS, TIMBRE, MORPH,
// TRIGGER, LEVEL. Most inputs have a 100k input resistor and a 33k feedback
// resistor (-0.2 per volt). The V/OCT zero is the one given in the comments
// of Ui::CalibrateC3.
static const float kZero[kNumCvInputs] = {
  0.0243f, 0.428571f, 0.0f, 0.0f, 0.0f, 0.0f, 0.666667f, 0.816667f
};

static const float kGain[kNumCvInputs] = {
  -0.2f, -0.2f, -0.2f, -0.2f, -0.125f, -0.125f, -0.2f, -0.2f
};

// Inputs connected to the normalization probe when unpatched.
static const bool kNormalled[kNumCvInputs] = {
  false, false, true, false, true, true, true, true
};

static const float kProbeVoltage = 2.0f;

// The voltage at OUT and AUX for a full scale DAC sample.
static const float kOutputVolts = 8.0f;

void ResetHardware() {
  memset(&hardware, 0, sizeof(hardware));
  for (int i = 0; i < kNumCvInputs; ++i) {
    hardware.cv_gain_error[i] = 1.0f;
  }
  for (int i = 0; i < kNumPots; ++i) {
    hardware.pot[i] = 0.5f;
  }
  hardware.vdda = 3.3f;
  hardware.adc_gain = 1.0f;
}

int16_t CvAdcValue(int channel) {
  float volts = 0.0f;
  if (hardware.patched[channel]) {
    volts = hardware.cv_volts[channel];
  } else if (kNormalled[channel] && hardware.probe_enabled &&
             hardware.probe_output) {
    volts = kProbeVoltage;
  }
  // The output of the input stage, then its reading by the ADC.
  float output = 1.65f * (1.0f + kZero[channel] + \
      kGain[channel] * hardware.cv_gain_error[channel] * volts);
  if (output < hardware.swing) {
    output = hardware.swing;
  } else if (output > hardware.vdda - hardware.swing) {
    output = hardware.vdda - hardware.swing;
  }
  float x = 2.0f * output / hardware.vdda * hardware.adc_gain - 1.0f;
  if (x < -1.0f) {
    x = -1.0f;
  } else if (x > 32767.0f / 32768.0f) {
    x = 32767.0f / 32768.0f;
  }
  return static_cast<int16_t>(x * 32768.0f);
}

void SetOutputs(int16_t main, int16_t aux) {
  for (int i = 0; i < kNumCvInputs; ++i) {
    if (hardware.patched_output[i]) {
      // The output stage inverts.
      const int16_t sample = hardware.patched_output[i] == 1 ? main : aux;
      hardware.cv_volts[i] = -sample / 32768.0f * kOutputVolts;
    }
  }
}

}  // namespace sim
}  // namespace plaits
