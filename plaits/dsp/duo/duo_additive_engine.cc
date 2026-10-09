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

// The phase difference of one harmonic is updated in each block, so all of
// them are updated every kNumDuoVoices * kDuoNumHarmonics blocks (12ms).
const int kRotationsPerBlock = 1;

// Rate (in Hz) of the LFO which changes the phase difference of harmonic k
// + 1 between the two channels: the higher harmonics have rates spread
// between 0.12 and 0.4Hz (using the fractional parts of multiples of the
// golden ratio).
inline float RotationRate(int k) {
  if (k < 3) {
    return 0.11f * static_cast<float>(k + 1);
  }
  float x = static_cast<float>(k + 1) * 0.618034f;
  x -= static_cast<float>(static_cast<int>(x));
  return 0.4f * (0.3f + 0.7f * x);
}

void DuoAdditiveEngine::Init(BufferAllocator* allocator) {
  fill(&amplitudes_[0], &amplitudes_[kDuoNumHarmonics], 0.0f);
  for (int i = 0; i < kNumDuoVoices; ++i) {
    bank_[i].Init();
    for (int k = 0; k < kDuoNumHarmonics; ++k) {
      float x = static_cast<float>(k + 1) * 0.414214f + 0.37f * i;
      rotation_phase_[i][k] = x - static_cast<float>(static_cast<int>(x));
    }
  }
  for (int i = 0; i < kNumDuoVoices; ++i) {
    for (int k = 0; k < kDuoNumHarmonics; ++k) {
      rotation_cos_[i][k] = 0.25f;
      rotation_sin_[i][k] = 0.0f;
    }
  }
  rotation_counter_ = 0;
}

void DuoAdditiveEngine::UpdateRotations() {
  const float period = static_cast<float>(
      kNumDuoVoices * kDuoNumHarmonics / kRotationsPerBlock * kBlockSize) / \
      kCorrectedSampleRate;
  for (int n = 0; n < kRotationsPerBlock; ++n) {
    const int i = rotation_counter_ / kDuoNumHarmonics;
    const int k = rotation_counter_ % kDuoNumHarmonics;
    if (++rotation_counter_ >= kNumDuoVoices * kDuoNumHarmonics) {
      rotation_counter_ = 0;
    }
    if (k == 0 || (k & 1) == 0) {
      // The fundamental is the same in both channels, and the phase
      // difference of an odd harmonic is the opposite of that of the
      // harmonic below it (see below).
      continue;
    }
    // The harmonic is advanced in phase by psi (in cycles) in the left
    // channel, and delayed by psi in the right channel.
    float phase = rotation_phase_[i][k];
    float psi;
    if (k < 3) {
      // Harmonics 2 and 3 swing around zero: phase differences of 0.5 and 1
      // radian at most.
      phase += RotationRate(k) * period;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      psi = 0.0397887f * static_cast<float>(k) * \
          Interpolate(lut_sine, phase, 1024.0f);
      if (psi < 0.0f) {
        psi += 1.0f;
      }
    } else {
      // The phase difference (2 psi) turns at the rate of the LFO.
      phase += 0.5f * RotationRate(k) * period;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      psi = phase;
    }
    rotation_phase_[i][k] = phase;
    // Scaled as expected by DuoHarmonicBank::RenderStereo. The cosine is a
    // quarter of a cycle later in the table.
    const float index = psi * 1024.0f;
    MAKE_INTEGRAL_FRACTIONAL(index)
    const float* table = &lut_sine[index_integral];
    const float sine = 0.5f * (
        table[0] + (table[1] - table[0]) * index_fractional);
    const float cosine = 0.25f * (
        table[256] + (table[257] - table[256]) * index_fractional);
    rotation_sin_[i][k] = sine;
    rotation_cos_[i][k] = cosine;
    // The harmonic above turns the other way: since the amplitudes of
    // neighbouring harmonics are close, their pulls towards the channel
    // which leads cancel out, and the image stays centered at all times.
    if (k + 1 < kDuoNumHarmonics) {
      rotation_sin_[i][k + 1] = -sine;
      rotation_cos_[i][k + 1] = cosine;
    }
  }
}

void DuoAdditiveEngine::Reset() {

}

// Same as AdditiveEngine::UpdateAmplitudes, with integer harmonics (and the
// same results, with fewer conversions between integers and floats).
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

  float harmonic = 0.0f;
  for (size_t i = 0; i < num_harmonics; ++i) {
    float order = fabsf(harmonic - center) * slope;
    harmonic += 1.0f;
    float gain = 1.0f - order;
    gain += fabsf(gain);
    gain *= gain;

    // InterpolateWrap(lut_sine, b, 1024.0f), wrapping the integral part of
    // the index (b is positive).
    float b = (0.25f + order * bumps) * 1024.0f;
    const int32_t b_integral = static_cast<int32_t>(b);
    const float b_fractional = b - static_cast<float>(b_integral);
    const float* sine = &lut_sine[b_integral & 1023];
    float bump_factor = 1.0f + (sine[0] + (sine[1] - sine[0]) * b_fractional);

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

  if (!stereo) {
    for (int i = 0; i < num_voices; ++i) {
      bank_[i].Render(
          NoteToFrequency(parameters[i].note),
          amplitudes_,
          i == 0 ? out : aux,
          size);
    }
    return;
  }
  
  UpdateRotations();
  for (int i = 0; i < num_voices; ++i) {
    bank_[i].RenderStereo(
        NoteToFrequency(parameters[i].note),
        amplitudes_,
        rotation_cos_[i],
        rotation_sin_[i],
        i != 0,
        out,
        aux,
        size);
  }
}

}  // namespace plaits
