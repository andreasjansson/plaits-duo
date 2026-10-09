// Copyright 2016 Emilie Gillet.
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
// Duo version of the harmonic oscillator.
//
// The spectral envelope does not depend on pitch, so it is computed once and
// shared by the two voices (each voice still applies its own anti-aliasing
// attenuation of the upper harmonics).
//
// Stereo: spectral panning. The fundamental stays in the center, and the
// other harmonics are dealt in pairs to the two sides (2 | 3 4 | 5 6 | 7 8...
// so that each side gets both odd and even harmonics), with some crossfeed.
// The pattern is mirrored for the second voice.

#ifndef PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/harmonic_bank.h"

namespace plaits {

class DuoAdditiveEngine : public DuoEngine {
 public:
  DuoAdditiveEngine() { }
  ~DuoAdditiveEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  void UpdateAmplitudes(
      float centroid,
      float slope,
      float bumps,
      float* amplitudes,
      size_t num_harmonics);

  DuoHarmonicBank bank_[kNumDuoVoices];

  float amplitudes_[kDuoNumHarmonics];
  float* temp_buffer_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoAdditiveEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_
