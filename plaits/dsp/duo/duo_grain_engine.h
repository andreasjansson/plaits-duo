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
// Duo version of the granular formant oscillator.
//
// Stereo: the original OUT is the sum of two grainlet oscillators sharing the
// same carrier, with formants set by TIMBRE and TIMBRE * HARMONICS ratio.
// Each formant is placed on one side of the stereo field, and the second
// formant is offset by a small interval so that the channels still differ
// when HARMONICS is at 12 o'clock (where the two formants coincide).

#ifndef PLAITS_DSP_DUO_DUO_GRAIN_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_GRAIN_ENGINE_H_

#include "stmlib/dsp/filter.h"

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/dual_grainlet_oscillator.h"

namespace plaits {

class DuoGrainVoice {
 public:
  DuoGrainVoice() { }
  ~DuoGrainVoice() { }

  void Init();

  // When right is NULL, writes the mono signal to left. Otherwise, adds the
  // stereo signal to left and right.
  void Render(
      const EngineParameters& parameters,
      bool mirror,
      float* temp_1,
      float* temp_2,
      float* left,
      float* right,
      size_t size);

 private:
  DualGrainletOscillator grainlet_;
  stmlib::OnePole dc_blocker_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoGrainVoice);
};

class DuoGrainEngine : public DuoEngine {
 public:
  DuoGrainEngine() { }
  ~DuoGrainEngine() { }

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
  DuoGrainVoice voice_[kNumDuoVoices];
  float* temp_buffer_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoGrainEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_GRAIN_ENGINE_H_
