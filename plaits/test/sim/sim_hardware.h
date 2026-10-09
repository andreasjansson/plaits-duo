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
// Simulated hardware, used by the stub drivers in plaits/test/sim/plaits/
// drivers (which replace the real ones when building the simulator: they
// come first in the include path).
//
// CV inputs are modeled as on the module: an inverting input stage scales
// the jack voltage and adds an offset before the ADC. The gains and offsets
// are consistent with the default calibration data in plaits/settings.cc.
// Unpatched jacks with a switch are connected to the normalization probe,
// which outputs 2V when high.

#ifndef PLAITS_TEST_SIM_SIM_HARDWARE_H_
#define PLAITS_TEST_SIM_SIM_HARDWARE_H_

#include <stdint.h>

namespace plaits {
namespace sim {

const int kNumCvInputs = 8;
const int kNumPots = 7;
const int kNumSwitches = 2;
const int kNumSimLeds = 8;

struct Hardware {
  float cv_volts[kNumCvInputs];  // In CvAdcChannel order.
  bool patched[kNumCvInputs];
  float cv_gain_error[kNumCvInputs];  // Multiplies the gain of the input.
  float pot[kNumPots];  // In PotsAdcChannel order, 0 to 1.
  bool switch_down[kNumSwitches];
  bool probe_output;
  bool probe_enabled;
  uint32_t led_color[kNumSimLeds];  // From top to bottom.
};

extern Hardware hardware;

void ResetHardware();

// 16-bit ADC reading of a CV input.
int16_t CvAdcValue(int channel);

}  // namespace sim
}  // namespace plaits

#endif  // PLAITS_TEST_SIM_SIM_HARDWARE_H_
