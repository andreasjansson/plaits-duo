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
// Simulator version of plaits/drivers/pots_adc.h.

#ifndef PLAITS_DRIVERS_POTS_ADC_H_
#define PLAITS_DRIVERS_POTS_ADC_H_

#include "stmlib/stmlib.h"

#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#endif  // PLAITS_RIG

#include "plaits/test/sim/sim_hardware.h"

namespace plaits {

enum PotsAdcChannel {
  POTS_ADC_CHANNEL_FREQ_POT,
  POTS_ADC_CHANNEL_HARMONICS_POT,
  POTS_ADC_CHANNEL_TIMBRE_POT,
  POTS_ADC_CHANNEL_MORPH_POT,
  POTS_ADC_CHANNEL_TIMBRE_ATTENUVERTER,
  POTS_ADC_CHANNEL_FM_ATTENUVERTER,
  POTS_ADC_CHANNEL_MORPH_ATTENUVERTER,
  POTS_ADC_CHANNEL_LAST
};

class PotsAdc {
 public:
  PotsAdc() { }
  ~PotsAdc() { }
  
  void Init() { Convert(); }
  void Convert() {
    for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
      values_[i] = static_cast<uint16_t>(sim::hardware.pot[i] * 65535.0f);
    }
  }
  
  inline int32_t value(PotsAdcChannel channel) const {
#ifdef PLAITS_RIG
    // The rig firmware: a position given by a command.
    if (rig.pot_overridden(channel)) {
      return rig.pot_override(channel);
    }
#endif  // PLAITS_RIG
    return static_cast<int32_t>(values_[channel]);
  }
  
  inline float float_value(PotsAdcChannel channel) const {
    return static_cast<float>(value(channel)) / 65536.0f;
  }
  
 private:
  uint16_t values_[POTS_ADC_CHANNEL_LAST];
  
  DISALLOW_COPY_AND_ASSIGN(PotsAdc);
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_POTS_ADC_H_
