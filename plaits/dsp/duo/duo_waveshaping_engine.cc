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
// Duo version of the waveshaping oscillator.

#include "plaits/dsp/duo/duo_waveshaping_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/dsp.h"

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Same as Tame() in waveshaping_engine.cc.
float DuoWSTame(float f0, float harmonics, float order) {
  f0 *= harmonics;
  float max_f = 0.5f / order;
  float max_amount = 1.0f - (f0 - max_f) / (0.5f - max_f);
  CONSTRAIN(max_amount, 0.0f, 1.0f);
  return max_amount * max_amount * max_amount;
}

// Relative difference between the drive of the two wavefolders.
const float kDuoFolderSpread = 0.2f;

}  // namespace

void DuoWaveshapingVoice::Init() {
  slope_.Init();
  previous_shape_ = 0.0f;
  previous_wavefolder_gain_ = 0.0f;
}

void DuoWaveshapingVoice::Render(
    const EngineParameters& parameters,
    bool mirror,
    float* temp,
    float* left,
    float* right,
    size_t size) {
  const float root = parameters.note;

  const float f0 = NoteToFrequency(root);
  const float pw = parameters.morph * 0.45f + 0.5f;

  // Start from bandlimited slope signal.
  slope_.Render<OSCILLATOR_SHAPE_SLOPE>(f0, pw, temp, size);

  // Try to estimate how rich the spectrum is, and reduce the range of the
  // waveshaping control accordingly.
  const float slope = 3.0f + fabsf(parameters.morph - 0.5f) * 5.0f;
  const float shape_amount = fabsf(parameters.harmonics - 0.5f) * 2.0f;
  const float shape_amount_attenuation = DuoWSTame(f0, slope, 16.0f);
  const float wavefolder_gain = parameters.timbre;
  const float wavefolder_gain_attenuation = DuoWSTame(
      f0,
      slope * (3.0f + shape_amount * shape_amount_attenuation * 5.0f),
      12.0f);

  // Apply waveshaper / wavefolder.
  ParameterInterpolator shape_modulation(
      &previous_shape_,
      0.5f + (parameters.harmonics - 0.5f) * shape_amount_attenuation,
      size);
  ParameterInterpolator wf_gain_modulation(
      &previous_wavefolder_gain_,
      0.03f + 0.46f * wavefolder_gain * wavefolder_gain_attenuation,
      size);

  float* low_drive_side = mirror ? right : left;
  float* high_drive_side = mirror ? left : right;
  
  for (size_t i = 0; i < size; ++i) {
    float shape = shape_modulation.Next() * 3.9999f;
    MAKE_INTEGRAL_FRACTIONAL(shape);
    
    const int16_t* shape_1 = lookup_table_i16_table[shape_integral];
    const int16_t* shape_2 = lookup_table_i16_table[shape_integral + 1];
    
    float ws_index = 127.0f * temp[i] + 128.0f;
    MAKE_INTEGRAL_FRACTIONAL(ws_index)
    ws_index_integral &= 255;
    
    float x0 = static_cast<float>(shape_1[ws_index_integral]) / 32768.0f;
    float x1 = static_cast<float>(shape_1[ws_index_integral + 1]) / 32768.0f;
    float x = x0 + (x1 - x0) * ws_index_fractional;

    float y0 = static_cast<float>(shape_2[ws_index_integral]) / 32768.0f;
    float y1 = static_cast<float>(shape_2[ws_index_integral + 1]) / 32768.0f;
    float y = y0 + (y1 - y0) * ws_index_fractional;
    
    float mix = x + (y - x) * shape_fractional;
    if (!right) {
      float index = mix * wf_gain_modulation.Next() + 0.5f;
      left[i] = InterpolateHermite(lut_fold + 1, index, 512.0f);
    } else {
      float drive = mix * wf_gain_modulation.Next();
      low_drive_side[i] += InterpolateHermite(
          lut_fold + 1, drive * (1.0f - kDuoFolderSpread) + 0.5f, 512.0f);
      high_drive_side[i] += InterpolateHermite(
          lut_fold + 1, drive * (1.0f + kDuoFolderSpread) + 0.5f, 512.0f);
    }
  }
}

void DuoWaveshapingEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    voice_[i].Init();
  }
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
}

void DuoWaveshapingEngine::Reset() {

}

void DuoWaveshapingEngine::Render(
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
        temp_buffer_,
        stereo ? out : (i == 0 ? out : aux),
        stereo ? aux : NULL,
        size);
  }
}

}  // namespace plaits
