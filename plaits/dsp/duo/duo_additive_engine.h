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
// Stereo: each harmonic has its own phase difference between the two
// channels, which changes slowly. The fundamental is the same in both
// channels, the second and third harmonics swing a little around zero, and
// the phase difference of the higher harmonics rotates (as if the harmonic
// was detuned by a fraction of a Hz in one channel). Neighbouring harmonics
// turn in opposite directions, so that their pulls towards one side or the
// other cancel out. Every harmonic has the same level in both channels.

#ifndef PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/duo_ram_table.h"
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
  void UpdateRotations();
  void UpdateAmplitudes(
      float centroid,
      float slope,
      float bumps,
      float* amplitudes,
      size_t num_harmonics);

  DuoHarmonicBank bank_[kNumDuoVoices];

  // lut_sine, copied to RAM (read about 150 times per block).
  DuoRamTable<float> sine_;
  const float* sine_table_;

  float amplitudes_[kDuoNumHarmonics];

  // Stereo: phase of the LFO of each harmonic (in cycles), and the cosine
  // and sine of half the phase difference between the channels (scaled, see
  // DuoHarmonicBank::RenderStereo).
  float rotation_phase_[kNumDuoVoices][kDuoNumHarmonics];
  float rotation_cos_[kNumDuoVoices][kDuoNumHarmonics];
  float rotation_sin_[kNumDuoVoices][kDuoNumHarmonics];
  int rotation_counter_;

  DISALLOW_COPY_AND_ASSIGN(DuoAdditiveEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_ADDITIVE_ENGINE_H_
