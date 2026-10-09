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
// Base class for the engines used in duo mode. A duo engine renders two
// voices of the same model at once, so that the voices can share whatever
// does not depend on pitch (spectral envelopes, word banks, string pools...).
//
// Output conventions:
// - stereo == true: out and aux receive the left and right channels of the
//   mix of all rendered voices.
// - stereo == false: out receives voice 0 and aux receives voice 1. When
//   only one voice is rendered, aux is left untouched.
//
// Only the OUT signal of the original engine is synthesized: the AUX
// variants of the original models are not used in duo mode, and the CPU
// time they would have used is spent on the second voice and on the stereo
// image instead.

#ifndef PLAITS_DSP_DUO_DUO_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumDuoVoices = 2;
const int kNumDuoEngines = 8;

// Equal-power gains for a source panned 1/3 of the way towards one side.
const float kDuoPanNear = 0.8660254f;
const float kDuoPanFar = 0.5f;

// Equal-power gains for a source panned 60% of the way towards one side.
const float kDuoWidePanNear = 0.9510565f;
const float kDuoWidePanFar = 0.3090170f;

class DuoEngine {
 public:
  DuoEngine() { }
  ~DuoEngine() { }
  virtual void Init(stmlib::BufferAllocator* allocator) = 0;
  virtual void Reset() = 0;
  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) = 0;

  PostProcessingSettings post_processing_settings;

  // In duo mode, out_gain is used for the split layout, and aux_gain for the
  // stereo layout (see DuoVoice::Init).
  void set_post_processing_settings(
      bool already_enveloped,
      float split_gain,
      float stereo_gain) {
    post_processing_settings.already_enveloped = already_enveloped;
    post_processing_settings.out_gain = split_gain;
    post_processing_settings.aux_gain = stereo_gain;
  }
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_ENGINE_H_
