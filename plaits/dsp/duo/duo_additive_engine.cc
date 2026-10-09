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

#include "plaits/dsp/duo/duo_additive_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void DuoAdditiveEngine::Init(BufferAllocator* allocator) {
  fill(&amplitudes_[0], &amplitudes_[kDuoNumHarmonics], 0.0f);
  for (int i = 0; i < kNumDuoVoices; ++i) {
    bank_[i].Init();
  }
  temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
}

void DuoAdditiveEngine::Reset() {

}

// Same as AdditiveEngine::UpdateAmplitudes, with integer harmonics.
void DuoAdditiveEngine::UpdateAmplitudes(
    float centroid,
    float slope,
    float bumps,
    float* amplitudes,
    size_t num_harmonics) {
  const float n = (static_cast<float>(num_harmonics) - 1.0f);
  const float margin = (1.0f / slope - 1.0f) / (1.0f + bumps);
  const float center = centroid * (n + margin) - 0.5f * margin;

  float sum = 0.001f;

  for (size_t i = 0; i < num_harmonics; ++i) {
    float order = fabsf(static_cast<float>(i) - center) * slope;
    float gain = 1.0f - order;
    gain += fabsf(gain);
    gain *= gain;

    float b = 0.25f + order * bumps;
    float bump_factor = 1.0f + InterpolateWrap(lut_sine, b, 1024.0f);

    gain *= bump_factor;
    gain *= gain;
    gain *= gain;

    ONE_POLE(amplitudes[i], gain, 0.001f);
    sum += amplitudes[i];
  }

  sum = 1.0f / sum;

  for (size_t i = 0; i < num_harmonics; ++i) {
    amplitudes[i] *= sum;
  }
}

void DuoAdditiveEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // The spectral envelope only depends on TIMBRE, MORPH and HARMONICS, which
  // are shared by the two voices.
  const EngineParameters& p = parameters[0];
  const float centroid = p.timbre;
  const float raw_bumps = p.harmonics;
  const float raw_slope = (1.0f - 0.6f * raw_bumps) * p.morph;
  const float slope = 0.01f + 1.99f * raw_slope * raw_slope * raw_slope;
  const float bumps = 16.0f * raw_bumps * raw_bumps;
  UpdateAmplitudes(
      centroid,
      slope,
      bumps,
      &amplitudes_[0],
      kDuoNumHarmonics);

  if (stereo) {
    fill(&out[0], &out[size], 0.0f);
    fill(&aux[0], &aux[size], 0.0f);
  }
  for (int i = 0; i < num_voices; ++i) {
    bank_[i].Render(
        NoteToFrequency(parameters[i].note),
        amplitudes_,
        stereo,
        i == 1,
        temp_buffer_[0],
        temp_buffer_[1],
        stereo ? out : (i == 0 ? out : aux),
        aux,
        size);
  }
}

}  // namespace plaits
