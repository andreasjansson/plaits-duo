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

#include "plaits/dsp/duo/duo_grain_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

// 1.5 semitone between the two formants when they would otherwise coincide.
const float kDuoFormantSpread = 1.0905077f;

void DuoGrainVoice::Init() {
  grainlet_.Init();
  dc_blocker_[0].Init();
  dc_blocker_[1].Init();
}

void DuoGrainVoice::Render(
    const EngineParameters& parameters,
    bool mirror,
    float* temp_1,
    float* temp_2,
    float* left,
    float* right,
    size_t size) {
  const float root = parameters.note;
  const float f0 = NoteToFrequency(root);

  const float f1 = NoteToFrequency(24.0f + 84.0f * parameters.timbre);
  const float ratio = SemitonesToRatio(-24.0f + 48.0f * parameters.harmonics);
  const float carrier_bleed = parameters.harmonics < 0.5f
      ? 1.0f - 2.0f * parameters.harmonics
      : 0.0f;
  const float carrier_bleed_fixed = carrier_bleed * (2.0f - carrier_bleed);
  const float carrier_shape = 0.33f + (parameters.morph - 0.33f) * \
      max(1.0f - f0 * 24.0f, 0.0f);

  dc_blocker_[0].set_f<FREQUENCY_DIRTY>(0.3f * f0);
  if (!right) {
    grainlet_.Render(
        f0, f1, f1 * ratio, carrier_shape, carrier_bleed_fixed,
        left, temp_1, size);
    for (size_t i = 0; i < size; ++i) {
      left[i] = dc_blocker_[0].Process<FILTER_MODE_HIGH_PASS>(
          left[i] + temp_1[i]);
    }
  } else {
    grainlet_.Render(
        f0, f1, f1 * ratio * kDuoFormantSpread, carrier_shape,
        carrier_bleed_fixed, temp_1, temp_2, size);
    dc_blocker_[1].set_f<FREQUENCY_DIRTY>(0.3f * f0);
    float* first_formant_side = mirror ? right : left;
    float* second_formant_side = mirror ? left : right;
    for (size_t i = 0; i < size; ++i) {
      const float a = temp_1[i];
      const float b = temp_2[i];
      first_formant_side[i] += dc_blocker_[0].Process<FILTER_MODE_HIGH_PASS>(
          kDuoWidePanNear * a + kDuoWidePanFar * b);
      second_formant_side[i] += dc_blocker_[1].Process<FILTER_MODE_HIGH_PASS>(
          kDuoWidePanNear * b + kDuoWidePanFar * a);
    }
  }
}

void DuoGrainEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    voice_[i].Init();
  }
  temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
}

void DuoGrainEngine::Reset() {

}

void DuoGrainEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (stereo) {
    fill(&out[0], &out[size], 0.0f);
    fill(&aux[0], &aux[size], 0.0f);
  }
  for (int i = 0; i < num_voices; ++i) {
    voice_[i].Render(
        parameters[i],
        i == 1,
        temp_buffer_[0],
        temp_buffer_[1],
        stereo ? out : (i == 0 ? out : aux),
        stereo ? aux : NULL,
        size);
  }
}

}  // namespace plaits
