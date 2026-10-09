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
// Duo version of the pair of classic waveforms.

#include "plaits/dsp/duo/duo_virtual_analog_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const float duo_va_intervals[5] = {
  0.0f, 7.01f, 12.01f, 19.01f, 24.01f
};

inline float DuoVASquash(float x) {
  return x * x * (3.0f - 2.0f * x);
}

// Same as VirtualAnalogEngine::ComputeDetuning.
float DuoVAComputeDetuning(float detune) {
  detune = 2.05f * detune - 1.025f;
  CONSTRAIN(detune, -1.0f, 1.0f);

  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 3.9999f;
  MAKE_INTEGRAL_FRACTIONAL(detune);

  float a = duo_va_intervals[detune_integral];
  float b = duo_va_intervals[detune_integral + 1];
  return (a + (b - a) * DuoVASquash(DuoVASquash(detune_fractional))) * sign;
}

}  // namespace

void DuoVirtualAnalogVoice::Init() {
  square_.Init();
  saw_.Init();
  square_gain_ = 0.0f;
  saw_gain_ = 0.0f;
}

void DuoVirtualAnalogVoice::Render(
    const EngineParameters& parameters,
    bool mirror,
    float* temp_square,
    float* temp_saw,
    float* left,
    float* right,
    size_t size) {
  // Identical to the computation of OUT in VirtualAnalogEngine (variant 2).
  const float auxiliary_detune = DuoVAComputeDetuning(parameters.harmonics);
  const float primary_f = NoteToFrequency(parameters.note);
  const float auxiliary_f = NoteToFrequency(parameters.note + auxiliary_detune);

  float square_pw = 1.3f * parameters.timbre - 0.15f;
  CONSTRAIN(square_pw, 0.005f, 0.5f);

  const float square_sync_ratio = parameters.timbre < 0.5f
      ? 0.0f
      : (parameters.timbre - 0.5f) * (parameters.timbre - 0.5f) * 4.0f * 48.0f;

  const float square_gain = min(parameters.timbre * 8.0f, 1.0f);

  float saw_pw = parameters.morph < 0.5f
      ? parameters.morph + 0.5f
      : 1.0f - (parameters.morph - 0.5f) * 2.0f;
  saw_pw *= 1.1f;
  CONSTRAIN(saw_pw, 0.005f, 1.0f);

  float saw_shape = 10.0f - 21.0f * parameters.morph;
  CONSTRAIN(saw_shape, 0.0f, 1.0f);

  float saw_gain = 8.0f * (1.0f - parameters.morph);
  CONSTRAIN(saw_gain, 0.02f, 1.0f);

  const float square_sync_f = NoteToFrequency(
      parameters.note + square_sync_ratio);

  square_.Render<true>(
      primary_f, square_sync_f, square_pw, 1.0f, temp_square, size);

  float norm = 1.0f / (std::max(square_gain, saw_gain));

  ParameterInterpolator square_gain_modulation(
      &square_gain_,
      square_gain * 0.3f * norm,
      size);

  ParameterInterpolator saw_gain_modulation(
      &saw_gain_,
      saw_gain * 0.5f * norm,
      size);

  if (!right) {
    saw_.Render(auxiliary_f, saw_pw, saw_shape, left, size);
    for (size_t i = 0; i < size; ++i) {
      left[i] = left[i] * saw_gain_modulation.Next() + \
          square_gain_modulation.Next() * temp_square[i];
    }
  } else {
    saw_.Render(auxiliary_f, saw_pw, saw_shape, temp_saw, size);
    float* square_side = mirror ? right : left;
    float* saw_side = mirror ? left : right;
    for (size_t i = 0; i < size; ++i) {
      const float square = square_gain_modulation.Next() * temp_square[i];
      const float saw = saw_gain_modulation.Next() * temp_saw[i];
      square_side[i] += kDuoWidePanNear * square + kDuoWidePanFar * saw;
      saw_side[i] += kDuoWidePanNear * saw + kDuoWidePanFar * square;
    }
  }
}

void DuoVirtualAnalogEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    voice_[i].Init();
  }
  temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
}

void DuoVirtualAnalogEngine::Reset() {

}

void DuoVirtualAnalogEngine::Render(
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
