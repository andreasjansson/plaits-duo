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
// -----------------------------------------------------------------------------
//
// Slow sine LFO, updated once per block, for the drifts of the stereo images.

#ifndef PLAITS_DSP_DUO_DUO_LFO_H_
#define PLAITS_DSP_DUO_DUO_LFO_H_

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

class DuoLfo {
 public:
  DuoLfo() { }
  ~DuoLfo() { }

  // Frequency in Hz, initial phase in cycles.
  void Init(float frequency, float phase) {
    increment_ = frequency / kCorrectedSampleRate;
    phase_ = phase;
  }

  // Advances by one block of the given size, and returns the value (-1 to
  // 1).
  inline float Next(size_t size) {
    phase_ += increment_ * static_cast<float>(size);
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    return stmlib::Interpolate(lut_sine, phase_, 1024.0f);
  }

 private:
  float increment_;
  float phase_;
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_LFO_H_
