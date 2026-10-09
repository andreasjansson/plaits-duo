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
// Duo version of the wavetable oscillator.
//
// The rendering loop has been reorganized to run two voices in about the time
// the original engine took for one (see the .cc file). The result is the
// same as the original OUT within 1e-4.


#ifndef PLAITS_DSP_DUO_DUO_WAVETABLE_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_WAVETABLE_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"

namespace plaits {

// The 8 waves surrounding the current position in the wavetable, and their
// weights (sum = 2^30).
struct DuoWavetableBlend {
  const int16_t* wave[8];
  int32_t weight[8];
};

class DuoWavetableVoice {
 public:
  DuoWavetableVoice() { }
  ~DuoWavetableVoice() { }

  void Init();
  // restart: the voice was not rendered in the previous block.
  void Render(
      float f0,
      const DuoWavetableBlend* blend,
      bool restart,
      float* out,
      size_t size);

 private:
  float phase_;
  float previous_f0_;
  float reciprocal_f0_;
  Differentiator diff_out_;

  DISALLOW_COPY_AND_ASSIGN(DuoWavetableVoice);
};

class DuoWavetableEngine : public DuoEngine {
 public:
  DuoWavetableEngine() { }
  ~DuoWavetableEngine() { }

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
  void ComputeBlend(
      const EngineParameters& parameters,
      DuoWavetableBlend* blend,
      size_t size);
  void UpdateWaves(int x, int y, int z);

  DuoWavetableVoice voice_[kNumDuoVoices];
  bool second_voice_active_;

  // Position in the wavetable, shared by the two voices.
  float x_pre_lp_;
  float y_pre_lp_;
  float z_pre_lp_;

  float x_lp_;
  float y_lp_;
  float z_lp_;

  float previous_x_;
  float previous_y_;
  float previous_z_;

  int cell_;
  const int16_t* wave_[8];


  DISALLOW_COPY_AND_ASSIGN(DuoWavetableEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_WAVETABLE_ENGINE_H_
